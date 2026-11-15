#include "../firmware/components/station_ui/fonts/text_fonts.h"
#include "station_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static time_t test_now=1794729600;
time_t time(time_t *out){if(out)*out=test_now;return test_now;}
static unsigned saves, scans, connections, city_request, weather_requests;
static station_weather_snapshot_t weather;
static void weather_status(station_weather_snapshot_t *s){*s=weather;}
static bool refresh_weather(void){weather_requests++;return true;}
static station_settings_t saved;
static station_wifi_scan_t scan_result;
static station_network_status_t status={.automatic=true,.timezone="UTC",.tz_rule="UTC0"};
static bool scan_wifi(void){scans++;scan_result.state=WIFI_SCAN_RUNNING;scan_result.revision++;return true;}
static void wifi_scan_status(station_wifi_scan_t*s){*s=scan_result;}
static void snapshot(station_network_status_t*s){*s=status;}
static int save_state(void){return 2;}
static bool save(const station_settings_t*s){assert(station_settings_valid(s));saved=*s;saves++;return true;}
static unsigned backlight, brightness_calls;
static bool brightness_failure;
static bool dim(unsigned p){brightness_calls++;backlight=p;return !brightness_failure&&p<=100;}
static const char *expected_password="12345678";
static bool connect_wifi(const char*s,const char*p){assert(!strcmp(s,"Home"));assert(!strcmp(p,expected_password));connections++;status.connecting=true;status.connected=false;return true;}
static bool city(unsigned i){city_request=i;return true;}
static uint8_t *frame;static unsigned frame_count;
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*p){(void)a;frame_count++;frame=p;lv_display_flush_ready(d);}
static void tick(void){lv_tick_inc(100);lv_timer_handler();}
static lv_obj_t *find(lv_obj_t*p,const char*t){for(unsigned i=0;i<lv_obj_get_child_count(p);i++){lv_obj_t*c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class)&&!strcmp(lv_label_get_text(c),t))return c;lv_obj_t*r=find(c,t);if(r)return r;}return NULL;}
static lv_obj_t *find_prefix(lv_obj_t *p,const char *t){if(lv_obj_check_type(p,&lv_label_class)&&!strncmp(lv_label_get_text(p),t,strlen(t)))return p;for(unsigned i=0;i<lv_obj_get_child_count(p);i++){lv_obj_t *r=find_prefix(lv_obj_get_child(p,i),t);if(r)return r;}return NULL;}
static lv_obj_t *kind(lv_obj_t*p,const lv_obj_class_t*k,unsigned *skip){for(unsigned i=0;i<lv_obj_get_child_count(p);i++){lv_obj_t*c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,k)){if(!*skip)return c;--*skip;}lv_obj_t*r=kind(c,k,skip);if(r)return r;}return NULL;}
static lv_obj_t *widget(const lv_obj_class_t*k,unsigned n){return kind(lv_screen_active(),k,&n);}
static void click(const char*t){lv_obj_t*c=find(lv_screen_active(),t);assert(c);lv_obj_t*b=lv_obj_get_parent(c);assert(!lv_obj_has_state(b,LV_STATE_DISABLED));lv_obj_send_event(b,LV_EVENT_CLICKED,NULL);tick();}
static void shot(const char*name){tick();char path[160];snprintf(path,sizeof(path),"build-host/%s.ppm",name);FILE*f=fopen(path,"wb");assert(f&&frame);fprintf(f,"P6\n1280 800\n255\n");for(unsigned i=0;i<1280*800;i++){fputc(frame[i*3+2],f);fputc(frame[i*3+1],f);fputc(frame[i*3],f);}fclose(f);}
static station_transport_snapshot_t transit;
static unsigned transit_refreshes,nearby_resets;
static void nearby_reset(void){nearby_resets++;}
static void transport_snapshot(station_transport_snapshot_t *out){*out=transit;}
static bool transport_select(unsigned index){transit.selected=index;transit.valid=false;transit.row_count=0;transit.state=TRANS_LOADING;return true;}
static bool transport_refresh(void){transit_refreshes++;return true;}
static station_transport_search_t search_results;
static station_transport_line_map_t route_stations;
static unsigned search_calls,search_selected,map_calls,tile_requests,last_zoom,line_selected;
static bool line_select(unsigned i){line_selected=i+1;return true;}
static bool search_station(const char *query){assert(!strcmp(query,"Robinson"));search_calls++;search_results.state=LOOKUP_LOADING;search_results.revision++;return true;}
static void search_status(station_transport_search_t *out){*out=search_results;}
static bool search_select(unsigned i){search_selected=i+1;return true;}
static bool line_map(const char *line){assert(!strcmp(line,"C01268"));map_calls++;return true;}
static void line_status(station_transport_line_map_t *out){*out=route_stations;}
static int first_tile_x,first_tile_y;static unsigned first_tile_zoom;
static unsigned tile_leases;static uint16_t pixels[256*256];
static bool tile_acquire(unsigned z,int x,int y,station_map_tile_t *out){if(!tile_requests){first_tile_x=x;first_tile_y=y;first_tile_zoom=z;}tile_requests++;last_zoom=z;tile_leases++;*out=(station_map_tile_t){.pixels=pixels,.handle=1};return true;}
static void tile_release(unsigned handle){assert(handle==1&&tile_leases);tile_leases--;}
static station_phone_command_t phone_command;
static bool phone_pending;
static char test_phone_url[160]="http://192.168.1.34/#token=0123456789abcdef0123456789abcdef";
static void phone_link(station_phone_link_t *s){s->online=status.connected;snprintf(s->url,sizeof(s->url),"%s",test_phone_url);}
static bool phone_take(station_phone_command_t *c){if(!phone_pending)return false;*c=phone_command;phone_pending=false;return true;}
static bool contains_text(lv_obj_t *obj,const char *text){
 if(lv_obj_check_type(obj,&lv_label_class)&&strstr(lv_label_get_text(obj),text))return true;
 for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)if(contains_text(lv_obj_get_child(obj,i),text))return true;
 return false;
}
int main(int argc,char **argv){
 lv_init();lv_display_t*d=lv_display_create(1280,800);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB888);
 void*buf=malloc(1280*800*3);assert(buf);lv_display_set_buffers(d,buf,NULL,1280*800*3,LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
 station_settings_t initial=station_defaults();
 unsigned boot_level=0;
 if(argc>1){
  setenv("TZ","UTC0",1);tzset();struct tm boot={.tm_year=126,.tm_mon=10,.tm_mday=14,.tm_hour=23};
  initial.night_mode=strcmp(argv[1],"--boot-dim")==0?1:2;
  status.source=strcmp(argv[1],"--boot-unset")==0?TIME_UNSET:TIME_NETWORK;
  if(strcmp(argv[1],"--boot-day")==0)boot.tm_hour=12;
  test_now=mktime(&boot);boot_level=status.source==TIME_UNSET||boot.tm_hour==12?65:initial.night_mode==1?10:0;
 }
 station_ui_start(&initial,(station_ui_actions_t){.transport_search=search_station,.transport_search_status=search_status,.transport_search_select=search_select,.transport_line_stop_select=line_select,.transport_line_map=line_map,.transport_line_map_status=line_status,.map_tile_acquire=tile_acquire,.map_tile_release=tile_release,.transport_nearby_reset=nearby_reset,.transport_status=transport_snapshot,.transport_select=transport_select,.transport_refresh=transport_refresh,.phone_link=phone_link,.phone_take=phone_take,.weather_status=weather_status,.refresh_weather=refresh_weather,.save=save,.save_state=save_state,.brightness=dim,.scan_wifi=scan_wifi,.wifi_scan_status=wifi_scan_status,.connect_wifi=connect_wifi,.set_city=city,.network_status=snapshot});
 if(argc>1){assert(brightness_calls==1&&backlight==boot_level);tick();assert(brightness_calls==1&&backlight==boot_level);printf("PASS: %s starts directly at %u%% with no normal-brightness flash\n",argv[1],boot_level);lv_deinit();free(buf);return 0;}
 tick();
 const lv_font_t *text_fonts[]={&station_text_18,&station_text_24,&station_text_32,&station_text_48};
 const uint32_t accents[]={0xe2,0xe9,0xe8,0xea,0xeb,0xe0,0xe7,0xee,0xef,0xf4,0xf9,0xfb,0xfc,0xff,0xc9,0x153,0x152,0xdf,0x2019};
 for(unsigned f=0;f<4;f++)for(unsigned c=0;c<sizeof(accents)/sizeof(accents[0]);c++){lv_font_glyph_dsc_t g;assert(lv_font_get_glyph_dsc(text_fonts[f],&g,accents[c],0));assert(g.box_w>0);}
 status.connected=true;
 transit.generation=status.location_generation;transit.state=TRANS_READY;transit.count=2;transit.selected=0;transit.valid=true;transit.fetched_at=time(NULL);transit.row_count=1;
 snprintf(transit.stops[0].name,sizeof(transit.stops[0].name),"Mairie – Châtenay");transit.stops[0].metres=80;
 snprintf(transit.rows[0].line,20,"C01268");snprintf(transit.rows[0].code,20,"194");snprintf(transit.rows[0].destination,128,"Châtillon – Montrouge");transit.rows[0].at[0]=time(NULL)+300;transit.rows[0].at[1]=time(NULL)+900;transit.rows[0].estimated=true;transit.rows[0].predicted[0]=transit.rows[0].predicted[1]=true;
 transit.rows[0].arrival[0]=transit.rows[0].arrival[1]=true;transit.rows[0].delay_minutes[0]=3;
 snprintf(transit.rows[0].pickup,96,"Mairie");snprintf(transit.rows[0].platform,24,"2");
 click("Transport");assert(nearby_resets==1);assert(find(lv_screen_active(),"194"));assert(find(lv_screen_active(),"Châtillon – Montrouge"));assert(find(lv_screen_active(),"Traffic status unavailable"));assert(contains_text(lv_screen_active(),"Arrives "));assert(!contains_text(lv_screen_active(),"Next:"));assert(contains_text(lv_screen_active(),"Delayed +3 min"));assert(contains_text(lv_screen_active(),"Platform 2"));shot("transport-080");

 transit.row_count=6;for(unsigned i=1;i<6;i++){transit.rows[i]=transit.rows[0];snprintf(transit.rows[i].code,20,"Line %u",i);}
 for(unsigned i=0;i<12;i++)tick();
 lv_obj_t *first_card=lv_obj_get_parent(find(lv_screen_active(),"194")),*service_list=lv_obj_get_parent(first_card);
 lv_obj_scroll_to_y(service_list,300,LV_ANIM_OFF);tick();assert(lv_obj_get_scroll_y(service_list)>200);
 transit.selected=1;transit.selection_generation++;transit.valid=false;transit.row_count=0;for(unsigned i=0;i<12;i++)tick();
 transit.valid=true;transit.row_count=1;for(unsigned i=0;i<12;i++)tick();
 printf("Station switch scroll=%d, first-row-visible=%d\n",(int)lv_obj_get_scroll_y(service_list),(int)lv_obj_is_visible(first_card));
 shot("station-switch-scroll");assert(lv_obj_is_visible(first_card));assert(lv_obj_get_scroll_y(service_list)==0);
 transit.row_count=6;for(unsigned i=0;i<12;i++)tick();lv_obj_scroll_to_y(service_list,300,LV_ANIM_OFF);tick();
 transit.revision++;for(unsigned i=0;i<12;i++)tick();assert(lv_obj_get_scroll_y(service_list)>200);
 transit.row_count=1;for(unsigned i=0;i<12;i++)tick();assert(lv_obj_is_visible(first_card)&&lv_obj_get_scroll_y(service_list)==0);
 transit.selected=0;
 click("194");assert(contains_text(lv_screen_active(),"Boarding point: Mairie"));assert(find(lv_screen_active(),"Route diagram"));shot("transport-detail-080");
 route_stations.state=LOOKUP_READY;route_stations.revision=1;route_stations.count=2;route_stations.route_count=1;route_stations.color=0x5091cb;route_stations.routes[0].count=2;route_stations.routes[0].stops[0]=0;route_stations.routes[0].stops[1]=1;strcpy(route_stations.routes[0].name,"Robinson > Sceaux");strcpy(route_stations.stops[1].name,"Sceaux");strcpy(route_stations.stops[0].name,"Robinson");route_stations.stops[0].coordinates=true;route_stations.stops[0].latitude=48.78;route_stations.stops[0].longitude=2.28;
 route_stations.state=LOOKUP_LOADING;click("Route diagram");assert(contains_text(lv_screen_active(),"Loading route diagram"));unsigned loading_frames=frame_count;for(unsigned i=0;i<12;i++)tick();assert(frame_count>loading_frames);shot("route-loading-089");route_stations.state=LOOKUP_FAILED;route_stations.revision++;for(unsigned i=0;i<12;i++)tick();assert(find(lv_screen_active(),"Retry"));route_stations.state=LOOKUP_READY;route_stations.connections_loading=true;route_stations.revision++;for(unsigned i=0;i<12;i++)tick();assert(contains_text(lv_screen_active(),"loading connecting lines"));route_stations.connections_loading=false;route_stations.stops[1].connections_known=true;strcpy(route_stations.stops[1].connections,"Métro 4 · Bus 38 · Tram T3");route_stations.revision++;for(unsigned i=0;i<12;i++)tick();assert(contains_text(lv_screen_active(),"Métro 4"));assert(map_calls==1&&contains_text(lv_screen_active(),"Robinson"));assert(contains_text(lv_screen_active(),"Sceaux"));shot("route-diagram-087");click("Sceaux");assert(line_selected==2);assert(tile_leases==0);search_results.connections=true;search_results.state=LOOKUP_LOADING;search_results.revision++;for(unsigned i=0;i<12;i++)tick();assert(contains_text(lv_screen_active(),"Finding connecting services"));search_results.state=LOOKUP_READY;search_results.count=12;search_results.revision++;for(unsigned n=0;n<12;n++){snprintf(search_results.stops[n].name,96,"Boarding point %u",n);strcpy(search_results.stops[n].mode,n==0?"RER":n==1?"Métro":n==2?"Tram":"Bus");}for(unsigned i=0;i<12;i++)tick();assert(contains_text(lv_screen_active(),"Boarding point 11"));shot("connections-089");
 click("Services");click("Search");lv_obj_t *station_field=widget(&lv_textarea_class,0),*station_keyboard=widget(&lv_keyboard_class,0);assert(lv_obj_has_flag(station_keyboard,LV_OBJ_FLAG_HIDDEN));lv_obj_send_event(station_field,LV_EVENT_CLICKED,NULL);assert(!lv_obj_has_flag(station_keyboard,LV_OBJ_FLAG_HIDDEN));lv_textarea_set_text(station_field,"Robinson");click("Search stations");assert(search_calls==1&&lv_obj_has_flag(station_keyboard,LV_OBJ_FLAG_HIDDEN));
 search_results.state=LOOKUP_READY;search_results.count=1;search_results.revision++;strcpy(search_results.stops[0].name,"Robinson");strcpy(search_results.stops[0].mode,"RER");for(unsigned i=0;i<12;i++)tick();assert(find(lv_screen_active(),"RER · Robinson"));shot("transport-search-080");click("RER · Robinson");assert(search_selected==1);

 status.location_valid=true;status.latitude=48.766968;status.longitude=2.280719;
 transit.stops[0].coordinates=true;transit.stops[0].latitude=48.777;transit.stops[0].longitude=2.276;
 snprintf(transit.stops[0].mode,24,"RER");snprintf(transit.stops[1].name,96,"Second station");snprintf(transit.stops[1].mode,24,"Métro");transit.stops[1].coordinates=true;transit.stops[1].latitude=48.761;transit.stops[1].longitude=2.291;
 for(unsigned i=0;i<12;i++)tick();click("Map");assert(find(lv_screen_active(),"My location"));assert(contains_text(lv_screen_active(),"Second station"));shot("transport-map-070");for(unsigned i=0;i<20;i++)tick();unsigned map_idle=frame_count;for(unsigned i=0;i<20;i++)tick();fprintf(stderr,"Idle street map redraws: %u\n",frame_count-map_idle);assert(frame_count-map_idle<10);
 click("Services");transit.selected=1;transit.stops[1].latitude=49.009;transit.stops[1].longitude=2.56;tile_requests=0;click("Map");
 double center_x,center_y;station_map_project(49.009,2.56,15,&center_x,&center_y);assert(first_tile_zoom==15&&first_tile_x==(int)floor(center_x/256)&&first_tile_y==(int)floor(center_y/256));
 unsigned resets=nearby_resets;click("Near me");assert(nearby_resets==resets+1);click("Map");click("2");assert(transit.selected==1);assert(!find(lv_screen_active(),"My location"));
 transit.selected=0;transit.valid=true;transit.row_count=1;transit.state=TRANS_READY;click("Transport");

 click("Refresh");assert(transit_refreshes==1);
 click(LV_SYMBOL_RIGHT);assert(transit.selected==1);assert(!find(lv_screen_active(),"194"));assert(contains_text(lv_screen_active(),"Updating nearby transport…"));
 transit.valid=true;transit.row_count=1;transit.state=TRANS_READY;transit.fetched_at=time(NULL)-600;for(unsigned i=0;i<10;i++)tick();assert(find(lv_screen_active(),"Out of date"));
 transit.generation=status.location_generation+1;for(unsigned i=0;i<10;i++)tick();assert(!find(lv_screen_active(),"194"));
 click("Clock");
 assert(!find(lv_screen_active(),"Waiting for internet time / UTC"));
 click("Settings");assert(!widget(&lv_textarea_class,0));assert(!find(lv_screen_active(),"Set time"));
 click("Teal");assert(saved.theme==1);click("Blue");assert(saved.theme==2);
 click("Other colours");lv_obj_t*r=widget(&lv_slider_class,0);assert(r);lv_slider_set_value(r,220,LV_ANIM_OFF);lv_obj_send_event(r,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_send_event(r,LV_EVENT_RELEASED,NULL);assert(saved.theme==3 && saved.custom_color>>16==220);click("Back");shot("settings-022");
 click("Your profile");lv_obj_t*name=widget(&lv_textarea_class,0);lv_textarea_set_text(name,"Simon & Alex");click("Save name");assert(!strcmp(saved.user_name,"Simon & Alex"));assert(find(lv_screen_active(),"Welcome Simon & Alex"));click("Back");
 strcpy(status.timezone,"Europe/Paris");strcpy(status.location_name,"Châtenay-Malabry");status.location_valid=true;status.latitude=48.766968;status.longitude=2.280719;click("Home location");assert(contains_text(lv_screen_active(),"Saved location: Châtenay-Malabry"));lv_obj_t*search=widget(&lv_textarea_class,0);lv_textarea_set_text(search,"Paris");lv_obj_t*picker=widget(&lv_dropdown_class,0);assert(lv_dropdown_get_option_count(picker)==1);click("Use city");assert(!strcmp(station_cities[city_request].zone,"Europe/Paris"));shot("city-022");click("Back");
 status.connecting=true;click("Wi-Fi");assert(scans==0);assert(!lv_obj_has_flag(widget(&lv_spinner_class,0),LV_OBJ_FLAG_HIDDEN));
 char waiting[128];snprintf(waiting,sizeof(waiting),"%s",lv_dropdown_get_options(widget(&lv_dropdown_class,0)));for(unsigned i=0;i<5;i++)tick();assert(strcmp(waiting,lv_dropdown_get_options(widget(&lv_dropdown_class,0))));
 status.connecting=false;status.connected=false;tick();assert(scans==1);picker=widget(&lv_dropdown_class,0);assert(lv_obj_has_state(picker,LV_STATE_DISABLED));
 scan_result.state=WIFI_SCAN_READY;scan_result.count=2;scan_result.revision++;scan_result.networks[0]=(station_wifi_ap_t){.ssid="Home",.rssi=-35,.secured=true};scan_result.networks[1]=(station_wifi_ap_t){.ssid="Guest\nnetwork",.rssi=-65,.secured=false};tick();assert(lv_dropdown_get_option_count(picker)==2);assert(!lv_obj_has_state(picker,LV_STATE_DISABLED));lv_dropdown_open(picker);assert(lv_dropdown_is_open(picker));lv_dropdown_close(picker);
 lv_obj_t*password=widget(&lv_textarea_class,0);assert(lv_textarea_get_password_mode(password));lv_obj_t*kb=widget(&lv_keyboard_class,0);assert(lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));
 lv_textarea_set_text(password,"Example123");click("Show");assert(!lv_textarea_get_password_mode(password));assert(!strcmp(lv_textarea_get_text(password),"Example123"));click("Hide");assert(lv_textarea_get_password_mode(password));
 lv_obj_send_event(password,LV_EVENT_CLICKED,NULL);assert(!lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));
 for(unsigned i=0;i<20;i++)tick();unsigned idle_start=frame_count;for(unsigned i=0;i<20;i++)tick();fprintf(stderr,"Keyboard idle frames over 2 seconds: %u\n",frame_count-idle_start);assert(frame_count-idle_start<10);
 lv_obj_update_layout(kb);assert(lv_obj_get_height(kb)==310);assert(lv_obj_get_style_text_font(kb,LV_PART_ITEMS)->line_height>=32);
 lv_textarea_set_text(password,"");lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);lv_buttonmatrix_set_selected_button(kb,1);lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(password),"q"));
 lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_UPPER);lv_buttonmatrix_set_selected_button(kb,1);lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(password),"qQ"));
 lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_SPECIAL);lv_buttonmatrix_set_selected_button(kb,0);lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(password),"qQ1"));
 lv_buttonmatrix_set_selected_button(kb,10);lv_obj_send_event(kb,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(password),"qQ"));
 lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);shot("keyboard-086");
 lv_obj_send_event(kb,LV_EVENT_CANCEL,NULL);assert(lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));
 lv_textarea_set_text(password,"12345678");click("Connect");assert(connections==1);assert(find(lv_screen_active(),"Connecting..."));assert(lv_obj_has_state(picker,LV_STATE_DISABLED));assert(!lv_obj_has_flag(widget(&lv_spinner_class,1),LV_OBJ_FLAG_HIDDEN));shot("wifi-connecting-022");
 status.connecting=false;status.connected=true;tick();assert(find(lv_screen_active(),"Connected to Wi-Fi"));assert(!lv_obj_has_state(picker,LV_STATE_DISABLED));assert(lv_obj_has_flag(widget(&lv_spinner_class,1),LV_OBJ_FLAG_HIDDEN));shot("wifi-connected-022");
 scan_result.networks[0].saved=true;scan_result.revision++;tick();lv_obj_send_event(picker,LV_EVENT_VALUE_CHANGED,NULL);assert(!lv_textarea_get_text(password)[0]);assert(lv_textarea_get_password_mode(password));expected_password="";click("Reconnect");assert(connections==2);assert(lv_textarea_get_password_mode(password));status.connected=true;status.connecting=false;tick();
 status.connected=false;status.connecting=false;snprintf(status.message,sizeof(status.message),"Connection timed out. Check the password and try again.");tick();assert(find(lv_screen_active(),status.message));assert(!lv_obj_has_state(lv_obj_get_parent(find(lv_screen_active(),"Connect")),LV_STATE_DISABLED));
 click("Rescan");scan_result.state=WIFI_SCAN_FAILED;scan_result.revision++;snprintf(scan_result.message,sizeof(scan_result.message),"Scan failed. Tap Rescan to retry.");tick();assert(!lv_obj_has_state(picker,LV_STATE_DISABLED));
 click("Rescan");scan_result.state=WIFI_SCAN_READY;scan_result.count=0;scan_result.revision++;tick();assert(lv_obj_has_state(picker,LV_STATE_DISABLED));
 click("Back");lv_obj_t*fonts=widget(&lv_dropdown_class,0);lv_dropdown_set_selected(fonts,1);lv_obj_send_event(fonts,LV_EVENT_VALUE_CHANGED,NULL);assert(saved.clock_font==1);
 status.source=TIME_NETWORK;status.connected=true;click("Clock");shot("clock-modern-022");assert(find(lv_screen_active(),LV_SYMBOL_WIFI));
 click("Settings");fonts=widget(&lv_dropdown_class,0);lv_dropdown_set_selected(fonts,0);lv_obj_send_event(fonts,LV_EVENT_VALUE_CHANGED,NULL);click("Clock");shot("clock-digital-022");
 /* At least one inactive segment must be hidden, not painted grey. */
 lv_obj_t*content=lv_obj_get_child(lv_screen_active(),0);unsigned hidden=0;for(unsigned i=0;i<lv_obj_get_child_count(content);i++)if(lv_obj_has_flag(lv_obj_get_child(content,i),LV_OBJ_FLAG_HIDDEN))hidden++;assert(hidden>0);
 click("Weather");assert(find(lv_screen_active(),LV_SYMBOL_WIFI));click("Transport");assert(find(lv_screen_active(),LV_SYMBOL_WIFI));status.connected=false;tick();assert(find(lv_screen_active(),LV_SYMBOL_WIFI "  Offline"));assert(saves>=5);
 status.connected=true;status.location_valid=true;status.location_generation=1;strcpy(status.location_name,"Paris / FR");
 FILE*fixture=fopen("tests/fixtures/weather.json","rb");assert(fixture);char body[24576];size_t bytes=fread(body,1,sizeof(body)-1,fixture);fclose(fixture);body[bytes]=0;
 assert(station_weather_parse(body,1794499201,&weather.data));weather.data.fetched_at=time(NULL);weather.data.observed_at=time(NULL)-300;
 for(unsigned i=0;i<weather.data.hour_count;i++)weather.data.hours[i].at=(time(NULL)/3600+i+1)*3600;
 weather.location_generation=1;strcpy(weather.location,"Paris / FR");weather.state=WEATHER_READY;
 click("Weather");assert(find(lv_screen_active(),"15 °C"));assert(find(lv_screen_active(),"74%"));shot("weather-ready-040");
 click("Refresh");assert(weather_requests==1);
 transit.generation=status.location_generation;transit.valid=true;transit.state=TRANS_READY;transit.fetched_at=time(NULL);transit.row_count=2;
 transit.rows[0].at[0]=time(NULL)+240;transit.rows[0].at[1]=time(NULL)+960;transit.rows[0].delay_minutes[0]=0;transit.rows[0].cancelled[0]=false;
 transit.rows[1]=transit.rows[0];strcpy(transit.rows[1].destination,"Other direction");transit.rows[1].at[0]=time(NULL)+420;
 click("Clock");assert(find(lv_screen_active(),"15°"));assert(find(lv_screen_active(),"4 min"));assert(find(lv_screen_active(),"7 min"));assert(find(lv_screen_active(),"Other direction"));shot("clock-weather-040");
 transit.rows[1].cancelled[0]=true;for(unsigned i=0;i<12;i++)tick();assert(find(lv_screen_active(),"Cancelled"));transit.rows[1].cancelled[0]=false;
 weather.state=WEATHER_LOADING;transit.state=TRANS_LOADING;for(unsigned i=0;i<12;i++)tick();unsigned animated=frame_count;for(unsigned i=0;i<12;i++)tick();assert(frame_count>animated);assert(find(lv_screen_active(),"4 min"));assert(contains_text(lv_screen_active(),"Updating"));
 weather.state=WEATHER_READY;transit.state=TRANS_READY;
 click("Settings");fonts=widget(&lv_dropdown_class,0);lv_dropdown_set_selected(fonts,1);lv_obj_send_event(fonts,LV_EVENT_VALUE_CHANGED,NULL);click("Clock");for(unsigned i=0;i<14;i++)tick();shot("clock-weather-modern-040");
 click("Settings");fonts=widget(&lv_dropdown_class,0);assert(strstr(lv_dropdown_get_options(fonts),"Light"));lv_dropdown_set_selected(fonts,3);lv_obj_send_event(fonts,LV_EVENT_VALUE_CHANGED,NULL);assert(saved.clock_font==3);
 weather.data.days[0].at=(time(NULL)/86400)*86400;weather.data.days[0].high=17;weather.data.days[0].low=10;
 phone_command=(station_phone_command_t){.change_settings=true,.settings=saved,.change_view=true,.view=VIEW_CLOCK};phone_command.settings.theme=1;phone_pending=true;tick();for(unsigned i=0;i<14;i++)tick();
 assert(find(lv_screen_active(),"4 min")&&find(lv_screen_active(),"7 min"));assert(find(lv_screen_active(),"Other direction"));transit.selected=0;strcpy(transit.stops[0].name,"Robinson");strcpy(status.location_name,"Châtenay-Malabry");
 for(unsigned i=0;i<2;i++){strcpy(transit.rows[i].pickup,"Robinson");strcpy(transit.rows[i].code,"B");}
 strcpy(transit.rows[0].destination,"Mitry–Claye");strcpy(transit.rows[1].destination,"Aéroport Charles de Gaulle 2");for(unsigned i=0;i<14;i++)tick();lv_obj_t *weather_card=lv_obj_get_parent(find(lv_screen_active(),"LOCAL WEATHER"));lv_obj_t *transport_card=lv_obj_get_parent(find(lv_screen_active(),"Robinson"));
 assert(lv_obj_get_width(weather_card)==592&&lv_obj_get_width(transport_card)==592);assert(lv_obj_get_height(weather_card)==328&&lv_obj_get_height(transport_card)==328);assert(lv_obj_get_y(weather_card)==lv_obj_get_y(transport_card));
 lv_obj_t *long_destination=find_prefix(lv_screen_active(),"Aéroport");assert(long_destination&&lv_obj_get_height(long_destination)==station_text_24.line_height);
 lv_obj_t *clock_nav=lv_obj_get_parent(find(lv_screen_active(),"Clock"));assert(lv_obj_get_x(clock_nav)==320&&lv_obj_get_y(clock_nav)==718&&lv_obj_get_width(clock_nav)==148&&lv_obj_get_height(clock_nav)==52);
 transit.row_count=3;transit.rows[2]=transit.rows[0];strcpy(transit.rows[2].destination,"Gare du Nord");transit.rows[2].at[0]=time(NULL)+600;transit.rows[2].delay_minutes[0]=3;for(unsigned i=0;i<12;i++)tick();
 assert(find(lv_screen_active(),"Gare du Nord"));assert(contains_text(lv_screen_active(),"+3 min late"));assert(contains_text(lv_screen_active(),"Rain peak (6h)"));assert(contains_text(lv_screen_active(),"Wind:"));
 shot("clock-content-0814");
 /* Clock overview must show multiple stations even when the selected stop has one direction. */
 transit.summary.count=3;
 for(unsigned i=0;i<3;i++){transit.summary.rows[i]=transit.rows[i];transit.summary.fetched[i]=time(NULL);}
 strcpy(transit.summary.stops[0],"Robinson");strcpy(transit.summary.stops[1],"La Croix de Berny");strcpy(transit.summary.stops[2],"Centre-ville");
 strcpy(transit.summary.rows[2].code,"194");strcpy(transit.summary.rows[2].destination,"Porte d’Orléans");
 transit.row_count=1;for(unsigned i=0;i<12;i++)tick();
 assert(find(lv_screen_active(),"NEARBY TRANSPORT"));assert(find(lv_screen_active(),"194"));assert(contains_text(lv_screen_active(),"La Croix de Berny"));assert(contains_text(lv_screen_active(),"Centre-ville"));assert(contains_text(lv_screen_active(),"Humidity"));
 shot("clock-content-0815");
 transit.summary.fetched[1]=time(NULL)-400;for(unsigned i=0;i<12;i++)tick();assert(find(lv_screen_active(),"Out of date"));
 transit.summary.fetched[1]=time(NULL);transit.summary.loading=true;for(unsigned i=0;i<12;i++)tick();assert(contains_text(lv_screen_active(),"Updating"));transit.summary.loading=false;


 weather.data.temperature=NAN;weather.data.humidity=NAN;click("Weather");assert(find(lv_screen_active(),"-- °C"));assert(find(lv_screen_active(),"--%"));
 status.connected=false;weather.state=WEATHER_WIFI;for(unsigned i=0;i<10;i++)tick();shot("weather-offline-040");
 status.location_generation=2;strcpy(status.location_name,"London / GB");click("Clock");assert(find(lv_screen_active(),"--°"));
 status.location_valid=false;click("Weather");shot("weather-no-location-040");
 status.connected=true;click("Settings");click("iPhone setup");
 assert(widget(&lv_qrcode_class,0)&&!lv_obj_has_flag(widget(&lv_qrcode_class,0),LV_OBJ_FLAG_HIDDEN));shot("phone-setup-050");
 lv_obj_t *qr=widget(&lv_qrcode_class,0);lv_qrcode_set_size(qr,1);strcpy(test_phone_url,"http://192.168.1.35/#token=0123456789abcdef0123456789abcdef");for(unsigned i=0;i<12;i++)tick();assert(lv_obj_has_flag(qr,LV_OBJ_FLAG_HIDDEN));lv_qrcode_set_size(qr,290);for(unsigned i=0;i<12;i++)tick();assert(!lv_obj_has_flag(qr,LV_OBJ_FLAG_HIDDEN));
 status.connected=false;for(unsigned i=0;i<12;i++)tick();assert(lv_obj_has_flag(widget(&lv_qrcode_class,0),LV_OBJ_FLAG_HIDDEN));
 status.connected=true;strcpy(test_phone_url,"http://172.20.10.2/#token=0123456789abcdef0123456789abcdef");for(unsigned i=0;i<12;i++)tick();assert(!lv_obj_has_flag(widget(&lv_qrcode_class,0),LV_OBJ_FLAG_HIDDEN));
 click("Hotspot instructions");assert(find(lv_screen_active(),"1. iPhone: Settings > Personal Hotspot"));shot("hotspot-help-050");
 phone_command=(station_phone_command_t){.change_settings=true,.settings=station_defaults(),.change_view=true,.view=VIEW_CLOCK};
 strcpy(phone_command.settings.user_name,"Phone user");phone_command.settings.brightness=37;phone_pending=true;tick();
 assert(saved.brightness==37&&!strcmp(saved.user_name,"Phone user"));assert(find(lv_screen_active(),"Welcome Phone user"));
 shot("clock-phone-050");
 phone_command=(station_phone_command_t){.change_settings=true,.settings=station_defaults(),.change_view=true,.view=VIEW_CLOCK};phone_command.settings.rotate=true;phone_command.settings.dwell_seconds=20;phone_pending=true;tick();
 for(unsigned i=0;i<200;i++)tick();assert(contains_text(lv_screen_active(),"Weather"));
 for(unsigned i=0;i<200;i++)tick();assert(find(lv_screen_active(),"Search"));
 for(unsigned i=0;i<200;i++)tick();assert(find(lv_screen_active(),"LOCAL WEATHER"));
 for(unsigned v=0;v<4;v++){
  phone_command=(station_phone_command_t){.change_view=true,.view=v};phone_pending=true;tick();lv_obj_update_layout(lv_screen_active());
  const char *names[]={"Clock","Weather","Transport","Settings"};
  for(unsigned n=0;n<4;n++){
   lv_obj_t *button=NULL;
   for(unsigned j=0;j<lv_obj_get_child_count(lv_screen_active());j++){lv_obj_t *candidate=lv_obj_get_child(lv_screen_active(),j);if(lv_obj_check_type(candidate,&lv_button_class)&&find(candidate,names[n])){button=candidate;break;}}
   assert(button&&lv_obj_get_x(button)==320+(int)n*160&&lv_obj_get_y(button)==718&&lv_obj_get_width(button)==148&&lv_obj_get_height(button)==52);
  }
 }
 /* Compare actual rasterised whitespace, not label boxes, for every style. */
 const int hours[]={0,11,12,23},minutes[]={0,11,34,59};status.source=TIME_NETWORK;
 for(unsigned style=0;style<10;style++)for(unsigned t=0;t<4;t++){
  struct tm civil;localtime_r(&test_now,&civil);civil.tm_hour=hours[t];civil.tm_min=minutes[t];test_now=mktime(&civil);
  phone_command=(station_phone_command_t){.change_settings=true,.settings=station_defaults(),.change_view=true,.view=VIEW_CLOCK};phone_command.settings.clock_font=style;phone_command.settings.theme=1;phone_pending=true;
  for(unsigned n=0;n<15;n++)tick();
  int time_bottom=-1,date_top=999,date_bottom=-1;
  for(int y=64;y<350;y++)for(int x=200;x<1080;x++){unsigned at=(y*1280+x)*3;int b=frame[at],g=frame[at+1],r=frame[at+2];
   if(y<280&&g>100&&g>r+30&&b>r+30)time_bottom=y;
   if(y>=270&&r>150&&g>150&&b>150){if(y<date_top)date_top=y;date_bottom=y;}
  }
  int above=date_top-time_bottom-1,below=350-date_bottom-1;
  printf("Clock style %u, %02d:%02d: date whitespace above=%d below=%d\n",style,hours[t],minutes[t],above,below);
  assert(time_bottom>0&&date_bottom>date_top&&above>0&&abs(above-below)<=2);
  if(t==2){char capture[48];snprintf(capture,sizeof(capture),"clock-spacing-style-%u",style);shot(capture);}
 }
 /* Night mode is applied by the same LVGL tick as the real device. */
 initial=station_defaults();initial.rotate=false;initial.night_mode=2;initial.night_start=1380;initial.night_end=420;
 setenv("TZ","UTC0",1);tzset();test_now=1794697200; /* 2026-11-14 23:00 UTC, replaced below explicitly */
 struct tm night={.tm_year=126,.tm_mon=10,.tm_mday=14,.tm_hour=23};test_now=mktime(&night);
 status.source=TIME_NETWORK;status.connected=true;
 phone_command=(station_phone_command_t){.change_settings=true,.settings=initial,.change_view=true,.view=VIEW_CLOCK};phone_pending=true;tick();
 assert(backlight==65);
 for(unsigned i=0;i<610;i++)tick();assert(backlight==0);
 /* Model a driver that changes hardware but returns an error. */
 assert(station_ui_touch(true));assert(station_ui_touch(false));
 brightness_failure=true;for(unsigned i=0;i<600;i++)tick();assert(backlight==0);
 unsigned failed_calls=brightness_calls;for(unsigned i=0;i<20;i++)tick();assert(brightness_calls==failed_calls);
 assert(station_ui_touch(true));assert(station_ui_touch(false));
 assert(station_ui_touch(true));assert(station_ui_touch(false)); /* unknown state: no click-through */
 brightness_failure=false;for(unsigned i=0;i<55;i++)tick();assert(backlight==65);
 assert(!station_ui_touch(true));assert(!station_ui_touch(false));
 for(unsigned i=0;i<610;i++)tick();assert(backlight==0);
 unsigned sleeping_beat=station_ui_heartbeat();for(unsigned i=0;i<20;i++)tick();assert(station_ui_heartbeat()>sleeping_beat);
 assert(station_ui_touch(true));assert(backlight==65);assert(station_ui_touch(true));assert(station_ui_touch(false));assert(!station_ui_touch(false));
 for(unsigned i=0;i<610;i++)tick();assert(backlight==0);
 test_now+=8*3600;for(unsigned i=0;i<10;i++)tick();assert(backlight==65);
 status.source=TIME_UNSET;test_now-=8*3600;tick();assert(backlight==65);
 click("Settings");click("Power & night mode");shot("power-night-mode");click("Save night mode");assert(saved.night_mode==2);
 status.connected=false;transit.generation=status.location_generation;transit.valid=true;transit.row_count=1;transit.fetched_at=test_now;transit.state=TRANS_READY;
 click("Transport");assert(contains_text(lv_screen_active(),"Offline · saved"));assert(!contains_text(lv_screen_active(),"Arrives "));
 puts("PASS: scheduled screen-off, first-touch consumption, timed wake, trusted-time fallback and offline departures");
 puts("PASS: equal visible date spacing for all ten styles at 00:00, 11:11, 12:34 and 23:59");
 puts("PASS: clock two-service layout, cancellation, updating animation with cached values, and 20-second automatic rotation");
 puts("PASS: phone QR online/offline/network changes, hotspot guide and queued remote settings");
 puts("PASS: live-weather rendering, missing values, refresh, offline cache and old-location suppression");
 puts("PASS: custom colours/fonts/profile, city search, Wi-Fi progress/success/scans, persistent icons, hidden clock bars");lv_deinit();free(buf);
}
