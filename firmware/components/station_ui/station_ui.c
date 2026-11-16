#include "transport_memory.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include "fonts/text_fonts.h"
#include "station_ui.h"
#include "weather_visuals.h"
#include "src/misc/lv_text_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

LV_FONT_DECLARE(station_clock_font);
LV_FONT_DECLARE(station_bold_clock_font);
LV_FONT_DECLARE(station_hairline_clock_font);
LV_FONT_DECLARE(station_condensed_clock_font);
LV_FONT_DECLARE(station_wide_clock_font);
LV_FONT_DECLARE(station_light_clock_font);

#define BG 0x080c12
#define CARD 0x131c28
#define MUTED 0x96a6ba
#define ACCENT 0x66dfc4
static station_settings_t settings;
static station_ui_actions_t actions;
static station_network_status_t network;
static uint64_t awake_until_ms;
static unsigned applied_brightness=UINT32_MAX, requested_brightness=UINT32_MAX;
static bool brightness_failed;
static uint64_t brightness_retry_ms;
static bool consume_wake_touch;
static lv_obj_t *night_level_text;
static lv_obj_t *night_mode_picker, *night_level, *night_start_hour, *night_start_minute, *night_end_hour, *night_end_minute;
static void update_power(void);

static station_view_t view;
typedef enum { PAGE_DISPLAY, PAGE_WIFI, PAGE_HOME, PAGE_PROFILE, PAGE_COLOR, PAGE_PHONE, PAGE_HOTSPOT, PAGE_POWER, PAGE_UPDATES } page_t;
static page_t page;
static lv_obj_t *content, *date_label, *clock_text, *wifi_icon, *welcome, *settings_status, *header_clock;
static lv_obj_t *phone_qr, *phone_caption, *phone_wait, *tap_notice;
static uint64_t tap_notice_until;
static char phone_url[160];
static lv_obj_t *segments[4][7], *nav[4], *theme_buttons[4], *theme_preview;
static lv_obj_t *ssid_picker, *password_input, *network_keyboard, *network_label, *password_caption;
static lv_obj_t *scan_label, *rescan_button, *connect_button, *connect_caption, *busy_indicator;
static station_weather_snapshot_t weather;
static STATION_TRANSPORT_DATA station_transport_snapshot_t transport;
static lv_obj_t *transport_title,*transport_status_label,*transport_alert_label,*transport_refresh_button;
static lv_obj_t *transport_lines[TRANSPORT_ROWS],*transport_destinations[TRANSPORT_ROWS],*transport_times[TRANSPORT_ROWS];
static lv_obj_t *transport_previous,*transport_next,*transport_footer;
static lv_obj_t *transport_pickups[TRANSPORT_ROWS],*transport_cards[TRANSPORT_ROWS];
static lv_obj_t *clock_departure[3],*clock_badge[3],*clock_pickup[3],*clock_destination[3],*clock_wait[3],*clock_next[3];
static lv_obj_t *clock_dots[2],*clock_rain,*clock_wind;
static int clock_segment_y[4][7];
#define CLOCK_INK_BOTTOM 199
#define CLOCK_WIDGET_TOP 286
static lv_obj_t *clock_weather_busy,*clock_transport_busy;
static bool transport_map;
static bool transport_search_open,transport_detail_open,map_line_mode;
static STATION_TRANSPORT_DATA station_transport_search_t station_search;
static STATION_TRANSPORT_DATA station_transport_line_map_t line_map;
static lv_obj_t *search_input,*search_status,*search_buttons[TRANSPORT_SEARCH_STOPS],*search_names[TRANSPORT_SEARCH_STOPS];
static unsigned search_revision;
static char detail_line[20],detail_destination[128],detail_point[64];
static lv_obj_t *detail_times,*detail_pickup,*detail_freshness;
static void open_station_search(lv_event_t *event);
static void update_station_search(void);
static void open_service_detail(lv_event_t *event);
static void update_service_detail(const char *alerts,bool stale);
static void create_transport_map(void);
static void open_transport_map(lv_event_t *event);
static void update_transport_map(void);
static void create_route_diagram(void);
static void update_route_diagram(void);

static lv_obj_t *weather_title,*weather_temp,*weather_condition,*weather_feels,*weather_range,*weather_footer;
static lv_obj_t *weather_metrics[6],*weather_graph,*weather_graph_caption,*weather_icon;
static lv_obj_t *day_titles[3],*day_conditions[3],*day_lows[3],*day_highs[3],*day_rain[3],*day_icons[3],*day_bars[3];
static station_graph_visual_t graph_visual;
static station_icon_visual_t icon_visual,day_icon_visuals[3];
static station_range_visual_t day_range_visuals[3];
static lv_obj_t *city_search, *city_picker, *profile_input, *color_sliders[3], *color_preview;
static station_wifi_scan_t visible_scan;
static unsigned displayed_scan_revision, city_indices[512], city_matches;
static uint64_t elapsed_ms, last_change_ms;
static bool scan_pending, scan_when_ready, awaiting_save, awaiting_city;
static uint64_t scan_started_ms;
static lv_obj_t *scan_spinner;
static void show_network(lv_event_t *e);
static unsigned location_completion;
static atomic_uint heartbeat;
static char applied_zone[96];
static const uint32_t colors[]={0xff951f,0x3cdac6,0x91b6ff};
static uint32_t ink(void) { return settings.theme<3 ? colors[settings.theme] : settings.custom_color; }
static station_update_status_t update_snapshot;
static void show_view(station_view_t next);
static void update_weather(void);
static void update_transport(void);
static void refresh_network(void)
{
    actions.network_status(&network);
    const char *rule=network.tz_rule[0]?network.tz_rule:"UTC0";
    if(strcmp(applied_zone,rule)) { setenv("TZ",rule,1); tzset(); snprintf(applied_zone,sizeof(applied_zone),"%s",rule); }
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,const lv_font_t *font)
{
    lv_obj_t *o=lv_label_create(parent); lv_label_set_text(o,text); lv_obj_set_pos(o,x,y);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(0xe8edf5),0); return o;
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,16,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE); return o;
}
static void acknowledge_tap(lv_event_t *event)
{
    lv_obj_t *target=lv_event_get_target_obj(event);
    const char *caption="Selection";
    lv_obj_t *child=lv_obj_get_child(target,0);
    if(child&&lv_obj_check_type(child,&lv_label_class))caption=lv_label_get_text(child);
    if(!tap_notice) {
        tap_notice=label(lv_layer_top(),"",0,0,&station_text_18);
        lv_obj_set_style_bg_color(tap_notice,lv_color_hex(0x29483f),0);
        lv_obj_set_style_bg_opa(tap_notice,LV_OPA_COVER,0);
        lv_obj_set_style_pad_all(tap_notice,10,0);
        lv_obj_set_style_radius(tap_notice,8,0);
        lv_obj_remove_flag(tap_notice,LV_OBJ_FLAG_CLICKABLE);
    }
    lv_label_set_text_fmt(tap_notice,"%s selected",caption);
    lv_obj_align(tap_notice,LV_ALIGN_BOTTOM_MID,0,-84);
    lv_obj_remove_flag(tap_notice,LV_OBJ_FLAG_HIDDEN);
    tap_notice_until=elapsed_ms+1200;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,lv_event_cb_t cb,void *data)
{
    lv_obj_t *o=lv_button_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,width,58);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x233348),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x385675),LV_STATE_PRESSED);
    lv_obj_set_style_opa(o,LV_OPA_40,LV_STATE_DISABLED); lv_obj_set_style_radius(o,12,0);
    lv_obj_t *caption=label(o,text,0,0,&station_text_24); lv_obj_center(caption);
    lv_obj_add_event_cb(o,acknowledge_tap,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,data); return o;
}
static void nearby_again(lv_event_t *event);
static void navigate_tab(lv_event_t *e){station_view_t next=(station_view_t)(uintptr_t)lv_event_get_user_data(e);if(next==VIEW_TRANSPORT)nearby_again(e);else show_view(next);}
static void navigate(lv_event_t *e) { show_view((station_view_t)(uintptr_t)lv_event_get_user_data(e)); }
static void close_keyboard(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL)
        lv_obj_add_flag(lv_event_get_target_obj(e),LV_OBJ_FLAG_HIDDEN);
}
static void focus_input(lv_event_t *e)
{
    lv_keyboard_set_textarea(network_keyboard,lv_event_get_target_obj(e));
    lv_obj_remove_flag(network_keyboard,LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(network_keyboard);
}
static void keyboard(lv_obj_t *input)
{
    network_keyboard=lv_keyboard_create(content); lv_obj_set_align(network_keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(network_keyboard,16,350); lv_obj_set_size(network_keyboard,1248,310);
    lv_obj_set_style_bg_color(network_keyboard,lv_color_hex(0x101923),0);
    lv_obj_set_style_pad_all(network_keyboard,10,0);
    lv_obj_set_style_pad_row(network_keyboard,8,0);lv_obj_set_style_pad_column(network_keyboard,8,0);
    lv_obj_set_style_text_font(network_keyboard,&station_text_32,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(network_keyboard,lv_color_hex(0x2b3d50),LV_PART_ITEMS);
    lv_obj_set_style_text_color(network_keyboard,lv_color_hex(0xffffff),LV_PART_ITEMS);
    lv_obj_set_style_radius(network_keyboard,8,LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(network_keyboard,0,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(network_keyboard,lv_color_hex(ACCENT),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(network_keyboard,lv_color_hex(0x071914),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_keyboard_set_popovers(network_keyboard,false);
    lv_keyboard_set_textarea(network_keyboard,input); lv_obj_add_flag(network_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(network_keyboard,close_keyboard,LV_EVENT_ALL,NULL);
}
static lv_obj_t *input(lv_obj_t *parent,int x,int y,int width,unsigned max,const char *placeholder)
{
    lv_obj_t *o=lv_textarea_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,width,60);
    lv_textarea_set_one_line(o,true); lv_textarea_set_max_length(o,max);
    lv_textarea_set_placeholder_text(o,placeholder); lv_obj_set_style_text_font(o,&station_text_24,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x202d3e),0); lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x40516a),0);
    lv_obj_add_event_cb(o,focus_input,LV_EVENT_CLICKED,NULL); return o;
}
static void page_header(const char *title,page_t next)
{
    page=next; awaiting_save=false; awaiting_city=false; lv_obj_clean(content);
    label(content,title,40,20,&station_text_32);
    button(content,"Back",1060,12,180,navigate,(void *)(uintptr_t)VIEW_SETTINGS);
}
static void save_settings(void)
{
    awaiting_save=actions.save(&settings);
    lv_label_set_text(settings_status,awaiting_save?"Saving...":"Could not save. Please retry.");
}
static void set_label_if_changed(lv_obj_t *obj,const char *text)
{
    if(strcmp(lv_label_get_text(obj),text))lv_label_set_text(obj,text);
}
static void set_text_color_if_changed(lv_obj_t *obj,uint32_t rgb)
{
    lv_color_t color=lv_color_hex(rgb);
    if(!lv_color_eq(lv_obj_get_style_text_color(obj,0),color))lv_obj_set_style_text_color(obj,color,0);
}
static void update_header(void)
{
    char connection[96];
    if(network.connected)snprintf(connection,sizeof(connection),"%s",LV_SYMBOL_WIFI);
    else if(network.connecting)snprintf(connection,sizeof(connection),LV_SYMBOL_WIFI "  Connecting%.*s",1+(int)(elapsed_ms/400%3),"...");
    else if(network.retry_seconds)snprintf(connection,sizeof(connection),LV_SYMBOL_WIFI "  Retry in %u s",network.retry_seconds);
    else snprintf(connection,sizeof(connection),LV_SYMBOL_WIFI "  Offline");
    bool header_changed=strcmp(lv_label_get_text(wifi_icon),connection)!=0;
    if(header_changed)lv_label_set_text(wifi_icon,connection);
    set_text_color_if_changed(wifi_icon,network.connected?ACCENT:MUTED);
    if(header_changed)lv_obj_align(wifi_icon,LV_ALIGN_TOP_RIGHT,-36,22);
    if(view==VIEW_CLOCK){
        lv_obj_remove_flag(header_clock,LV_OBJ_FLAG_HIDDEN);
        set_label_if_changed(header_clock,network.location_name);
        lv_obj_set_width(header_clock,430);lv_label_set_long_mode(header_clock,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(header_clock,LV_TEXT_ALIGN_RIGHT,0);
        lv_obj_update_layout(wifi_icon);lv_obj_align_to(header_clock,wifi_icon,LV_ALIGN_OUT_LEFT_MID,-24,0);
    }
    else {
        lv_obj_set_width(header_clock,LV_SIZE_CONTENT);
        header_changed=header_changed||lv_obj_has_flag(header_clock,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(header_clock,LV_OBJ_FLAG_HIDDEN);
        char text[64]="--:--";time_t now=time(NULL);struct tm civil;
        if(network.source!=TIME_UNSET&&localtime_r(&now,&civil))strftime(text,sizeof(text),"%a %d %b  %H:%M",&civil);
        if(strcmp(lv_label_get_text(header_clock),text)){lv_label_set_text(header_clock,text);header_changed=true;}
        if(header_changed){lv_obj_update_layout(wifi_icon);lv_obj_align_to(header_clock,wifi_icon,LV_ALIGN_OUT_LEFT_MID,-24,0);}
    }
    char greeting[64]; snprintf(greeting,sizeof(greeting),settings.user_name[0]?"Welcome %s":"Welcome",settings.user_name);
    if(update_snapshot.countdown_seconds)snprintf(greeting,sizeof(greeting),"Update in %u s · Settings to postpone",update_snapshot.countdown_seconds);
    else if(update_snapshot.state==UPDATE_DOWNLOADING||update_snapshot.state==UPDATE_VERIFYING||update_snapshot.state==UPDATE_RESTARTING)
        snprintf(greeting,sizeof(greeting),"Update: %u%% · %s",update_snapshot.progress,update_snapshot.state==UPDATE_DOWNLOADING?"Downloading":update_snapshot.state==UPDATE_VERIFYING?"Verifying":"Restarting");
    if(strcmp(lv_label_get_text(welcome),greeting)) lv_label_set_text(welcome,greeting);
}
/* Match LVGL's glyph placement, excluding font ascent/descent whitespace. */
static void text_ink_bounds(const lv_font_t *font,const char *text,int *top,int *bottom)
{
    *top=font->line_height;*bottom=0;uint32_t at=0;
    while(text[at]){
        uint32_t letter=lv_text_encoded_next(text,&at);lv_font_glyph_dsc_t glyph;
        if(!lv_font_get_glyph_dsc(font,&glyph,letter,0)||!glyph.box_h)continue;
        int y=font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
        if(y<*top)*top=y;
        if(y+glyph.box_h>*bottom)*bottom=y+glyph.box_h;
    }
    if(*bottom<=*top){*top=0;*bottom=font->line_height;}
}
static const lv_font_t *selected_clock_font(void)
{
    switch(settings.clock_font){
    case 1:return &station_clock_font;
    case 3:return &station_light_clock_font;
    case 4:return &station_bold_clock_font;
    case 5:return &station_hairline_clock_font;
    case 6:return &station_condensed_clock_font;
    case 7:return &station_wide_clock_font;
    default:return NULL;
    }
}
static void align_clock_date(const char *plain,const char *date)
{
    int top,bottom;
    lv_obj_update_layout(content);
    if(selected_clock_font()!=NULL){
        const lv_font_t *font=selected_clock_font();
        text_ink_bounds(font,plain,&top,&bottom);
        lv_obj_set_y(clock_text,CLOCK_INK_BOTTOM-bottom);
    }else{
        bottom=0;
        for(unsigned d=0;d<4;d++)for(unsigned i=0;i<7;i++)if(!lv_obj_has_flag(segments[d][i],LV_OBJ_FLAG_HIDDEN)){
            int edge=clock_segment_y[d][i]+lv_obj_get_height(segments[d][i]);if(edge>bottom)bottom=edge;
        }
        for(unsigned i=0;i<2;i++){int edge=(i?138:58)+13;if(edge>bottom)bottom=edge;}
        int shift=CLOCK_INK_BOTTOM-bottom;
        for(unsigned d=0;d<4;d++)for(unsigned i=0;i<7;i++)lv_obj_set_y(segments[d][i],clock_segment_y[d][i]+shift);
        for(unsigned i=0;i<2;i++)lv_obj_set_y(clock_dots[i],(i?138:58)+shift);
    }
    text_ink_bounds(&station_text_24,date,&top,&bottom);
    int gap=(CLOCK_WIDGET_TOP-CLOCK_INK_BOTTOM-(bottom-top))/2;
    lv_obj_set_y(date_label,CLOCK_INK_BOTTOM+gap-top);
}
static void update_clock(void)
{
    char digits[5]="----",date[80]="Connect to Wi-Fi to get started",plain[6]="--:--";
    if(network.source!=TIME_UNSET) {
        time_t now=time(NULL); struct tm civil;
        if(localtime_r(&now,&civil)) {
            strftime(digits,sizeof(digits),"%H%M",&civil); strftime(plain,sizeof(plain),"%H:%M",&civil);
            strftime(date,sizeof(date),"%A, %d %B %Y",&civil);
        }
    }
    if(selected_clock_font()!=NULL) lv_label_set_text(clock_text,plain);
    else for(int d=0;d<4;d++) {
        uint8_t mask=station_digit_segments(digits[d]);
        for(int s=0;s<7;s++) {
            /* Never paint unlit bars: the clock background remains uniform. */
            if(mask&(1<<s)) lv_obj_remove_flag(segments[d][s],LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(segments[d][s],LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_label_set_text(date_label,date);
    align_clock_date(plain,date);
}
static void clock_card(lv_obj_t *card,station_view_t target)
{
    lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card,lv_color_hex(0x24394b),LV_STATE_PRESSED);
    lv_obj_add_event_cb(card,acknowledge_tap,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(card,navigate,LV_EVENT_CLICKED,(void *)(uintptr_t)target);
}
static lv_obj_t *summary_label(lv_obj_t *parent,const char *text,int x,int y,int width,const lv_font_t *font)
{
    lv_obj_t *o=label(parent,text,x,y,font);lv_obj_set_size(o,width,font->line_height);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *summary_spinner(lv_obj_t *parent,int x,int y)
{
    lv_obj_t *o=lv_spinner_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,24,24);lv_spinner_set_anim_params(o,900,200);lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static void clock_view(void)
{
    if(selected_clock_font()!=NULL) {
        clock_text=label(content,"00:00",0,settings.clock_font==3?12:-30,selected_clock_font());
        lv_obj_set_width(clock_text,1280);lv_obj_set_style_text_align(clock_text,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_color(clock_text,lv_color_hex(ink()),0);
    } else {
        const int geometry[7][4]={{25,0,110,20},{135,20,20,115},{135,155,20,115},{25,270,110,20},{5,155,20,115},{5,20,20,115},{25,135,110,20}};
        const int positions[]={195,390,695,890};
        for(int d=0;d<4;d++)for(int segment=0;segment<7;segment++){
            int x=geometry[segment][0],y=geometry[segment][1],w=geometry[segment][2],h=geometry[segment][3];
            if(settings.clock_font==2){if(w==20){x+=5;w=10;}else{y+=5;h=10;}}
            clock_segment_y[d][segment]=4+y*2/3;
            segments[d][segment]=box(content,640+(positions[d]+x-640)*2/3,4+y*2/3,w*2/3,h*2/3,ink());
            if(settings.clock_font==8)lv_obj_set_style_radius(segments[d][segment],LV_RADIUS_CIRCLE,0);
            if(settings.clock_font==9){lv_obj_set_style_bg_opa(segments[d][segment],LV_OPA_TRANSP,0);lv_obj_set_style_border_width(segments[d][segment],2,0);lv_obj_set_style_border_color(segments[d][segment],lv_color_hex(ink()),0);lv_obj_set_style_radius(segments[d][segment],0,0);}
        }
        clock_dots[0]=box(content,626,58,13,13,ink());clock_dots[1]=box(content,626,138,13,13,ink());
    }
    date_label=label(content,"",0,218,&station_text_24);lv_obj_set_width(date_label,1280);lv_obj_set_style_text_align(date_label,LV_TEXT_ALIGN_CENTER,0);
    /* Equal cards: 592 × 328, 32px page margins and 32px gutter. */
    lv_obj_t *card=box(content,32,CLOCK_WIDGET_TOP,592,328,0x121d29);clock_card(card,VIEW_WEATHER);
    weather_title=summary_label(card,"LOCAL WEATHER",24,24,500,&station_text_18);set_text_color_if_changed(weather_title,MUTED);
    clock_weather_busy=summary_spinner(card,544,22);
    weather_icon=station_icon_create(card,24,82,64,&icon_visual);lv_obj_remove_flag(weather_icon,LV_OBJ_FLAG_CLICKABLE);
    weather_temp=label(card,"--°",104,80,&station_text_48);set_text_color_if_changed(weather_temp,ink());
    weather_condition=summary_label(card,"",268,82,300,&station_text_24);
    weather_feels=summary_label(card,"",268,120,300,&station_text_18);set_text_color_if_changed(weather_feels,MUTED);
    weather_range=summary_label(card,"",24,168,544,&station_text_18);set_text_color_if_changed(weather_range,MUTED);
    clock_rain=summary_label(card,"",24,204,266,&station_text_18);clock_wind=summary_label(card,"",302,204,266,&station_text_18);
    lv_obj_t *rule=box(card,24,238,544,1,0x273443);lv_obj_remove_flag(rule,LV_OBJ_FLAG_CLICKABLE);
    graph_visual.compact=true;graph_visual.accent=ink();
    weather_graph=NULL;
    weather_graph_caption=summary_label(card,"",24,234,544,&station_text_18);set_text_color_if_changed(weather_graph_caption,MUTED);
    card=box(content,656,CLOCK_WIDGET_TOP,592,328,0x121d29);clock_card(card,VIEW_TRANSPORT);
    transport_title=summary_label(card,"Departures",24,24,330,&station_text_18);set_text_color_if_changed(transport_title,MUTED);
    transport_status_label=summary_label(card,"",362,24,166,&station_text_18);lv_obj_set_style_text_align(transport_status_label,LV_TEXT_ALIGN_RIGHT,0);set_text_color_if_changed(transport_status_label,MUTED);
    clock_transport_busy=summary_spinner(card,544,22);
    for(unsigned i=0;i<3;i++){
        int y=68+(int)i*68;
        clock_departure[i]=box(card,24,y,544,60,0x121d29);lv_obj_remove_flag(clock_departure[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *badge=box(clock_departure[i],0,4,44,40,0x28425c);lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);
        clock_badge[i]=summary_label(badge,"",0,9,44,&station_text_18);lv_obj_set_style_text_align(clock_badge[i],LV_TEXT_ALIGN_CENTER,0);
        clock_pickup[i]=summary_label(clock_departure[i],"",60,0,336,&station_text_24);
        clock_destination[i]=summary_label(clock_departure[i],"",60,32,336,&station_text_18);set_text_color_if_changed(clock_destination[i],MUTED);
        clock_wait[i]=summary_label(clock_departure[i],"",408,0,136,&station_text_32);lv_obj_set_style_text_align(clock_wait[i],LV_TEXT_ALIGN_RIGHT,0);
        clock_next[i]=summary_label(clock_departure[i],"",408,34,136,&station_text_18);lv_obj_set_style_text_align(clock_next[i],LV_TEXT_ALIGN_RIGHT,0);set_text_color_if_changed(clock_next[i],MUTED);
        if(i<2){rule=box(card,84,y+60,484,1,0x273443);lv_obj_remove_flag(rule,LV_OBJ_FLAG_CLICKABLE);}
    }
    transport_alert_label=summary_label(card,"",24,282,544,&station_text_18);lv_obj_set_height(transport_alert_label,42);lv_label_set_long_mode(transport_alert_label,LV_LABEL_LONG_WRAP);
    transport_footer=summary_label(card,"",24,300,544,&station_text_18);lv_obj_add_flag(transport_footer,LV_OBJ_FLAG_HIDDEN);
    update_clock();update_weather();update_transport();
}

static void metric(char *out,size_t size,float value,const char *unit,bool decimal)
{
    if(isfinite(value))snprintf(out,size,decimal?"%.1f%s":"%.0f%s",(double)value,unit);
    else snprintf(out,size,"--%s",unit);
}
static void weather_time(char *out,size_t size,time_t t,const char *format,bool daily)
{
    struct tm tm;
    if(daily){t+=weather.data.utc_offset;gmtime_r(&t,&tm);}else localtime_r(&t,&tm);
    strftime(out,size,format,&tm);
}
static const char *weather_state_text(void)
{
    if(!network.location_valid)return "Choose your home city in Settings";
    if(!network.connected)return weather.data.valid?"Offline - showing saved weather":"Connect Wi-Fi to load weather";
    if(weather.state==WEATHER_TIME)return "Waiting for clock synchronisation";
    if(weather.state==WEATHER_RETRY)return weather.data.valid?"Update failed - showing saved weather":"Weather unavailable - retrying";
    if(weather.state==WEATHER_LOADING)return "Updating weather...";
    if(weather.stale)return weather.data.valid?"Weather is out of date":"Loading weather...";
    return "";
}
static void read_weather(void)
{
    if(actions.weather_status)actions.weather_status(&weather);
    else memset(&weather,0,sizeof(weather));
    /* Never show the previous city's data while a worker is in flight. */
    if(weather.location_generation!=network.location_generation) {
        memset(&weather.data,0,sizeof(weather.data));weather.state=WEATHER_LOADING;
        snprintf(weather.location,sizeof(weather.location),"%s",network.location_name);
    }
}
static void update_weather(void)
{
    read_weather();const station_weather_data_t *d=&weather.data;
    time_t now=time(NULL);int today=station_weather_day_index(d,now);
    char temp[24],feel[24],humidity[24],high[24],low[24],chance[24],updated[24]="--:--",observed[24]="--:--";
    metric(temp,sizeof(temp),d->valid?d->temperature:NAN,view==VIEW_CLOCK?"°":" °C",false);
    metric(feel,sizeof(feel),d->valid?d->feels_like:NAN,"°",false);
    metric(humidity,sizeof(humidity),d->valid?d->humidity:NAN,"%",false);
    metric(high,sizeof(high),today>=0?d->days[today].high:NAN,"°",false);
    metric(low,sizeof(low),today>=0?d->days[today].low:NAN,"°",false);
    metric(chance,sizeof(chance),today>=0?d->days[today].rain_chance:NAN,"%",false);
    if(d->valid){weather_time(updated,sizeof(updated),d->fetched_at,"%H:%M",false);weather_time(observed,sizeof(observed),d->observed_at,"%H:%M",false);}
    const char *state=weather_state_text();
    const char *place=weather.location[0]?weather.location:"Your local weather";
    lv_label_set_text(weather_temp,temp);
    lv_label_set_text(weather_condition,d->valid?station_weather_condition(d->code,d->is_day):"Weather");
    station_graph_visual_t next_graph={.accent=ink(),.compact=view==VIEW_CLOCK};
    station_weather_plot(d,now,view==VIEW_CLOCK?6:12,&next_graph.plot);
    if(memcmp(&graph_visual,&next_graph,sizeof(next_graph))) {graph_visual=next_graph;if(weather_graph)lv_obj_invalidate(weather_graph);}
    if(icon_visual.code!=(d->valid?d->code:-1)||icon_visual.day!=d->is_day) {
        icon_visual=(station_icon_visual_t){.code=d->valid?d->code:-1,.day=d->is_day};lv_obj_invalidate(weather_icon);
    }
    if(view==VIEW_CLOCK) {
        set_label_if_changed(weather_title,"LOCAL WEATHER");
        if(weather.state==WEATHER_LOADING&&network.connected)lv_obj_remove_flag(clock_weather_busy,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(clock_weather_busy,LV_OBJ_FLAG_HIDDEN);
        char detail[160];snprintf(detail,sizeof(detail),"Humidity %s · Updated %s",humidity,updated);
        if(state[0]&&d->valid)snprintf(detail,sizeof(detail),"%s · %ld min old",state,(long)(now>=d->fetched_at?(now-d->fetched_at)/60:0));
        set_label_if_changed(weather_graph_caption,state[0]&&!d->valid?state:detail);lv_obj_set_y(weather_graph_caption,258);lv_obj_set_height(weather_graph_caption,48);lv_label_set_long_mode(weather_graph_caption,LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(weather_feels,"Feels like %s",feel);
        lv_label_set_text_fmt(weather_range,"Today: low %s / high %s",low,high);
        float peak=NAN;for(unsigned i=0;i<next_graph.plot.count;i++){float rain=next_graph.plot.points[i].rain_chance;if(isfinite(rain)&&(!isfinite(peak)||rain>peak))peak=rain;}
        char risk[20],wind[24];metric(risk,sizeof(risk),peak,"%",false);metric(wind,sizeof(wind),d->valid?d->wind:NAN," km/h",false);
        lv_label_set_text_fmt(clock_rain,"Rain peak (6h): %s",risk);lv_label_set_text_fmt(clock_wind,"Wind: %s",wind);
        return;
    }
    lv_label_set_text_fmt(weather_title,"Weather / %s",place);
    lv_label_set_text_fmt(weather_feels,"Feels like %s",feel);
    lv_label_set_text_fmt(weather_range,"Low %s / High %s",low,high);
    char value[48],gust[24];
    metric(value,sizeof(value),d->valid?d->humidity:NAN,"%",false);lv_label_set_text(weather_metrics[0],value);
    metric(value,sizeof(value),d->valid?d->wind:NAN,"",false);metric(gust,sizeof(gust),d->valid?d->gusts:NAN,"",false);
    lv_label_set_text_fmt(weather_metrics[1],"%s / %s km/h",value,gust);
    metric(value,sizeof(value),d->valid?d->pressure:NAN," hPa",false);lv_label_set_text(weather_metrics[2],value);
    metric(value,sizeof(value),d->valid?d->precipitation:NAN," mm",true);
    if(d->valid&&d->interval_seconds>0&&d->interval_seconds%60==0)
        lv_label_set_text_fmt(weather_metrics[4],"%s / %s in %dm",station_weather_direction(d->direction),value,d->interval_seconds/60);
    else lv_label_set_text_fmt(weather_metrics[4],"Precipitation %s",value);
    lv_label_set_text_fmt(weather_metrics[5],"Precip today %s",chance);
    float minimum=INFINITY,maximum=-INFINITY;
    for(unsigned i=0;i<d->day_count;i++)if(isfinite(d->days[i].low)&&isfinite(d->days[i].high)&&d->days[i].low<=d->days[i].high) {
        minimum=fminf(minimum,d->days[i].low);maximum=fmaxf(maximum,d->days[i].high);
    }
    if(!isfinite(minimum)){minimum=0;maximum=6;}else if(maximum-minimum<2){minimum-=1;maximum+=1;}
    for(unsigned i=0;i<WEATHER_DAYS;i++) {
        unsigned index=today>=0?(unsigned)today+i:WEATHER_DAYS;
        bool valid=d->valid&&index<d->day_count;const station_weather_day_t *day=&d->days[valid?index:0];
        if(valid)weather_time(value,sizeof(value),day->at,"%a",true);else snprintf(value,sizeof(value),"--");
        lv_label_set_text(day_titles[i],value);
        lv_label_set_text(day_conditions[i],valid?station_weather_condition(day->code,true):"No forecast");
        metric(high,sizeof(high),valid?day->high:NAN,"°",false);metric(low,sizeof(low),valid?day->low:NAN,"°",false);
        lv_label_set_text(day_lows[i],low);lv_label_set_text(day_highs[i],high);
        metric(chance,sizeof(chance),valid?day->rain_chance:NAN,"%",false);lv_label_set_text(day_rain[i],chance);
        day_icon_visuals[i]=(station_icon_visual_t){.code=valid?day->code:-1,.day=true};lv_obj_invalidate(day_icons[i]);
        day_range_visuals[i]=(station_range_visual_t){.low=valid?day->low:NAN,.high=valid?day->high:NAN,.minimum=minimum,.maximum=maximum,.accent=ink()};lv_obj_invalidate(day_bars[i]);
    }
    if(state[0]&&d->valid)lv_label_set_text_fmt(weather_footer,"%s / %ld min old / Open-Meteo model data",state,(long)(now>=d->fetched_at?(now-d->fetched_at)/60:0));
    else if(state[0])lv_label_set_text_fmt(weather_footer,"%s / Open-Meteo model data%s%s",state,d->valid?" / Updated ":"",d->valid?updated:"");
    else lv_label_set_text_fmt(weather_footer,"Open-Meteo / Model data / Updated %s / Conditions at %s",updated,observed);
}
static void refresh_weather(lv_event_t *event)
{
    (void)event;
    if(actions.refresh_weather && actions.refresh_weather())lv_label_set_text(weather_footer,"Weather update requested...");
    else lv_label_set_text(weather_footer,"Please wait a minute before refreshing again.");
}
static void weather_view(void)
{
    weather_title=label(content,"Weather",32,12,&station_text_32);lv_obj_set_width(weather_title,970);lv_label_set_long_mode(weather_title,LV_LABEL_LONG_DOT);
    button(content,"Refresh",1040,0,208,refresh_weather,NULL);
    box(content,32,76,1216,160,CARD);
    weather_icon=station_icon_create(content,56,100,88,&icon_visual);
    weather_temp=label(content,"",158,92,&station_text_48);lv_obj_set_style_text_color(weather_temp,lv_color_hex(ink()),0);
    weather_condition=label(content,"",158,154,&station_text_24);lv_obj_set_width(weather_condition,238);lv_label_set_long_mode(weather_condition,LV_LABEL_LONG_DOT);
    weather_feels=label(content,"",158,198,&station_text_18);
    box(content,417,101,1,108,0x344154);
    const char *names[]={"HUMIDITY","WIND / GUSTS","PRESSURE"};const int positions[]={450,700,1010};
    for(unsigned i=0;i<3;i++) {
        lv_obj_t *title=label(content,names[i],positions[i],100,&station_text_18);lv_obj_set_style_text_color(title,lv_color_hex(MUTED),0);
        weather_metrics[i]=label(content,"--",positions[i],133,&station_text_32);
    }
    weather_range=label(content,"",450,198,&station_text_18);
    weather_metrics[4]=label(content,"",700,198,&station_text_18);
    weather_metrics[5]=label(content,"",1010,198,&station_text_18);
    label(content,"Next 12 hours",32,260,&station_text_24);
    lv_obj_t *legend=label(content,"Temperature °C",640,263,&station_text_18);lv_obj_set_style_text_color(legend,lv_color_hex(ink()),0);
    graph_visual.compact=false;graph_visual.accent=ink();weather_graph=station_graph_create(content,32,296,820,318,&graph_visual);
    label(content,"3-day outlook",888,260,&station_text_24);
    legend=label(content,"Precip",1176,263,&station_text_18);lv_obj_set_style_text_color(legend,lv_color_hex(0x62b8ff),0);
    for(unsigned i=0;i<WEATHER_DAYS;i++) {
        int y=296+i*106;box(content,888,y,360,100,CARD);
        day_icons[i]=station_icon_create(content,900,y+23,48,&day_icon_visuals[i]);
        day_titles[i]=label(content,"",960,y+8,&station_text_24);
        day_conditions[i]=label(content,"",960,y+39,&station_text_18);lv_obj_set_width(day_conditions[i],270);lv_label_set_long_mode(day_conditions[i],LV_LABEL_LONG_DOT);
        day_rain[i]=label(content,"",1176,y+9,&station_text_18);lv_obj_set_style_text_color(day_rain[i],lv_color_hex(0x62b8ff),0);
        day_lows[i]=label(content,"",960,y+68,&station_text_18);lv_obj_set_width(day_lows[i],48);
        day_highs[i]=label(content,"",1198,y+68,&station_text_18);lv_obj_set_width(day_highs[i],48);
        day_bars[i]=station_range_create(content,1010,y+74,176,8,&day_range_visuals[i]);
    }
    weather_footer=label(content,"",32,632,&station_text_18);lv_obj_set_width(weather_footer,1216);lv_label_set_long_mode(weather_footer,LV_LABEL_LONG_DOT);
    update_weather();
}
static void brightness_changed(lv_event_t *e)
{
    unsigned value=lv_slider_get_value(lv_event_get_target_obj(e));
    settings.brightness=value;awake_until_ms=elapsed_ms+60000;update_power();save_settings();
}
static void rotate_changed(lv_event_t *e)
{
    settings.rotate=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED); save_settings();
}
static void update_theme(void)
{
    for(unsigned i=0;i<4;i++) {
        lv_obj_set_style_border_width(theme_buttons[i],settings.theme==i?3:0,0);
        lv_obj_set_style_border_color(theme_buttons[i],lv_color_hex(0xffffff),0);
        lv_obj_set_style_border_opa(theme_buttons[i],LV_OPA_COVER,0);
    }
    lv_obj_set_style_text_color(theme_preview,lv_color_hex(ink()),0);
}
static void theme_changed(lv_event_t *e)
{
    settings.theme=(uint8_t)(uintptr_t)lv_event_get_user_data(e); update_theme(); save_settings();
}
static void font_changed(lv_event_t *e)
{
    settings.clock_font=lv_dropdown_get_selected(lv_event_get_target_obj(e)); save_settings();
}
static void custom_changed(lv_event_t *e)
{
    (void)e;
    settings.custom_color=((uint32_t)lv_slider_get_value(color_sliders[0])<<16)|((uint32_t)lv_slider_get_value(color_sliders[1])<<8)|lv_slider_get_value(color_sliders[2]);
    settings.theme=3; lv_obj_set_style_text_color(color_preview,lv_color_hex(ink()),0);
    if(lv_event_get_code(e)==LV_EVENT_RELEASED) save_settings();
}
static void show_colors(lv_event_t *e)
{
    (void)e; page_header("Your clock colour",PAGE_COLOR);
    box(content,32,95,1216,470,CARD);
    color_preview=label(content,"12:34",830,245,&station_text_48);
    lv_obj_set_style_text_color(color_preview,lv_color_hex(settings.custom_color),0);
    const char *names[]={"Red","Green","Blue"};
    for(unsigned i=0;i<3;i++) {
        label(content,names[i],65,135+110*i,&station_text_24);
        color_sliders[i]=lv_slider_create(content); lv_obj_set_pos(color_sliders[i],210,150+110*i); lv_obj_set_size(color_sliders[i],520,24);
        lv_slider_set_range(color_sliders[i],0,255); lv_slider_set_value(color_sliders[i],(settings.custom_color>>(16-8*i))&255,LV_ANIM_OFF);
        lv_obj_add_event_cb(color_sliders[i],custom_changed,LV_EVENT_VALUE_CHANGED,NULL);
        lv_obj_add_event_cb(color_sliders[i],custom_changed,LV_EVENT_RELEASED,NULL);
    }
    settings_status=label(content,"Drag to preview. Release to save your colour.",65,490,&station_text_24);
}
static void scan_wifi(lv_event_t *event)
{
    (void)event;
    if (network.connecting) return;
    if (!actions.scan_wifi()) {
        set_label_if_changed(scan_label, "Scan unavailable or already running. Please retry.");
        return;
    }
    scan_pending = true;scan_when_ready=false;scan_started_ms=elapsed_ms;
    set_label_if_changed(scan_label, "Scanning nearby Wi-Fi networks...");
    lv_obj_add_state(rescan_button, LV_STATE_DISABLED);
    lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    lv_obj_add_state(ssid_picker, LV_STATE_DISABLED);
    lv_dropdown_close(ssid_picker);
    lv_obj_add_flag(network_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void update_scan_results(void)
{
    if(scan_when_ready&&!network.connecting){scan_when_ready=false;scan_wifi(NULL);}
    if(scan_pending||scan_when_ready) {
        lv_obj_remove_flag(scan_spinner,LV_OBJ_FLAG_HIDDEN);
        char progress[128];
        snprintf(progress,sizeof(progress),scan_when_ready?"Waiting for connection attempt%.*s":"Looking for networks%.*s",1+(int)(elapsed_ms/400%3),"...");
        if(!visible_scan.count&&strcmp(lv_dropdown_get_options(ssid_picker),progress))lv_dropdown_set_options(ssid_picker,progress);
        snprintf(progress,sizeof(progress),scan_when_ready?"Scan will start automatically when the connection attempt finishes.":"Scanning nearby Wi-Fi networks... %u s",(unsigned)((elapsed_ms-scan_started_ms)/1000));
        set_label_if_changed(scan_label,progress);
    } else lv_obj_add_flag(scan_spinner,LV_OBJ_FLAG_HIDDEN);
    station_wifi_scan_t latest;
    actions.wifi_scan_status(&latest);
    if (latest.revision == displayed_scan_revision) return;
    displayed_scan_revision = latest.revision;
    if (latest.state == WIFI_SCAN_RUNNING) {
        scan_pending = true;
        set_label_if_changed(scan_label, "Scanning nearby Wi-Fi networks...");
        lv_obj_add_state(rescan_button, LV_STATE_DISABLED);
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
        lv_obj_add_state(ssid_picker, LV_STATE_DISABLED);
        return;
    }
    scan_pending = false;
    lv_obj_remove_state(rescan_button, LV_STATE_DISABLED);
    if (latest.state == WIFI_SCAN_FAILED) {
        set_label_if_changed(scan_label, latest.message);
        if(!visible_scan.count)lv_dropdown_set_options(ssid_picker,"Scan failed - tap Rescan");
        /* Keep old results available after a failed rescan. */
        if (visible_scan.count) {
            lv_obj_remove_state(ssid_picker, LV_STATE_DISABLED);
            lv_obj_remove_state(connect_button, LV_STATE_DISABLED);
        }
        return;
    }
    if (latest.state != WIFI_SCAN_READY) return;
    char selected_ssid[33] = {0};
    unsigned selected = lv_dropdown_get_selected(ssid_picker);
    if (selected < visible_scan.count) memcpy(selected_ssid, visible_scan.networks[selected].ssid, sizeof(selected_ssid));
    visible_scan = latest;
    lv_dropdown_clear_options(ssid_picker);
    unsigned retained = 0;
    for (unsigned i=0; i<latest.count; ++i) {
        char name[33]; memcpy(name, latest.networks[i].ssid, sizeof(name));
        /* A scan result must not inject extra dropdown rows; connect uses raw SSID. */
        for (unsigned j=0; name[j]; ++j) if ((unsigned char)name[j] < 32 || name[j] == 127) name[j] = ' ';
        char row[80];
        snprintf(row, sizeof(row), "%s  (%d dBm, %s)", name, latest.networks[i].rssi,
                 latest.networks[i].saved ? "saved" : latest.networks[i].secured ? "secured" : "open");
        lv_dropdown_add_option(ssid_picker, row, LV_DROPDOWN_POS_LAST);
        if (strcmp(selected_ssid, latest.networks[i].ssid) == 0) retained = i;
    }
    set_label_if_changed(scan_label, latest.message);
    if (latest.count) {
        lv_dropdown_set_selected(ssid_picker, retained);
        lv_textarea_set_placeholder_text(password_input,latest.networks[retained].saved?"Saved password (leave blank to use)":"Enter Wi-Fi password");
        lv_obj_remove_state(ssid_picker, LV_STATE_DISABLED);
        lv_obj_remove_state(connect_button, LV_STATE_DISABLED);
    } else {
        lv_dropdown_set_options(ssid_picker, "No networks found");
        lv_obj_add_state(ssid_picker, LV_STATE_DISABLED);
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    }
}
static void toggle_password(lv_event_t *event)
{
    (void)event;
    bool masked=lv_textarea_get_password_mode(password_input);
    lv_textarea_set_password_mode(password_input,!masked);
    lv_label_set_text(password_caption,masked?"Hide":"Show");
}
static void hide_password(void)
{
    lv_textarea_set_password_mode(password_input,true);
    lv_label_set_text(password_caption,"Show");
}
static void network_selected(lv_event_t *event)
{
    (void)event;
    lv_textarea_set_text(password_input, "");
    hide_password();
    unsigned selected=lv_dropdown_get_selected(ssid_picker);
    bool saved=selected<visible_scan.count&&visible_scan.networks[selected].saved;
    lv_textarea_set_placeholder_text(password_input,saved?"Saved password (leave blank to use)":"Enter Wi-Fi password");
    lv_label_set_text(settings_status,saved?"Password remembered. Tap Connect, or enter a replacement.":"Network selected. Enter its password, then tap Connect.");
}
static void connect_wifi(lv_event_t *event)
{
    (void)event;
    unsigned selected = lv_dropdown_get_selected(ssid_picker);
    if (network.connecting) return;
    if (scan_pending || selected >= visible_scan.count) {
        lv_label_set_text(settings_status, "Select a detected network first"); return;
    }
    const station_wifi_ap_t *ap = &visible_scan.networks[selected];
    const char *password = ap->secured ? lv_textarea_get_text(password_input) : "";
    if (!station_wifi_credentials_valid(ap->ssid, password) || (ap->secured && !ap->saved && !password[0])) {
        lv_label_set_text(settings_status, "Enter the network password (8-63 bytes)"); return;
    }
    if (actions.connect_wifi(ap->ssid, password)) {
        hide_password();
        lv_textarea_set_text(password_input, "");
        lv_obj_add_flag(network_keyboard, LV_OBJ_FLAG_HIDDEN);
        network.connecting=true; network.connected=false;
        set_label_if_changed(connect_caption,"Connecting...");
        lv_obj_add_state(connect_button,LV_STATE_DISABLED);
        lv_obj_add_state(rescan_button,LV_STATE_DISABLED);
        lv_obj_add_state(ssid_picker,LV_STATE_DISABLED);
        lv_obj_remove_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);
        set_label_if_changed(network_label,"Connecting to your network...");
        lv_label_set_text(settings_status,"Connecting. You can leave this page.");
    } else lv_label_set_text(settings_status, "Connection request busy; please retry");
}
static void update_connection(void)
{
    bool busy=network.connecting;
    if(busy) {
        lv_obj_remove_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);
        set_label_if_changed(connect_caption,"Connecting...");
        char progress[128];snprintf(progress,sizeof(progress),"Connecting to Wi-Fi... %u s (up to 45 s)",network.connection_seconds);
        set_label_if_changed(network_label,progress);
    } else {
        lv_obj_add_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);
        set_label_if_changed(connect_caption,network.connected?"Reconnect":"Connect");
        char progress[192];
        if(scan_pending) {snprintf(progress,sizeof(progress),"Scanning for nearby networks...");lv_obj_remove_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);}
        else if(!network.connected&&network.retry_seconds) {snprintf(progress,sizeof(progress),"Reconnecting automatically in %u s",network.retry_seconds);lv_obj_remove_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);}
        else snprintf(progress,sizeof(progress),"%s",network.connected?(strstr(network.message,"could not be saved")?network.message:"Connected to Wi-Fi"):network.message);
        const char *text=progress;
        if(strcmp(lv_label_get_text(network_label),text)) set_label_if_changed(network_label,text);
    }
    set_text_color_if_changed(network_label,network.connected?ACCENT:0xe8edf5);
    if(busy || scan_pending || scan_when_ready) {
        lv_obj_add_state(connect_button,LV_STATE_DISABLED); lv_obj_add_state(rescan_button,LV_STATE_DISABLED); lv_obj_add_state(ssid_picker,LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(rescan_button,LV_STATE_DISABLED);
        if(visible_scan.count) { lv_obj_remove_state(connect_button,LV_STATE_DISABLED); lv_obj_remove_state(ssid_picker,LV_STATE_DISABLED); }
    }
    if(!busy && !strcmp(lv_label_get_text(settings_status),"Connecting. You can leave this page."))
        lv_label_set_text(settings_status,network.connected?"You're connected. Your clock updates automatically.":"Check the password, then try again.");
}
static void show_hotspot(lv_event_t *e)
{
    (void)e;page_header("iPhone Personal Hotspot",PAGE_HOTSPOT);
    box(content,32,94,1216,520,CARD);
    label(content,"1. iPhone: Settings > Personal Hotspot",60,125,&station_text_32);
    label(content,"Turn on Allow Others to Join and Maximise Compatibility (if available).",60,195,&station_text_24);
    label(content,"2. Keep that iPhone page open while scanning",60,265,&station_text_32);
    label(content,"On this display: Settings > Wi-Fi > Rescan. Select your iPhone.",60,335,&station_text_24);
    label(content,"3. Enter the hotspot password shown on your iPhone",60,405,&station_text_32);
    label(content,"Your location stays saved. Use iPhone setup to change it after moving.",60,475,&station_text_24);
    label(content,"The display uses 2.4 GHz Wi-Fi. Hotspot data uses your mobile plan.",60,545,&station_text_18);
}
static void update_phone(void)
{
    station_phone_link_t link={0};if(actions.phone_link)actions.phone_link(&link);
    if(link.online) {
        lv_obj_add_flag(phone_wait,LV_OBJ_FLAG_HIDDEN);
        if(strcmp(phone_url,link.url)) {
            lv_obj_add_flag(phone_qr,LV_OBJ_FLAG_HIDDEN);
            if(lv_qrcode_update(phone_qr,link.url,strlen(link.url))!=LV_RESULT_OK) {
                phone_url[0]=0;lv_obj_remove_flag(phone_wait,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(phone_caption,"Preparing your QR code... retrying automatically.");return;
            }
            snprintf(phone_url,sizeof(phone_url),"%s",link.url);
        }
        lv_obj_remove_flag(phone_qr,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(phone_caption,"Scan with your iPhone camera.\nYour phone and display must share Wi-Fi.\n\nYou can also use iPhone Personal Hotspot.\nRescan after switching networks or restarting.\n\nKeep this QR code private: it controls your display.");
    } else {
        phone_url[0]=0;lv_obj_add_flag(phone_qr,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(phone_wait,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(phone_caption,network.connected?"Wi-Fi connected. Waiting for iPhone setup to become available...":"Connect the display to Wi-Fi first.\n\nNo home Wi-Fi? Use your iPhone hotspot.\nOpen Settings > Wi-Fi on the display.\n\nThis page will show a QR code when connected.");
    }
}
static void show_phone(lv_event_t *e)
{
    (void)e;page_header("iPhone setup",PAGE_PHONE);
    box(content,32,94,1216,520,CARD);
    phone_qr=lv_qrcode_create(content);lv_qrcode_set_size(phone_qr,290);lv_qrcode_set_quiet_zone(phone_qr,true);
    lv_qrcode_set_dark_color(phone_qr,lv_color_black());lv_qrcode_set_light_color(phone_qr,lv_color_white());
    lv_obj_set_style_border_width(phone_qr,20,0);lv_obj_set_style_border_color(phone_qr,lv_color_white(),0);
    lv_obj_set_pos(phone_qr,80,155);
    phone_wait=lv_spinner_create(content);lv_obj_set_size(phone_wait,64,64);lv_obj_set_pos(phone_wait,193,265);lv_spinner_set_anim_params(phone_wait,1000,200);
    phone_caption=label(content,"",440,150,&station_text_24);lv_obj_set_width(phone_caption,750);
    button(content,"Wi-Fi settings",440,430,370,show_network,NULL);
    button(content,"Hotspot instructions",440,510,370,show_hotspot,NULL);
    phone_url[0]=0;update_phone();
}
static void show_network(lv_event_t *e)
{
    (void)e; page_header("Wi-Fi",PAGE_WIFI); refresh_network();
    box(content,32,94,1216,316,CARD);
    label(content,"Network",60,113,&station_text_24);
    ssid_picker=lv_dropdown_create(content); lv_obj_set_pos(ssid_picker,60,151); lv_obj_set_size(ssid_picker,830,60);
    scan_spinner=lv_spinner_create(content);lv_obj_set_pos(scan_spinner,905,164);lv_obj_set_size(scan_spinner,34,34);lv_spinner_set_anim_params(scan_spinner,900,200);lv_obj_add_flag(scan_spinner,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(ssid_picker,&station_text_24,0);
    lv_dropdown_set_options(ssid_picker,"Looking for networks..."); lv_obj_add_state(ssid_picker,LV_STATE_DISABLED);
    lv_obj_add_event_cb(ssid_picker,network_selected,LV_EVENT_VALUE_CHANGED,NULL);
    rescan_button=button(content,"Rescan",980,151,230,scan_wifi,NULL);
    label(content,"Password",60,229,&station_text_24);
    password_input=input(content,60,267,700,63,"Enter Wi-Fi password"); lv_textarea_set_password_mode(password_input,true);
    password_caption=lv_obj_get_child(button(content,"Show",780,267,170,toggle_password,NULL),0);
    connect_button=button(content,"Connect",980,267,230,connect_wifi,NULL); connect_caption=lv_obj_get_child(connect_button,0);
    scan_label=label(content,"",60,355,&station_text_24); lv_obj_set_width(scan_label,1140);
    box(content,32,434,1216,210,CARD);
    busy_indicator=lv_spinner_create(content); lv_obj_set_pos(busy_indicator,60,462); lv_obj_set_size(busy_indicator,42,42);
    lv_spinner_set_anim_params(busy_indicator,1000,200); lv_obj_add_flag(busy_indicator,LV_OBJ_FLAG_HIDDEN);
    network_label=label(content,"",120,470,&station_text_24); lv_obj_set_width(network_label,1070);
    settings_status=label(content,"Your clock stays in sync automatically.",60,554,&station_text_24); lv_obj_set_width(settings_status,1140);
    button(content,"iPhone hotspot help",840,585,360,show_hotspot,NULL);
    keyboard(password_input);
    memset(&visible_scan,0,sizeof(visible_scan)); displayed_scan_revision=UINT32_MAX; scan_pending=false;scan_when_ready=network.connecting;scan_started_ms=elapsed_ms;
    update_scan_results(); if(!scan_pending && !network.connecting) scan_wifi(NULL);update_scan_results(); update_connection();
}
static bool contains_fold(const char *text,const char *query)
{
    for(;*text;text++) {
        unsigned i=0; while(query[i] && text[i] && tolower((unsigned char)text[i])==tolower((unsigned char)query[i])) i++;
        if(!query[i]) return true;
    }
    return !*query;
}
static void filter_cities(lv_event_t *e)
{
    (void)e; const char *query=lv_textarea_get_text(city_search); city_matches=0;
    lv_dropdown_clear_options(city_picker);
    unsigned selected=0;
    if(!query[0]){lv_dropdown_add_option(city_picker,"Keep saved location",LV_DROPDOWN_POS_LAST);city_indices[city_matches++]=UINT32_MAX;}
    for(unsigned i=0;i<station_city_count && city_matches<512;i++) if(contains_fold(station_cities[i].name,query)) {
        if(query[0]&&!strcmp(station_cities[i].name,network.location_name)) selected=city_matches;
        city_indices[city_matches++]=i; lv_dropdown_add_option(city_picker,station_cities[i].name,LV_DROPDOWN_POS_LAST);
    }
    if(city_matches) { lv_obj_remove_state(city_picker,LV_STATE_DISABLED); lv_dropdown_set_selected(city_picker,selected); }
    else { lv_dropdown_set_options(city_picker,"No matching cities"); lv_obj_add_state(city_picker,LV_STATE_DISABLED); }
}
static void apply_city(lv_event_t *e)
{
    (void)e; unsigned selected=lv_dropdown_get_selected(city_picker);
    if(selected>=city_matches || city_indices[selected]==UINT32_MAX) { lv_label_set_text(settings_status,"Search for a city first."); return; }
    location_completion=network.location_completion;
    if(actions.set_city(city_indices[selected])) {
        awaiting_city=true;
        lv_label_set_text(settings_status,"Saving home city..."); lv_obj_add_flag(network_keyboard,LV_OBJ_FLAG_HIDDEN);
    } else lv_label_set_text(settings_status,"Could not save this city. Please retry.");
}
static void update_home_location(void)
{
    char current[320];
    if(network.location_valid)snprintf(current,sizeof(current),"Saved location: %s\n%.6f, %.6f · Used for weather and nearby transport\nTime zone: %s",network.location_name,network.latitude,network.longitude,network.timezone);
    else snprintf(current,sizeof(current),"No location saved. Set a precise location using iPhone setup.");
    set_label_if_changed(network_label,current);
}
static void show_location(lv_event_t *e)
{
    (void)e; page_header("Home location",PAGE_HOME); refresh_network();
    box(content,32,94,1216,140,CARD);
    network_label=label(content,"",60,114,&station_text_24);lv_obj_set_width(network_label,1160);lv_obj_set_style_text_line_space(network_label,10,0);update_home_location();
    button(content,"iPhone setup",900,580,310,show_phone,NULL);
    label(content,"Or replace with a city centre",60,245,&station_text_24);
    city_search=input(content,60,280,1150,48,"Search replacement city, e.g. Paris or London");
    city_picker=lv_dropdown_create(content);lv_obj_set_pos(city_picker,60,414);lv_obj_set_size(city_picker,840,60);lv_obj_set_style_text_font(city_picker,&station_text_24,0);
    button(content,"Use city",940,414,270,apply_city,NULL);
    lv_obj_add_event_cb(city_search,filter_cities,LV_EVENT_VALUE_CHANGED,NULL);filter_cities(NULL);
    settings_status=label(content,"Use city replaces the saved coordinates. Phone and display edit the same location.",60,508,&station_text_24);lv_obj_set_width(settings_status,1140);
    label(content,"The time zone is separate from your saved location.",60,600,&station_text_18);
    keyboard(city_search);
}
static void apply_profile(lv_event_t *e)
{
    (void)e; const char *name=lv_textarea_get_text(profile_input); size_t n=strlen(name);
    if(n>=sizeof(settings.user_name)) { lv_label_set_text(settings_status,"Please use a shorter name."); return; }
    for(size_t i=0;i<n;i++) if((unsigned char)name[i]<32) { lv_label_set_text(settings_status,"Use a single-line name."); return; }
    snprintf(settings.user_name,sizeof(settings.user_name),"%s",name); save_settings();
    lv_obj_add_flag(network_keyboard,LV_OBJ_FLAG_HIDDEN); update_header();
}
static void show_profile(lv_event_t *e)
{
    (void)e; page_header("Your profile",PAGE_PROFILE);
    box(content,32,94,1216,316,CARD);
    label(content,"What should we call you?",60,125,&station_text_32);
    profile_input=input(content,60,200,820,48,"Your name or household names"); lv_textarea_set_text(profile_input,settings.user_name);
    button(content,"Save name",920,200,290,apply_profile,NULL);
    label(content,"Shown as a welcome on your home display.",60,315,&station_text_24);
    settings_status=label(content,"Your name stays on this device.",60,470,&station_text_24); keyboard(profile_input);
}
static lv_obj_t *time_picker(int x,int y,unsigned count,unsigned selected)
{
    char options[200]="";size_t used=0;
    for(unsigned i=0;i<count;i++)used+=(size_t)snprintf(options+used,sizeof(options)-used,"%s%02u",i?"\n":"",i);
    lv_obj_t *picker=lv_dropdown_create(content);lv_obj_set_pos(picker,x,y);lv_obj_set_size(picker,120,60);
    lv_obj_set_style_text_font(picker,&station_text_24,0);lv_dropdown_set_options(picker,options);lv_dropdown_set_selected(picker,selected);
    return picker;
}
static void apply_power(lv_event_t *e)
{
    (void)e;station_settings_t next=settings;
    next.night_mode=lv_dropdown_get_selected(night_mode_picker);
    next.night_brightness=lv_slider_get_value(night_level);
    next.night_start=lv_dropdown_get_selected(night_start_hour)*60+lv_dropdown_get_selected(night_start_minute);
    next.night_end=lv_dropdown_get_selected(night_end_hour)*60+lv_dropdown_get_selected(night_end_minute);
    if(!station_settings_valid(&next)){lv_label_set_text(settings_status,"Choose different start and end times.");return;}
    settings=next;awake_until_ms=elapsed_ms+60000;update_power();save_settings();
}
static void night_level_changed(lv_event_t *e)
{
    lv_label_set_text_fmt(night_level_text,"%d%%",(int)lv_slider_get_value(lv_event_get_target_obj(e)));
}
static void show_power(lv_event_t *e)
{
    (void)e;page_header("Power & night mode",PAGE_POWER);
    box(content,32,94,1216,440,CARD);
    label(content,"Night mode",60,120,&station_text_24);
    night_mode_picker=lv_dropdown_create(content);lv_obj_set_pos(night_mode_picker,360,104);lv_obj_set_size(night_mode_picker,480,60);
    lv_obj_set_style_text_font(night_mode_picker,&station_text_24,0);lv_dropdown_set_options(night_mode_picker,"Off\nDim\nScreen off");lv_dropdown_set_selected(night_mode_picker,settings.night_mode);
    label(content,"From (local time)",60,215,&station_text_24);
    night_start_hour=time_picker(360,195,24,settings.night_start/60);night_start_minute=time_picker(500,195,60,settings.night_start%60);
    label(content,"Until",700,215,&station_text_24);
    night_end_hour=time_picker(840,195,24,settings.night_end/60);night_end_minute=time_picker(980,195,60,settings.night_end%60);
    label(content,"Dim level (1–100%)",60,305,&station_text_24);
    night_level=lv_slider_create(content);lv_obj_set_pos(night_level,360,315);lv_obj_set_size(night_level,640,22);lv_slider_set_range(night_level,1,100);lv_slider_set_value(night_level,settings.night_brightness,LV_ANIM_OFF);
    night_level_text=label(content,"",1040,300,&station_text_24);lv_label_set_text_fmt(night_level_text,"%u%%",settings.night_brightness);
    lv_obj_add_event_cb(night_level,night_level_changed,LV_EVENT_VALUE_CHANGED,NULL);
    label(content,"Touch wakes the screen for 60 seconds. Wi-Fi and the clock stay on.",60,390,&station_text_24);
    label(content,"No battery. Tempo restarts automatically when external power returns.",60,450,&station_text_24);
    settings_status=label(content,"Uses your saved time zone. Changes take effect after 60 seconds idle.",40,565,&station_text_18);
    lv_obj_set_width(settings_status,820);
    button(content,"Save night mode",900,545,310,apply_power,NULL);
}
static lv_obj_t *update_label,*update_version,*update_progress,*update_spinner;
static void render_updates(void)
{
 if(!actions.update_status)return;
 station_update_status_t u;actions.update_status(&u);update_snapshot=u;
 if(view==VIEW_SETTINGS&&page==PAGE_UPDATES){
  lv_label_set_text_fmt(update_version,"Installed: %s   Available: %s",u.installed,u.available[0]?u.available:"—");
  lv_label_set_text_fmt(update_label,"%s\nChecks: %s · Automatic installation: %s\nTransport credential: %s",u.message,u.interval_hours==1?"hourly (testing)":"every 30 days",u.automatic?"on":"off",u.credential_configured?"configured":"set up on your phone");
  lv_bar_set_value(update_progress,(int)u.progress,LV_ANIM_OFF);
  if(u.state==UPDATE_CHECKING||u.state==UPDATE_DOWNLOADING||u.state==UPDATE_VERIFYING)lv_obj_remove_flag(update_spinner,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(update_spinner,LV_OBJ_FLAG_HIDDEN);
 }
}
static void update_action(lv_event_t *e)
{
 const char *action=lv_event_get_user_data(e);
 bool ok=actions.update_request&&actions.update_request(action);
 lv_label_set_text(update_label,ok?"Request queued…":"Update service busy. Please retry.");
}
static void show_updates(lv_event_t *e)
{
 (void)e;page_header("Software updates",PAGE_UPDATES);
 update_version=label(content,"",60,120,&station_text_24);
 update_label=label(content,"Loading update status…",60,190,&station_text_24);lv_obj_set_size(update_label,1080,180);
 update_progress=lv_bar_create(content);lv_obj_set_pos(update_progress,60,390);lv_obj_set_size(update_progress,1000,22);
 update_spinner=lv_spinner_create(content);lv_obj_set_pos(update_spinner,1100,380);lv_obj_set_size(update_spinner,50,50);lv_spinner_set_anim_params(update_spinner,1000,200);
 button(content,"Check for updates",60,470,330,update_action,"check");button(content,"Install update",420,470,300,update_action,"install");button(content,"Postpone",750,470,280,update_action,"postpone");
 label(content,"Release notes, schedule and transport key: open iPhone setup.",60,565,&station_text_18);
 render_updates();
}
static void settings_view(void)
{
    page=PAGE_DISPLAY;
    label(content,"Make it yours",40,20,&station_text_32);
    box(content,32,95,810,540,CARD); box(content,866,95,382,540,CARD);
    label(content,"Brightness",60,120,&station_text_24);
    lv_obj_t *slider=lv_slider_create(content); lv_obj_set_pos(slider,265,133); lv_obj_set_size(slider,520,22);
    lv_slider_set_range(slider,5,100); lv_slider_set_value(slider,settings.brightness,LV_ANIM_OFF);
    lv_obj_add_event_cb(slider,brightness_changed,LV_EVENT_RELEASED,NULL);
    label(content,"Clock colour",60,192,&station_text_24);
    const char *names[]={"Orange","Teal","Blue","Custom"};
    for(unsigned i=0;i<4;i++) {
        theme_buttons[i]=button(content,names[i],60+185*i,236,165,theme_changed,(void *)(uintptr_t)i);
        lv_obj_set_style_bg_color(theme_buttons[i],lv_color_hex(i<3?colors[i]:settings.custom_color),0);
        lv_obj_set_style_text_color(lv_obj_get_child(theme_buttons[i],0),lv_color_hex(0x080c12),0);
    }
    button(content,"Other colours",60,318,270,show_colors,NULL);
    theme_preview=label(content,"12:34",570,320,&station_text_48); update_theme();
    label(content,"Clock font",60,405,&station_text_24);
    lv_obj_t *fonts=lv_dropdown_create(content); lv_obj_set_pos(fonts,265,389); lv_obj_set_size(fonts,520,58);
    lv_dropdown_set_options(fonts,"Digital\nModern\nSlim\nLight\nBold\nHairline\nCondensed\nWide\nRounded Digital\nOutline Digital"); lv_dropdown_set_selected(fonts,settings.clock_font);
    lv_obj_set_style_text_font(fonts,&station_text_24,0); lv_obj_add_event_cb(fonts,font_changed,LV_EVENT_VALUE_CHANGED,NULL);
    label(content,"Rotate views every 20 seconds",60,492,&station_text_24);
    lv_obj_t *toggle=lv_switch_create(content); lv_obj_set_pos(toggle,715,490);
    if(settings.rotate) lv_obj_add_state(toggle,LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle,rotate_changed,LV_EVENT_VALUE_CHANGED,NULL);
    settings_status=label(content,"Changes save automatically",60,570,&station_text_24);
    label(content,"SETUP",900,125,&station_text_24);
    button(content,"Wi-Fi",900,165,310,show_network,NULL);
    button(content,"Home location",900,240,310,show_location,NULL);
    button(content,"Your profile",900,315,310,show_profile,NULL);
    button(content,"iPhone setup",900,390,310,show_phone,NULL);
    button(content,"Power & night mode",900,465,310,show_power,NULL);
    button(content,"Software updates",900,540,310,show_updates,NULL);
}
static const char *transport_state_text(void)
{
    switch(transport.state){
    case TRANS_LOCATION:return "Set your precise location with your phone";
    case TRANS_WIFI:return "Wi-Fi disconnected · showing cached data";
    case TRANS_TIME:return "Waiting for clock synchronisation";
    case TRANS_KEY:return "Transport API key not configured";
    case TRANS_LOADING:return "Updating nearby transport…";
    case TRANS_DENIED:return "PRIM access denied · check API permissions";
    case TRANS_QUOTA:return "API limit reached · retrying later";
    case TRANS_EMPTY:return "No supported platforms within 2 km";
    case TRANS_RETRY:return "Could not refresh · retrying automatically";
    default:return "";
    }
}
static void passage_text(char *text,size_t size,const station_transport_row_t *r,unsigned slot,time_t now)
{
    struct tm tm;char clock[16];localtime_r(&r->at[slot],&tm);strftime(clock,sizeof(clock),"%H:%M",&tm);
    long minutes=(long)((r->at[slot]>now?r->at[slot]-now:0)+59)/60;
    if(r->cancelled[slot])snprintf(text,size,"Cancelled · %s",clock);
    else snprintf(text,size,"%s %s · %ld min%s",r->arrival[slot]?"Arrives":"Departs",clock,minutes,r->predicted[slot]?"":" · scheduled");
}
static void departure_text(char *text,size_t size,const station_transport_row_t *r,time_t now,bool stale)
{
    if(stale){snprintf(text,size,"Out of date");return;}
    unsigned first=r->at[0]<now-30?1:0;
    if(!r->at[first]||r->at[first]<now-30){snprintf(text,size,"Awaiting update");return;}
    char next[100],delay[48]="";passage_text(text,size,r,first,now);
    if(!r->cancelled[first]&&r->delay_minutes[first]>0)snprintf(delay,sizeof(delay),"\nDelayed +%d min",r->delay_minutes[first]);
    else if(!r->cancelled[first]&&!strcmp(r->status,"delayed"))snprintf(delay,sizeof(delay),"\nDelay reported");
    else if(!strcmp(r->status,"arrived"))snprintf(delay,sizeof(delay),"\nAt the stop");
    if(first==0&&r->at[1]>r->at[0]){passage_text(next,sizeof(next),r,1,now);size_t used=strlen(text);snprintf(text+used,size-used,"\n%s",next);}
    size_t used=strlen(text);snprintf(text+used,size-used,"%s",delay);
}
static lv_obj_t *transport_services;
static unsigned list_selection=UINT32_MAX,list_generation=UINT32_MAX,list_epoch=UINT32_MAX,list_rows;
static void update_transport(void)
{
    if(actions.transport_status)actions.transport_status(&transport);else memset(&transport,0,sizeof(transport));
    if(transport.generation!=network.location_generation){memset(&transport,0,sizeof(transport));transport.state=TRANS_LOADING;}
    time_t now=time(NULL);bool stale=!network.connected||!transport.valid||now<transport.fetched_at||now-transport.fetched_at>360;
    char title[160],status[320],alerts[260];
    const station_transport_stop_t *stop=transport.selected<transport.count?&transport.stops[transport.selected]:NULL;
    if(stop)snprintf(title,sizeof(title),"%s%s%s · %u m",stop->mode,stop->mode[0]?" · ":"",stop->name,stop->metres);else snprintf(title,sizeof(title),"Nearby transport");
    snprintf(status,sizeof(status),"%s",transport_state_text());
    if(transport.state==TRANS_READY&&network.connected){if(!transport.row_count)snprintf(status,sizeof(status),"No upcoming departures reported for this platform");else snprintf(status,sizeof(status),"%s · updated %ld min ago · Île-de-France Mobilités",stale?"Out of date":"Stop predictions",(long)((now-transport.fetched_at)/60));}
    if(!network.connected&&transport.valid)snprintf(status,sizeof(status),"Offline · saved %ld min ago · predictions unavailable",(long)(now>=transport.fetched_at?(now-transport.fetched_at)/60:0));
    if(!network.connected||now-transport.traffic_at>2700||(!transport.traffic_valid&&!transport.alert_count))snprintf(alerts,sizeof(alerts),"Traffic status unavailable");
    else if(transport.alert_count){unsigned alert=(unsigned)(elapsed_ms/8000)%transport.alert_count;snprintf(alerts,sizeof(alerts),"%u/%u · %s%s",alert+1,transport.alert_count,transport.alerts[alert],!transport.traffic_valid?" · other status unavailable":transport.traffic_partial?" · partial coverage":"");}
    else snprintf(alerts,sizeof(alerts),transport.traffic_partial?"Traffic checked for some lines only":"No active alerts reported for checked lines");
    if(view==VIEW_CLOCK){
        bool overview=transport.summary.count>0;
        unsigned rows=overview?transport.summary.count:transport.row_count;
        set_label_if_changed(transport_title,overview?"NEARBY TRANSPORT":stop?stop->name:"Departures near you");
        bool loading=transport.state==TRANS_LOADING||transport.discovering||transport.summary.loading;
        if(loading&&network.connected)lv_obj_remove_flag(clock_transport_busy,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(clock_transport_busy,LV_OBJ_FLAG_HIDDEN);
        time_t oldest=transport.fetched_at;
        if(overview){oldest=transport.summary.fetched[0];for(unsigned i=1;i<rows;i++)if(transport.summary.fetched[i]<oldest)oldest=transport.summary.fetched[i];}
        bool summary_stale=!network.connected||now<oldest||now-oldest>360;
        char freshness[96];snprintf(freshness,sizeof(freshness),network.connected?"Updated %ld min ago":"Offline · saved %ld min ago",(long)(now>=oldest?(now-oldest)/60:0));
        set_label_if_changed(transport_status_label,!network.connected?freshness:loading?"Updating…":(overview||transport.valid)?(summary_stale?"Out of date":now-oldest<60?"Just updated":freshness):"Waiting for data");
        for(unsigned i=0;i<3;i++){
            if((!overview&&!transport.valid)||i>=rows){lv_obj_add_flag(clock_departure[i],LV_OBJ_FLAG_HIDDEN);continue;}
            lv_obj_remove_flag(clock_departure[i],LV_OBJ_FLAG_HIDDEN);
            const station_transport_row_t *r=overview?&transport.summary.rows[i]:&transport.rows[i];
            time_t fetched=overview?transport.summary.fetched[i]:transport.fetched_at;bool stale=!network.connected||now<fetched||now-fetched>360;
            unsigned first=r->at[0]<now-30?1:0;
            bool available=r->at[first]&&r->at[first]>=now-30;char wait[40],next[64]="";
            if(stale)snprintf(wait,sizeof(wait),"Out of date");
            else if(!available)snprintf(wait,sizeof(wait),"Updating…");
            else if(r->cancelled[first])snprintf(wait,sizeof(wait),"Cancelled");
            else snprintf(wait,sizeof(wait),"%ld min",(long)((r->at[first]>now?r->at[first]-now:0)+59)/60);
            if(!stale&&available){
                if(r->delay_minutes[first]>0)snprintf(next,sizeof(next),"+%d min late",r->delay_minutes[first]);
                else if(!r->predicted[first])snprintf(next,sizeof(next),"Scheduled");
                else if(first==0&&r->at[1]>r->at[0])snprintf(next,sizeof(next),r->cancelled[1]?"Then cancelled":"Then %ld min",(long)((r->at[1]-now+59)/60));
            }
            set_label_if_changed(clock_badge[i],r->code);
            set_label_if_changed(clock_pickup[i],r->destination);
            char boarding[160],passage[16]="";struct tm departure;
            if(available&&!stale&&localtime_r(&r->at[first],&departure))strftime(passage,sizeof(passage),"%H:%M",&departure);
            bool same_stop=!overview&&stop&&(!r->pickup[0]||!strcmp(r->pickup,stop->name));
            if(same_stop)snprintf(boarding,sizeof(boarding),"%s%s%s%s",r->platform[0]?"Platform ":"Boarding point",r->platform,passage[0]?" · ":"",passage);
            else snprintf(boarding,sizeof(boarding),"%s%s%s",overview?transport.summary.stops[i]:r->pickup[0]?r->pickup:"Boarding point",passage[0]?" · ":"",passage);
            set_label_if_changed(clock_destination[i],boarding);set_label_if_changed(clock_wait[i],wait);lv_obj_set_style_text_font(clock_wait[i],strlen(wait)>7?&station_text_18:&station_text_32,0);set_label_if_changed(clock_next[i],next);
            set_text_color_if_changed(clock_wait[i],r->cancelled[first]||r->delay_minutes[first]>0?0xffb86b:ink());
        }
        set_label_if_changed(transport_alert_label,!network.connected?"Reconnect to refresh departures and service alerts":overview?(transport.summary.partial?"Some nearby services unavailable · retrying":"Times include reported delays · tap for service alerts"):transport.valid&&transport.row_count?alerts:status);
        set_text_color_if_changed(transport_alert_label,transport.alert_count?0xffb86b:MUTED);
        set_label_if_changed(transport_footer,transport.valid&&transport.row_count?"Tap for services and disruption details":"Tap to open transport");return;
    }
    if(view!=VIEW_TRANSPORT)return;
    if(transport_search_open){update_station_search();return;}
    if(transport_detail_open){update_service_detail(alerts,stale);return;}
    if(transport_map){update_transport_map();return;}
    char heading[190];snprintf(heading,sizeof(heading),"%s%s%s",stop?stop->mode:"",stop&&stop->mode[0]?" · ":"",stop?stop->name:"Nearby transport");set_label_if_changed(transport_title,heading);
    char meta[400];snprintf(meta,sizeof(meta),"%u/%u · %u m · %s",stop?transport.selected+1:0,transport.count,stop?stop->metres:0,status);snprintf(status,sizeof(status),"%.*s",(int)sizeof(status)-1,meta);
    lv_label_set_text(transport_status_label,status);lv_label_set_text(transport_alert_label,alerts);
    lv_label_set_text(transport_footer,transport.discovering?"Finding additional nearby stations…":transport.discovery_partial?"Partial discovery · some stations could not be checked":"RER > Métro > Bus / Tram · 2 km · select Map for stations");
    for(unsigned i=0;i<TRANSPORT_ROWS;i++){
        if(i<transport.row_count&&transport.valid){
            lv_obj_remove_flag(transport_cards[i],LV_OBJ_FLAG_HIDDEN);
            const station_transport_row_t *r=&transport.rows[i];char when[240],pickup[160];departure_text(when,sizeof(when),r,now,stale);
            lv_label_set_text(transport_lines[i],r->code);lv_label_set_text(transport_destinations[i],r->destination);lv_label_set_text(transport_times[i],when);
            snprintf(pickup,sizeof(pickup),"%s%s%s",r->pickup[0]?r->pickup:stop?stop->name:"Boarding point",r->platform[0]?" · Platform ":"",r->platform);
            lv_label_set_text(transport_pickups[i],pickup);
            lv_obj_set_style_text_color(transport_times[i],lv_color_hex((r->cancelled[0]||r->delay_minutes[0]>0||!strcmp(r->status,"delayed"))?0xffb86b:0xe8edf5),0);
        }else {lv_obj_add_flag(transport_cards[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(transport_lines[i],"");lv_label_set_text(transport_destinations[i],"");lv_label_set_text(transport_times[i],"");lv_label_set_text(transport_pickups[i],"");}
    }
    bool reset_scroll=list_selection!=transport.selected||list_generation!=transport.generation||list_epoch!=transport.selection_generation||transport.row_count<list_rows;
    lv_obj_update_layout(transport_services);
    if(reset_scroll)lv_obj_scroll_to_y(transport_services,0,LV_ANIM_OFF);
    list_selection=transport.selected;list_generation=transport.generation;list_epoch=transport.selection_generation;list_rows=transport.row_count;
#ifdef ESP_PLATFORM
    static unsigned logged_revision=UINT32_MAX;
    if(logged_revision!=transport.revision){
        unsigned visible=0;for(unsigned i=0;i<transport.row_count&&i<TRANSPORT_ROWS;i++)if(lv_obj_is_visible(transport_cards[i]))visible++;
        ESP_LOGI("transport_ui","revision=%u station=%s rows=%u valid=%d visible_cards=%u scroll_y=%ld",transport.revision,stop?stop->id:"none",transport.row_count,transport.valid,visible,(long)lv_obj_get_scroll_y(transport_services));logged_revision=transport.revision;
    }
#endif
    if(transport.count<2){lv_obj_add_state(transport_previous,LV_STATE_DISABLED);lv_obj_add_state(transport_next,LV_STATE_DISABLED);}else {lv_obj_remove_state(transport_previous,LV_STATE_DISABLED);lv_obj_remove_state(transport_next,LV_STATE_DISABLED);}
    if(transport.state==TRANS_LOADING)lv_obj_add_state(transport_refresh_button,LV_STATE_DISABLED);else lv_obj_remove_state(transport_refresh_button,LV_STATE_DISABLED);
}
static void select_transport(lv_event_t *e)
{
    if(!transport.count||!actions.transport_select)return;
    int direction=(int)(intptr_t)lv_event_get_user_data(e);
    unsigned next=(transport.selected+transport.count+direction)%transport.count;
    actions.transport_select(next);update_transport();
}
static void refresh_transport(lv_event_t *e)
{
    (void)e;if(actions.transport_refresh&&actions.transport_refresh())lv_label_set_text(transport_status_label,"Refresh requested…");
    else lv_label_set_text(transport_status_label,"Please wait a few seconds before refreshing again");
}
static void transport_view(void)
{
    transport_title=label(content,"Nearby transport",32,15,&station_text_24);lv_obj_set_size(transport_title,440,40);lv_label_set_long_mode(transport_title,LV_LABEL_LONG_DOT);
    button(content,"Near me",490,4,150,nearby_again,NULL);
    button(content,"Search",650,4,140,open_station_search,NULL);
    button(content,"Map",800,4,80,open_transport_map,NULL);
    transport_previous=button(content,LV_SYMBOL_LEFT,890,4,65,select_transport,(void *)(intptr_t)-1);
    transport_next=button(content,LV_SYMBOL_RIGHT,965,4,65,select_transport,(void *)(intptr_t)1);
    transport_refresh_button=button(content,"Refresh",1040,4,200,refresh_transport,NULL);
    transport_status_label=label(content,"",32,66,&station_text_18);lv_obj_set_size(transport_status_label,1200,36);lv_label_set_long_mode(transport_status_label,LV_LABEL_LONG_DOT);
    lv_obj_t *services=box(content,32,108,1216,440,BG);transport_services=services;list_selection=list_generation=list_epoch=UINT32_MAX;list_rows=0;lv_obj_add_flag(services,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(services,LV_DIR_VER);
    for(unsigned i=0;i<TRANSPORT_ROWS;i++){
        int y=(int)i*124;transport_cards[i]=box(services,0,y,1216,112,CARD);lv_obj_add_flag(transport_cards[i],LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(transport_cards[i],acknowledge_tap,LV_EVENT_CLICKED,NULL);lv_obj_add_event_cb(transport_cards[i],open_service_detail,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        transport_lines[i]=label(transport_cards[i],"",16,30,&station_text_24);lv_obj_set_width(transport_lines[i],130);lv_obj_set_style_text_color(transport_lines[i],lv_color_hex(ink()),0);
        transport_destinations[i]=label(transport_cards[i],"",162,14,&station_text_24);lv_obj_set_width(transport_destinations[i],560);lv_label_set_long_mode(transport_destinations[i],LV_LABEL_LONG_DOT);
        transport_pickups[i]=label(transport_cards[i],"",162,54,&station_text_18);lv_obj_set_width(transport_pickups[i],560);lv_label_set_long_mode(transport_pickups[i],LV_LABEL_LONG_DOT);
        transport_times[i]=label(transport_cards[i],"",738,10,&station_text_24);lv_obj_set_width(transport_times[i],464);lv_obj_set_style_text_line_space(transport_times[i],8,0);lv_obj_set_style_text_align(transport_times[i],LV_TEXT_ALIGN_RIGHT,0);
    }
    transport_alert_label=label(content,"",32,556,&station_text_24);lv_obj_set_size(transport_alert_label,1216,64);lv_label_set_long_mode(transport_alert_label,LV_LABEL_LONG_DOT);
    transport_footer=label(content,"",32,630,&station_text_18);
    update_transport();
}
#include "transport_views.inc"
static void show_view(station_view_t next)
{
    view=next; transport_map=false;transport_search_open=false;transport_detail_open=false; page=PAGE_DISPLAY; awaiting_save=false; awaiting_city=false; last_change_ms=elapsed_ms;
    lv_obj_clean(content); refresh_network();
    lv_obj_set_style_text_font(welcome,view==VIEW_CLOCK?&station_text_18:&station_text_24,0);
    lv_obj_set_style_text_font(header_clock,view==VIEW_CLOCK?&station_text_18:&station_text_24,0);
    lv_obj_set_style_text_font(wifi_icon,view==VIEW_CLOCK?&station_text_18:&station_text_24,0);
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(view==VIEW_CLOCK?0x0c121b:BG),0);
    lv_obj_set_style_bg_color(content,lv_color_hex(view==VIEW_CLOCK?0x0c121b:BG),0);
    for(unsigned i=0;i<4;i++){
        lv_obj_set_pos(nav[i],320+(int)i*160,718);
        lv_obj_set_size(nav[i],148,52);
        lv_obj_set_style_text_font(lv_obj_get_child(nav[i],0),&station_text_18,0);
    }
    update_header();
    for(unsigned i=0;i<4;i++) lv_obj_set_style_bg_color(nav[i],lv_color_hex(i==(unsigned)view?0x29483f:0x0c121b),0);
    if(view==VIEW_CLOCK) clock_view();
    else if(view==VIEW_SETTINGS) settings_view();
    else if(view==VIEW_WEATHER) weather_view();
    else transport_view();
}
static unsigned power_target(bool *clock_trusted)
{
    time_t now=time(NULL);struct tm local={0};
    bool valid=network.source!=TIME_UNSET && localtime_r(&now,&local)!=NULL;
    if(clock_trusted)*clock_trusted=valid;
    return station_power_brightness(&settings,valid,(unsigned)(local.tm_hour*60+local.tm_min),elapsed_ms,awake_until_ms);
}
static void update_power(void)
{
    bool valid;
    unsigned level=power_target(&valid);
    if(level!=requested_brightness){requested_brightness=level;brightness_retry_ms=0;}
    if((!brightness_failed&&level==applied_brightness)||elapsed_ms<brightness_retry_ms)return;
    if(actions.brightness && actions.brightness(level)) {
        if(brightness_failed&&view==VIEW_SETTINGS&&page==PAGE_POWER&&settings_status)
            lv_label_set_text(settings_status,"Backlight restored.");
        brightness_failed=false;brightness_retry_ms=0;
        applied_brightness=level;
#ifdef ESP_PLATFORM
        ESP_LOGI("power","Backlight %u%%; night mode=%u, clock trusted=%d",level,settings.night_mode,valid);
#endif
    } else {
        brightness_failed=true;brightness_retry_ms=elapsed_ms+5000;
#ifdef ESP_PLATFORM
        ESP_LOGW("power","Backlight request %u%% failed; state uncertain, retrying in 5 s",level);
#endif
        if(view==VIEW_SETTINGS&&page==PAGE_POWER&&settings_status)
            lv_label_set_text(settings_status,"Backlight change failed. Retrying…");
    }
}
bool station_ui_touch(bool pressed)
{
    if(pressed) {
        /* Check current policy before extending wake: the schedule can change between ticks.
         * Keep the latch until release, even when the wake brightness succeeds immediately. */
        if(power_target(NULL)==0||requested_brightness==0||applied_brightness==0||brightness_failed)
            consume_wake_touch=true;
        awake_until_ms=elapsed_ms+60000;
        update_power();
        /* The wake attempt itself can fail. Never forward that same press until
         * a nonzero requested level has been confirmed by the driver. */
        if(brightness_failed||applied_brightness==0||applied_brightness!=requested_brightness)
            consume_wake_touch=true;
    }
    bool consumed=consume_wake_touch;
    if(!pressed)consume_wake_touch=false;
    return consumed;
}
static void tick(lv_timer_t *timer)
{
    (void)timer; elapsed_ms+=100; atomic_fetch_add(&heartbeat,1);
    if(tap_notice&&elapsed_ms>=tap_notice_until)lv_obj_add_flag(tap_notice,LV_OBJ_FLAG_HIDDEN);
    station_phone_command_t command;
    if(actions.phone_take && actions.phone_take(&command)) {
        if(command.change_settings && station_settings_valid(&command.settings)) {
            settings=command.settings;awake_until_ms=elapsed_ms+60000;update_power();actions.save(&settings);
            show_view(view);
        }
        if(command.change_view && command.view<VIEW_COUNT){awake_until_ms=elapsed_ms+60000;if(command.view==VIEW_TRANSPORT&&actions.transport_nearby_reset)actions.transport_nearby_reset();show_view(command.view);}
    }
    if(actions.phone_publish)actions.phone_publish(&settings,view);
    refresh_network();
    if(elapsed_ms%1000==0)render_updates();
    update_header(); update_power();
    if(view==VIEW_SETTINGS && page==PAGE_PHONE && elapsed_ms%1000==0)update_phone();
    if(awaiting_save && view==VIEW_SETTINGS) {
        int state=actions.save_state();
        if(state==2 || state==3) { lv_label_set_text(settings_status,state==2?"Saved":"Could not save. Please retry."); awaiting_save=false; }
    }
    if(view==VIEW_CLOCK && elapsed_ms%1000==0) update_clock();
    if((view==VIEW_CLOCK||view==VIEW_WEATHER) && elapsed_ms%1000==0) update_weather();
    if((view==VIEW_CLOCK||view==VIEW_TRANSPORT) && elapsed_ms%1000==0)update_transport();
    if(view==VIEW_TRANSPORT&&transport_map&&elapsed_ms%200==0)update_transport_map();
    if(view==VIEW_SETTINGS && page==PAGE_WIFI) { update_scan_results(); update_connection(); }
    if(view==VIEW_SETTINGS && page==PAGE_HOME)update_home_location();
    if(awaiting_city && view==VIEW_SETTINGS && page==PAGE_HOME && network.location_completion!=location_completion) {
        awaiting_city=false;
        lv_label_set_text(settings_status,network.location_success?"Home city saved. Your clock now uses local time.":"Could not save home city. Please retry.");
    }
    if(station_rotation_due(&settings,view,elapsed_ms,last_change_ms)) show_view(station_next_view(view));
}
void station_ui_start(const station_settings_t *initial,station_ui_actions_t handlers)
{
    station_text_fonts_init();
    settings=*initial; actions=handlers;awake_until_ms=0;applied_brightness=requested_brightness=UINT32_MAX;brightness_failed=false;brightness_retry_ms=0;consume_wake_touch=false;
    lv_obj_t *screen=lv_screen_active(); lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0); lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    content=box(screen,0,64,1280,660,BG); lv_obj_set_style_radius(content,0,0);
    lv_obj_set_pos(content,0,64); lv_obj_set_height(content,660);
    welcome=label(screen,"",40,22,&station_text_24);
    wifi_icon=label(screen,"",1050,22,&station_text_24);
    header_clock=label(screen,"",750,22,&station_text_24);
    lv_obj_set_width(welcome,580);lv_label_set_long_mode(welcome,LV_LABEL_LONG_DOT);
    const char *names[]={"Clock","Weather","Transport","Settings"};
    for(unsigned i=0;i<4;i++) nav[i]=button(screen,names[i],32+i*312,726,280,navigate_tab,(void *)(uintptr_t)i);
    show_view(VIEW_CLOCK);
    /* show_view refreshes clock trust; apply policy before releasing the display lock. */
    update_power();
    lv_timer_create(tick,100,NULL);
}
unsigned station_ui_heartbeat(void) { return atomic_load(&heartbeat); }
