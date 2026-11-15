/* Transport subviews. All LVGL operations remain on the UI task. */
static const station_transport_row_t *detail_row(void){
 for(unsigned i=0;i<transport.row_count;i++){const station_transport_row_t *r=&transport.rows[i];if(!strcmp(r->line,detail_line)&&!strcmp(r->destination,detail_destination)&&!strcmp(r->point,detail_point))return r;}return NULL;
}
static void line_map_open(lv_event_t *event){
 (void)event;if(actions.transport_line_map&&actions.transport_line_map(detail_line)){map_line_mode=true;create_route_diagram();}
 else lv_label_set_text(detail_freshness,"Line map request busy or unavailable. Please retry.");
}
static void update_service_detail(const char *alerts,bool stale){
 (void)alerts;const station_transport_row_t *r=detail_row();
 if(!r||!transport.valid){lv_label_set_text(detail_times,"Service no longer in the latest stop update.");lv_label_set_text(detail_freshness,"Return to services to select another departure.");return;}
 char title[180],times[280],pickup[180],freshness[160];
 snprintf(title,sizeof(title),"%s · %s",r->code,r->destination);lv_label_set_text(transport_title,title);
 departure_text(times,sizeof(times),r,time(NULL),stale);lv_label_set_text(detail_times,times);
 snprintf(pickup,sizeof(pickup),"Boarding point: %s%s%s",r->pickup[0]?r->pickup:transport.selected<transport.count?transport.stops[transport.selected].name:"Not supplied",r->platform[0]?" · Platform ":"",r->platform);lv_label_set_text(detail_pickup,pickup);
 snprintf(freshness,sizeof(freshness),"%s · fetched %ld min ago · Île-de-France Mobilités",stale?"Out of date":"Stop-specific predictions",(long)((time(NULL)-transport.fetched_at)/60));lv_label_set_text(detail_freshness,freshness);
 char notices[1800]="";size_t used=0;unsigned matches=0;
 for(unsigned i=0;i<transport.alert_count;i++)if((!strncmp(transport.alerts[i],r->code,strlen(r->code))&&transport.alerts[i][strlen(r->code)]==':')||(!strncmp(transport.alerts[i],r->line,strlen(r->line))&&transport.alerts[i][strlen(r->line)]==':')){
  int n=snprintf(notices+used,sizeof(notices)-used,"%s%s",matches?"\n\n":"",transport.alerts[i]);if(n<0||(size_t)n>=sizeof(notices)-used)break;used+=(size_t)n;matches++;
 }
 if(!transport.traffic_valid||time(NULL)-transport.traffic_at>2700)snprintf(notices+used,sizeof(notices)-used,"%sDisruption information is unavailable or incomplete.",used?"\n\n":"");
 else if(!matches)snprintf(notices,sizeof(notices),transport.traffic_partial?"Disruption coverage is incomplete.":"No active disruption reported for this line.");
 lv_label_set_text(transport_alert_label,notices);
}
static void open_service_detail(lv_event_t *event){
 unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(!transport.valid||index>=transport.row_count)return;
 const station_transport_row_t *r=&transport.rows[index];snprintf(detail_line,20,"%s",r->line);snprintf(detail_destination,128,"%s",r->destination);snprintf(detail_point,64,"%s",r->point);
 transport_detail_open=true;transport_map=false;transport_search_open=false;lv_obj_clean(content);
 transport_title=label(content,"",32,12,&station_text_32);lv_obj_set_width(transport_title,500);lv_label_set_long_mode(transport_title,LV_LABEL_LONG_DOT);
 button(content,"Near me",570,4,200,nearby_again,NULL);button(content,"Services",1040,4,200,navigate,(void *)(uintptr_t)VIEW_TRANSPORT);
 detail_pickup=label(content,"",32,78,&station_text_24);lv_obj_set_width(detail_pickup,1200);
 detail_times=label(content,"",56,142,&station_text_32);lv_obj_set_style_text_line_space(detail_times,20,0);lv_obj_set_width(detail_times,1160);
 detail_freshness=label(content,"",32,316,&station_text_18);
 button(content,"Route diagram",32,358,300,line_map_open,NULL);
 label(content,"Current disruptions",32,434,&station_text_24);
 lv_obj_t *notices=box(content,32,474,1216,172,CARD);lv_obj_add_flag(notices,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(notices,LV_DIR_VER);
 transport_alert_label=label(notices,"",16,12,&station_text_18);lv_obj_set_width(transport_alert_label,1168);lv_obj_set_style_text_line_space(transport_alert_label,8,0);
 update_transport();
}
static lv_obj_t *search_spinner,*search_heading;
static unsigned connection_index;
static bool showing_connections;
static void search_choose(lv_event_t *event){
 unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);
 if(actions.transport_search_select&&actions.transport_search_select(index))show_view(VIEW_TRANSPORT);
 else lv_label_set_text(search_status,"Station selection could not be queued. Please retry.");
}
static void search_submit(lv_event_t *event){
 (void)event;lv_obj_add_flag(network_keyboard,LV_OBJ_FLAG_HIDDEN);
 if((showing_connections&&actions.transport_line_stop_select&&actions.transport_line_stop_select(connection_index))||(!showing_connections&&actions.transport_search&&actions.transport_search(lv_textarea_get_text(search_input)))){lv_label_set_text(search_status,"Searching stations…");search_revision=UINT32_MAX;}
 else lv_label_set_text(search_status,"Enter at least two characters, or wait a moment before searching again.");
}
static void search_keyboard(lv_event_t *event){if(lv_event_get_code(event)==LV_EVENT_READY)search_submit(event);}
static void nearby_again(lv_event_t *event){(void)event;if(actions.transport_nearby_reset)actions.transport_nearby_reset();show_view(VIEW_TRANSPORT);}
static void update_station_search(void){
 if(!actions.transport_search_status)return;
 actions.transport_search_status(&station_search);
 if(station_search.revision==search_revision)return;
 search_revision=station_search.revision;
 if(station_search.state==LOOKUP_LOADING)lv_obj_remove_flag(search_spinner,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(search_spinner,LV_OBJ_FLAG_HIDDEN);
 if(showing_connections){char heading[160];snprintf(heading,sizeof(heading),"Connections · %s",station_search.query);set_label_if_changed(search_heading,heading);}
 const char *text=station_search.state==LOOKUP_LOADING?"Searching stations…":station_search.state==LOOKUP_FAILED?"Station search unavailable. Check Wi-Fi and try again.":station_search.state==LOOKUP_READY?(station_search.count?"Select a station to see its live services.":"No stations found. Try a station name or a town."):"Search by station name, e.g. Robinson or La Croix de Berny.";
 if(showing_connections)text=station_search.state==LOOKUP_LOADING?"Finding connecting services…":station_search.state==LOOKUP_FAILED?"Connections could not load. Tap Retry.":station_search.partial?"Some stops omitted by provider/limit · select a boarding point below.":"All available modes · select a boarding point for live departures.";
 lv_label_set_text(search_status,text);
 for(unsigned i=0;i<TRANSPORT_SEARCH_STOPS;i++){
  if(i<station_search.count&&station_search.state==LOOKUP_READY){char caption[300];station_transport_stop_t *p=&station_search.stops[i];snprintf(caption,sizeof(caption),"%s%s%s · %s",p->mode,station_search.services[i][0]?" ":"",station_search.services[i],p->name);lv_label_set_text(search_names[i],caption);lv_obj_remove_flag(search_buttons[i],LV_OBJ_FLAG_HIDDEN);}
  else lv_obj_add_flag(search_buttons[i],LV_OBJ_FLAG_HIDDEN);
 }
}
static void open_station_search(lv_event_t *event){
 showing_connections=event==NULL;transport_search_open=true;transport_detail_open=false;transport_map=false;lv_obj_clean(content);search_revision=UINT32_MAX;
 search_heading=label(content,showing_connections?"Station connections":"Find a station",32,14,&station_text_24);lv_obj_set_width(search_heading,520);lv_label_set_long_mode(search_heading,LV_LABEL_LONG_DOT);button(content,"Near me",570,4,200,nearby_again,NULL);button(content,"Services",1040,4,200,navigate,(void *)(uintptr_t)VIEW_TRANSPORT);
 search_input=lv_textarea_create(content);lv_obj_set_pos(search_input,32,82);lv_obj_set_size(search_input,930,58);lv_textarea_set_one_line(search_input,true);lv_textarea_set_max_length(search_input,63);lv_textarea_set_placeholder_text(search_input,"Station or town");lv_obj_add_event_cb(search_input,focus_input,LV_EVENT_FOCUSED,NULL);lv_obj_add_event_cb(search_input,focus_input,LV_EVENT_CLICKED,NULL);
 button(content,showing_connections?"Retry":"Search stations",982,82,258,search_submit,NULL);if(showing_connections){lv_obj_add_flag(search_input,LV_OBJ_FLAG_HIDDEN);label(content,"Choose a connecting service / boarding point",32,98,&station_text_24);}
 search_spinner=lv_spinner_create(content);lv_obj_set_pos(search_spinner,32,148);lv_obj_set_size(search_spinner,32,32);lv_spinner_set_anim_params(search_spinner,900,200);
 search_status=label(content,"",78,154,&station_text_18);lv_obj_set_width(search_status,1160);
 lv_obj_t *results=box(content,32,192,1216,450,BG);lv_obj_add_flag(results,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(results,LV_DIR_VER);
 for(unsigned i=0;i<TRANSPORT_SEARCH_STOPS;i++){search_buttons[i]=button(results,"",0,(int)i*64,1200,search_choose,(void *)(uintptr_t)i);search_names[i]=label(search_buttons[i],"",16,16,&station_text_24);lv_obj_set_width(search_names[i],1160);lv_label_set_long_mode(search_names[i],LV_LABEL_LONG_DOT);}
 keyboard(search_input);lv_obj_add_event_cb(network_keyboard,search_keyboard,LV_EVENT_READY,NULL);update_station_search();
}
#define MAP_W 780
#define MAP_H 450
#define MAP_TILES 20
typedef struct {lv_obj_t *image;lv_image_dsc_t descriptor;station_map_tile_t tile;unsigned z;int x,y;} tile_view_t;
static tile_view_t map_tiles[MAP_TILES];
static lv_obj_t *map_busy;
static lv_obj_t *map_viewport,*map_world,*map_tile_layer,*map_list,*map_home,*map_home_name;
static lv_obj_t *map_markers[TRANSPORT_LINE_STOPS],*map_stops[TRANSPORT_LINE_STOPS],*map_captions[TRANSPORT_LINE_STOPS];
static double map_lat,map_lon;static unsigned map_zoom=14,map_revision;static bool map_fit_pending,map_dragging;static lv_point_t map_press;
static void tile_deleted(lv_event_t *event){tile_view_t *t=lv_event_get_user_data(event);if(t->tile.handle)lv_image_cache_drop(&t->descriptor);if(t->tile.handle&&actions.map_tile_release)actions.map_tile_release(t->tile.handle);t->tile.handle=0;t->image=NULL;}
static bool map_geometry_dirty=true,map_moved;
static unsigned map_drawn_revision,map_drawn_location;
static void map_reset_tiles(void){
 map_geometry_dirty=true;
 lv_obj_clean(map_tile_layer);memset(map_tiles,0,sizeof(map_tiles));lv_obj_set_pos(map_world,0,0);if(actions.map_cancel_pending)actions.map_cancel_pending();
}
static unsigned map_count(void){return map_line_mode?line_map.count:transport.count;}
static bool map_point(unsigned i,double *lat,double *lon,const char **name,const char **mode){
 if(map_line_mode){if(i>=line_map.count)return false;station_line_stop_t *p=&line_map.stops[i];*lat=p->latitude;*lon=p->longitude;*name=p->name;*mode=p->mode;return p->coordinates;}
 if(i>=transport.count)return false;
 station_transport_stop_t *p=&transport.stops[i];*lat=p->latitude;*lon=p->longitude;*name=p->name;*mode=p->mode;return p->coordinates;
}
static void map_fit(void){
 double minx=1e20,miny=1e20,maxx=-1e20,maxy=-1e20;unsigned points=0;
 for(unsigned i=0;i<map_count();i++){double lat,lon,x,y;const char *name,*mode;if(!map_point(i,&lat,&lon,&name,&mode))continue;station_map_project(lat,lon,18,&x,&y);minx=fmin(minx,x);maxx=fmax(maxx,x);miny=fmin(miny,y);maxy=fmax(maxy,y);points++;}
 if(!map_line_mode&&network.location_valid){double x,y;station_map_project(network.latitude,network.longitude,18,&x,&y);minx=fmin(minx,x);maxx=fmax(maxx,x);miny=fmin(miny,y);maxy=fmax(maxy,y);points++;}
 if(!points)return;
 map_zoom=18;while(map_zoom>3&&((maxx-minx)/(1u<<(18-map_zoom))>MAP_W-100||(maxy-miny)/(1u<<(18-map_zoom))>MAP_H-100))map_zoom--;
 if(map_zoom>16)map_zoom=16;
 station_map_unproject((minx+maxx)/2,(miny+maxy)/2,18,&map_lat,&map_lon);map_fit_pending=false;map_reset_tiles();
}
static void choose_map_stop(lv_event_t *event){
 if(!map_line_mode&&map_moved)return;
 unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);bool ok=false;
 if(map_line_mode){if(actions.transport_line_stop_select)ok=actions.transport_line_stop_select(index);}
 else if(index<transport.count&&actions.transport_select)ok=actions.transport_select(index);
 if(ok){if(map_line_mode){connection_index=index;open_station_search(NULL);}else show_view(VIEW_TRANSPORT);}
 else lv_label_set_text(transport_status_label,"Station selection unavailable or busy. Please retry.");
}
static void map_zoom_click(lv_event_t *event){
 int step=(int)(intptr_t)lv_event_get_user_data(event);int z=(int)map_zoom+step;if(z<3||z>18)return;map_zoom=(unsigned)z;map_reset_tiles();update_transport_map();
}
static void map_home_click(lv_event_t *event){(void)event;if(network.location_valid){map_lat=network.latitude;map_lon=network.longitude;map_zoom=14;map_reset_tiles();update_transport_map();}}
static void map_drag(lv_event_t *event){
 lv_indev_t *input=lv_indev_active();if(!input)return;lv_point_t p;lv_indev_get_point(input,&p);
 if(lv_event_get_code(event)==LV_EVENT_PRESSED){map_press=p;map_dragging=true;map_moved=false;}
 else if(lv_event_get_code(event)==LV_EVENT_PRESSING&&map_dragging){int x=p.x-map_press.x,y=p.y-map_press.y;if(abs(x)>8||abs(y)>8)map_moved=true;if(x!=lv_obj_get_x(map_world)||y!=lv_obj_get_y(map_world))lv_obj_set_pos(map_world,x,y);}
 else if((lv_event_get_code(event)==LV_EVENT_RELEASED||lv_event_get_code(event)==LV_EVENT_PRESS_LOST)&&map_dragging){
  map_dragging=false;double x,y;station_map_project(map_lat,map_lon,map_zoom,&x,&y);station_map_unproject(x-(p.x-map_press.x),y-(p.y-map_press.y),map_zoom,&map_lat,&map_lon);map_reset_tiles();update_transport_map();
 }
}
static void update_transport_map(void){
 if(!transport_map||map_dragging)return;
 if(map_line_mode){update_route_diagram();return;}
 if(map_line_mode&&actions.transport_line_map_status){actions.transport_line_map_status(&line_map);if(line_map.revision!=map_revision){map_revision=line_map.revision;if(line_map.state==LOOKUP_READY)map_fit_pending=true;}}
 if(map_fit_pending)map_fit();
 double cx,cy;station_map_project(map_lat,map_lon,map_zoom,&cx,&cy);int left=(int)floor((cx-MAP_W/2)/256),top=(int)floor((cy-MAP_H/2)/256),right=(int)floor((cx+MAP_W/2)/256),bottom=(int)floor((cy+MAP_H/2)/256);unsigned index=0,ready=0;
 int center_x=(int)floor(cx/256),center_y=(int)floor(cy/256);
 for(int ring=0;ring<6;ring++)for(int y=top;y<=bottom;y++)for(int x=left;x<=right&&index<MAP_TILES;x++){
  if(abs(x-center_x)+abs(y-center_y)!=ring)continue;
  tile_view_t *t=&map_tiles[index++];if(!t->image){t->z=map_zoom;t->x=x;t->y=y;t->image=lv_image_create(map_tile_layer);lv_obj_remove_flag(t->image,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(t->image,(int)(x*256-cx+MAP_W/2),(int)(y*256-cy+MAP_H/2));lv_obj_add_event_cb(t->image,tile_deleted,LV_EVENT_DELETE,t);}
  int wrapped=(x%(1<<map_zoom)+(1<<map_zoom))%(1<<map_zoom);
  if(!t->tile.handle&&actions.map_tile_acquire&&actions.map_tile_acquire(map_zoom,wrapped,y,&t->tile)){
   t->descriptor=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=256,.h=256,.stride=512},.data_size=131072,.data=(const uint8_t *)t->tile.pixels};lv_image_set_src(t->image,&t->descriptor);
  }if(t->tile.handle)ready++;
 }
 char info[200];if(ready==index)snprintf(info,sizeof(info),"Drag to pan · + / - to zoom · tap a station for live services");else snprintf(info,sizeof(info),"Loading street map: %u/%u tiles · stations remain selectable",ready,index);
 if(map_line_mode&&line_map.state==LOOKUP_LOADING)snprintf(info,sizeof(info),"Loading line stations…");
 if(map_line_mode&&line_map.state==LOOKUP_FAILED)snprintf(info,sizeof(info),"Line map unavailable. Return to service details and retry.");
 set_label_if_changed(transport_status_label,info);
 if(ready<index)lv_obj_remove_flag(map_busy,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(map_busy,LV_OBJ_FLAG_HIDDEN);
 if(!map_geometry_dirty&&map_drawn_revision==transport.revision&&map_drawn_location==network.location_generation)return;
 map_geometry_dirty=false;map_drawn_revision=transport.revision;map_drawn_location=network.location_generation;
 if(network.location_valid){double x,y;station_map_project(network.latitude,network.longitude,map_zoom,&x,&y);lv_obj_set_pos(map_home,(int)(x-cx+MAP_W/2)-7,(int)(y-cy+MAP_H/2)-7);lv_obj_set_pos(map_home_name,(int)(x-cx+MAP_W/2)+12,(int)(y-cy+MAP_H/2)-10);}
 for(unsigned i=0;i<TRANSPORT_LINE_STOPS;i++){
  if(i>=map_count()){lv_obj_add_flag(map_stops[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(map_markers[i],LV_OBJ_FLAG_HIDDEN);continue;}
  double lat,lon,x,y;const char *name,*mode;bool coords=map_point(i,&lat,&lon,&name,&mode);char caption[180];snprintf(caption,sizeof(caption),"%u · %s · %s",i+1,mode,name);lv_label_set_text(map_captions[i],caption);lv_obj_remove_flag(map_stops[i],LV_OBJ_FLAG_HIDDEN);
  if(coords){station_map_project(lat,lon,map_zoom,&x,&y);int px=(int)(x-cx+MAP_W/2),py=(int)(y-cy+MAP_H/2);if(px>=0&&px<MAP_W&&py>=0&&py<MAP_H){lv_obj_set_pos(map_markers[i],px-21,py-21);lv_obj_remove_flag(map_markers[i],LV_OBJ_FLAG_HIDDEN);}else lv_obj_add_flag(map_markers[i],LV_OBJ_FLAG_HIDDEN);}
  else lv_obj_add_flag(map_markers[i],LV_OBJ_FLAG_HIDDEN);
 }
}
static void create_transport_map(void){
 transport_map=true;transport_detail_open=false;transport_search_open=false;lv_obj_clean(content);memset(map_tiles,0,sizeof(map_tiles));map_dragging=false;map_moved=false;map_geometry_dirty=true;map_fit_pending=false;map_revision=UINT32_MAX;map_lat=network.latitude;map_lon=network.longitude;map_zoom=15;
 if(transport.selected<transport.count&&transport.stops[transport.selected].coordinates){map_lat=transport.stops[transport.selected].latitude;map_lon=transport.stops[transport.selected].longitude;}
 lv_obj_t *map_heading=label(content,"Street map · selected station",32,14,&station_text_24);lv_obj_set_size(map_heading,500,40);lv_label_set_long_mode(map_heading,LV_LABEL_LONG_DOT);button(content,"Search",800,4,220,open_station_search,NULL);button(content,"Near me",570,4,200,nearby_again,NULL);button(content,"Services",1040,4,200,navigate,(void *)(uintptr_t)VIEW_TRANSPORT);
 transport_status_label=label(content,"",68,78,&station_text_18);
 map_busy=summary_spinner(content,32,76);
 map_viewport=box(content,32,120,MAP_W,MAP_H,CARD);lv_obj_add_flag(map_viewport,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(map_viewport,map_drag,LV_EVENT_ALL,NULL);
 map_world=box(map_viewport,0,0,MAP_W,MAP_H,0xd9dfdf);lv_obj_remove_flag(map_world,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(map_world,LV_OBJ_FLAG_EVENT_BUBBLE);
 map_tile_layer=box(map_world,0,0,MAP_W,MAP_H,0xd9dfdf);lv_obj_remove_flag(map_tile_layer,LV_OBJ_FLAG_CLICKABLE);
 map_home=box(map_world,0,0,14,14,0xff951f);lv_obj_set_style_radius(map_home,7,0);lv_obj_remove_flag(map_home,LV_OBJ_FLAG_CLICKABLE);map_home_name=label(map_world,"Home",0,0,&station_text_18);lv_obj_set_style_text_color(map_home_name,lv_color_hex(0x141820),0);
 map_list=box(content,836,120,412,450,BG);lv_obj_add_flag(map_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(map_list,LV_DIR_VER);
 for(unsigned i=0;i<TRANSPORT_LINE_STOPS;i++){
  map_stops[i]=button(map_list,"",0,(int)i*62,396,choose_map_stop,(void *)(uintptr_t)i);map_captions[i]=label(map_stops[i],"",12,17,&station_text_18);lv_obj_set_width(map_captions[i],370);lv_label_set_long_mode(map_captions[i],LV_LABEL_LONG_DOT);
  char number[8];snprintf(number,sizeof(number),"%u",i+1);map_markers[i]=button(map_world,number,0,0,42,choose_map_stop,(void *)(uintptr_t)i);lv_obj_set_height(map_markers[i],42);lv_obj_add_flag(map_markers[i],LV_OBJ_FLAG_EVENT_BUBBLE);lv_obj_set_style_bg_color(map_markers[i],lv_color_hex(0x176553),0);
 }
 button(content,"+",32,584,70,map_zoom_click,(void *)(intptr_t)1);button(content,"-",114,584,70,map_zoom_click,(void *)(intptr_t)-1);button(content,"My location",196,584,170,map_home_click,NULL);
 label(content,"© OpenStreetMap contributors",390,600,&station_text_18);
 if(map_line_mode)label(content,"Stations, not a track diagram",836,594,&station_text_18);
 update_transport_map();
}
static void open_transport_map(lv_event_t *event){(void)event;if(actions.transport_status)actions.transport_status(&transport);if(actions.network_status)actions.network_status(&network);map_line_mode=false;create_transport_map();}

static lv_obj_t *route_picker,*route_list,*route_retry_button,*route_spinner,*route_loading;
static unsigned route_revision=UINT32_MAX;
static void draw_route(lv_event_t *event){
 (void)event;lv_obj_clean(route_list);lv_obj_scroll_to_y(route_list,0,LV_ANIM_OFF);
 unsigned selected=lv_dropdown_get_selected(route_picker);if(selected>=line_map.route_count)return;
 const station_line_route_t *route=&line_map.routes[selected];
 for(unsigned i=0;i<route->count;i++){
  unsigned index=route->stops[i];if(index>=line_map.count)continue;
  int y=(int)i*112;
  if(i+1<route->count){lv_obj_t *rail=box(route_list,31,y+24,8,112,line_map.color);lv_obj_set_style_radius(rail,0,0);lv_obj_remove_flag(rail,LV_OBJ_FLAG_CLICKABLE);}
  lv_obj_t *dot=box(route_list,22,y+19,26,26,0xffffff);lv_obj_set_style_radius(dot,13,0);lv_obj_remove_flag(dot,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *station=button(route_list,line_map.stops[index].name,68,y,1080,choose_map_stop,(void *)(uintptr_t)index);
  lv_obj_set_height(station,94);
  lv_obj_t *caption=lv_obj_get_child(station,0);lv_obj_align(caption,LV_ALIGN_TOP_LEFT,16,8);
  const station_line_stop_t *stop=&line_map.stops[index];
  const char *connections=stop->connections_known?(stop->connections[0]?stop->connections:"No other connecting lines listed"):(line_map.connections_loading?"Loading connections…":"Connections unavailable · tap to check");
  lv_obj_t *links=label(station,connections,16,43,&station_text_18);lv_obj_set_width(links,1030);lv_label_set_long_mode(links,LV_LABEL_LONG_WRAP);lv_obj_set_height(links,45);lv_obj_set_width(caption,1030);lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);
  if(transport.selected<transport.count&&transport.stops[transport.selected].id[0]&&!strcmp(transport.stops[transport.selected].id,line_map.stops[index].id))lv_obj_set_style_bg_color(station,lv_color_hex(0x295947),0);
 }
}
static void route_retry(lv_event_t *event){(void)event;if(actions.transport_line_map&&actions.transport_line_map(detail_line)){route_revision=UINT32_MAX;set_label_if_changed(transport_status_label,"Loading route diagram...");}else set_label_if_changed(transport_status_label,"Request busy. Please retry.");}
static void update_route_diagram(void){
 if(!actions.transport_line_map_status)return;
 actions.transport_line_map_status(&line_map);
 if(route_revision==line_map.revision)return;
 route_revision=line_map.revision;
 bool loading=line_map.state==LOOKUP_LOADING||line_map.connections_loading;
 if(loading)lv_obj_remove_flag(route_spinner,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(route_spinner,LV_OBJ_FLAG_HIDDEN);
 if(line_map.state==LOOKUP_LOADING)lv_obj_remove_flag(route_loading,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(route_loading,LV_OBJ_FLAG_HIDDEN);
 if(line_map.state==LOOKUP_FAILED||line_map.connections_failed)lv_obj_remove_flag(route_retry_button,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(route_retry_button,LV_OBJ_FLAG_HIDDEN);
 bool keep_selection=!lv_obj_has_state(route_picker,LV_STATE_DISABLED)&&lv_dropdown_get_option_count(route_picker)==line_map.route_count;unsigned previous_branch=lv_dropdown_get_selected(route_picker);
 lv_dropdown_clear_options(route_picker);
 if(line_map.state!=LOOKUP_READY||!line_map.route_count){
  lv_obj_add_state(route_picker,LV_STATE_DISABLED);lv_obj_clean(route_list);lv_dropdown_set_options(route_picker,"Route not available yet");
  set_label_if_changed(transport_status_label,line_map.state==LOOKUP_FAILED?"Route could not load. Tap Retry.":"Loading ordered stations and branches...");return;
 }
 unsigned selected=0,best_score=0;
 for(unsigned i=0;i<line_map.route_count;i++){
  lv_dropdown_add_option(route_picker,line_map.routes[i].name,LV_DROPDOWN_POS_LAST);const station_line_route_t *r=&line_map.routes[i];unsigned score=0;
  if(r->count&&contains_fold(detail_destination,line_map.stops[r->stops[r->count-1]].name))score++;
  if(transport.selected<transport.count)for(unsigned j=0;j<r->count;j++)if(!strcmp(transport.stops[transport.selected].id,line_map.stops[r->stops[j]].id)){score+=2;break;}
  if(score>best_score){selected=i;best_score=score;}
 }
 if(keep_selection&&previous_branch<line_map.route_count)selected=previous_branch;
 lv_dropdown_set_selected(route_picker,selected);lv_obj_remove_state(route_picker,LV_STATE_DISABLED);
 set_label_if_changed(transport_status_label,line_map.connections_loading?"Route ready · loading connecting lines…":line_map.connections_failed?"Route ready · some connections unavailable · tap Retry":line_map.partial?"Some route/connection details unavailable · tap a station to explore":"Stations in travel order · select a direction/branch · tap a station for services");draw_route(NULL);
}
static void create_route_diagram(void){
 transport_map=true;transport_detail_open=false;transport_search_open=false;lv_obj_clean(content);route_revision=UINT32_MAX;
 char title[80];const station_transport_row_t *service=detail_row();snprintf(title,sizeof(title),"%s · Route diagram",service?service->code:"Line");label(content,title,32,14,&station_text_32);route_retry_button=button(content,"Retry",800,4,220,route_retry,NULL);button(content,"Near me",570,4,200,nearby_again,NULL);button(content,"Services",1040,4,200,navigate,(void *)(uintptr_t)VIEW_TRANSPORT);
 route_spinner=lv_spinner_create(content);lv_obj_set_pos(route_spinner,32,72);lv_obj_set_size(route_spinner,34,34);lv_spinner_set_anim_params(route_spinner,900,200);
 transport_status_label=label(content,"Loading route diagram...",82,76,&station_text_18);lv_obj_set_width(transport_status_label,1160);
 route_picker=lv_dropdown_create(content);lv_obj_add_state(route_picker,LV_STATE_DISABLED);lv_obj_set_pos(route_picker,32,118);lv_obj_set_size(route_picker,1216,58);lv_obj_set_style_text_font(route_picker,&station_text_24,0);lv_obj_add_event_cb(route_picker,draw_route,LV_EVENT_VALUE_CHANGED,NULL);
 route_list=box(content,32,194,1216,448,BG);lv_obj_add_flag(route_list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(route_list,LV_DIR_VER);
 route_loading=box(content,260,270,760,180,0x183444);lv_obj_t *spin=lv_spinner_create(route_loading);lv_obj_set_size(spin,64,64);lv_obj_set_pos(spin,40,55);lv_spinner_set_anim_params(spin,900,200);label(route_loading,"Loading route diagram…",140,60,&station_text_32);label(route_loading,"Fetching stations and branches",140,110,&station_text_18);
 update_route_diagram();
}
