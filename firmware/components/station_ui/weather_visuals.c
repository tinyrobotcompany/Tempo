#include "fonts/text_fonts.h"
#include "weather_visuals.h"
#include <math.h>
#include <stdio.h>
#define MUTED 0x96a6ba
#define BLUE 0x62b8ff
#define GRID 0x263344
static void line(lv_layer_t *layer,int x1,int y1,int x2,int y2,int width,uint32_t color)
{
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){x1,y1};d.p2=(lv_point_precise_t){x2,y2};
    d.width=width;d.color=lv_color_hex(color);d.round_start=d.round_end=1;lv_draw_line(layer,&d);
}
static void rect(lv_layer_t *layer,int x,int y,int w,int h,int radius,uint32_t color)
{
    if(w<=0||h<=0)return;
    lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;lv_draw_rect(layer,&d,&a);
}
static void text(lv_layer_t *layer,const char *value,int x,int y,int width,uint32_t color,lv_text_align_t align)
{
    lv_area_t a={x,y,x+width-1,y+23};lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);
    d.text=value;d.text_local=1;d.font=&station_text_18;d.color=lv_color_hex(color);d.align=align;
    lv_draw_label(layer,&d,&a);
}
static void graph_draw(lv_event_t *event)
{
    lv_obj_t *o=lv_event_get_target_obj(event);station_graph_visual_t *v=lv_event_get_user_data(event);
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(o,&a);
    int w=lv_obj_get_width(o),h=lv_obj_get_height(o),left=a.x1+60,right=a.x2-26;
    int top=a.y1+(v->compact?10:30),bottom=a.y1+(v->compact?64:174);
    int rain_top=a.y1+(v->compact?92:218),base=a.y2-27;
    const station_weather_plot_t *p=&v->plot;char value[32];
    if(v->compact){
        int x0=a.x1+6,x1=a.x2-6,y0=a.y1+5,y1=a.y2-25;bool any=false,previous=false;int px=0,py=0;
        for(unsigned i=0;i<p->count;i++){
            const station_weather_hour_t *point=&p->points[i];
            if(!isfinite(point->temperature)||p->end<=p->start){previous=false;continue;}
            int x=x0+(int)((double)(point->at-p->start)/(p->end-p->start)*(x1-x0));
            int y=y1-(int)((point->temperature-p->low)/fmaxf(1,p->high-p->low)*(y1-y0));
            if(previous)line(layer,px,py,x,y,3,v->accent);
            previous=true;any=true;px=x;py=y;
        }
        if(!any)text(layer,"Forecast unavailable",x0,y0,x1-x0,MUTED,LV_TEXT_ALIGN_CENTER);
        for(unsigned tick=0;tick<3;tick++){
            time_t when=p->start+(p->end-p->start)*tick/2;struct tm civil;localtime_r(&when,&civil);strftime(value,sizeof(value),"%H:%M",&civil);
            int x=tick==0?a.x1:tick==1?a.x1+w/2-40:a.x2-79;
            text(layer,value,x,a.y2-20,80,MUTED,tick==0?LV_TEXT_ALIGN_LEFT:tick==1?LV_TEXT_ALIGN_CENTER:LV_TEXT_ALIGN_RIGHT);
        }
        return;
    }

    for(unsigned row=0;row<3;row++) {
        int y=top+(bottom-top)*(int)row/2;
        line(layer,left,y,right,y,1,GRID);
        if(!v->compact || row!=1) {
            snprintf(value,sizeof(value),"%.0f°",(double)(p->high-(p->high-p->low)*row/2));
            text(layer,value,a.x1,y-10,39,MUTED,LV_TEXT_ALIGN_RIGHT);
        }
    }
    line(layer,left,base,right,base,1,GRID);
    text(layer,"100%",a.x1,rain_top-10,55,BLUE,LV_TEXT_ALIGN_RIGHT);
    text(layer,"0",a.x1,base-11,39,BLUE,LV_TEXT_ALIGN_RIGHT);
    if(!v->compact)text(layer,"Precipitation chance",left,rain_top-27,260,BLUE,LV_TEXT_ALIGN_LEFT);
    unsigned ticks=v->compact?3:4;
    for(unsigned i=0;i<ticks;i++) {
        time_t t=p->start+(time_t)lround((double)(p->end-p->start)/3600*i/(ticks-1))*3600;struct tm tm;localtime_r(&t,&tm);
        strftime(value,sizeof(value),"%H:%M",&tm);int x=left+(int)((double)(t-p->start)/(p->end-p->start)*(right-left));
        text(layer,value,x-29,base+5,58,MUTED,LV_TEXT_ALIGN_CENTER);
    }
    bool any=false;
    for(unsigned i=0;i<p->count;i++) {
        const station_weather_hour_t *point=&p->points[i];
        int x=left+(int)((double)(point->at-p->start)/(p->end-p->start)*(right-left));
        if(isfinite(point->rain_chance)) {
            any=true;int bh=(int)lroundf(point->rain_chance/100.0f*(base-rain_top));
            int bw=v->compact?18:28;
            if(bh>0)rect(layer,x-bw/2,base-bh,bw,bh,4,BLUE);
            else line(layer,x-bw/2,base,x+bw/2,base,2,BLUE);
        }
        if(!isfinite(point->temperature))continue;
        any=true;int y=bottom-(int)lroundf((point->temperature-p->low)/(p->high-p->low)*(bottom-top));
        if(station_weather_plot_join(p,i)) {
            const station_weather_hour_t *prev=&p->points[i-1];
            int px=left+(int)((double)(prev->at-p->start)/(p->end-p->start)*(right-left));
            int py=bottom-(int)lroundf((prev->temperature-p->low)/(p->high-p->low)*(bottom-top));
            line(layer,px,py,x,y,3,v->accent);
        }
        rect(layer,x-4,y-4,8,8,4,v->accent);
        if(!v->compact && (i==p->count-1||(i>0&&i%3==0))) {
            snprintf(value,sizeof(value),"%.0f°",(double)point->temperature);
            text(layer,value,x-25,y-28,50,v->accent,LV_TEXT_ALIGN_CENTER);
        }
    }
    if(!any)text(layer,"Forecast unavailable",left,top+15,right-left,MUTED,LV_TEXT_ALIGN_CENTER);
    (void)w;(void)h;
}
static void icon_draw(lv_event_t *event)
{
    lv_obj_t *o=lv_event_get_target_obj(event);station_icon_visual_t *v=lv_event_get_user_data(event);
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(o,&a);int s=lv_obj_get_width(o);
    int x=a.x1,y=a.y1,cx=x+s/2,cy=y+s/2;
    bool clear=v->code==0||v->code==1,partly=v->code==2,fog=v->code==45||v->code==48;
    bool snow=(v->code==71||v->code==73||v->code==75||v->code==77)||v->code==85||v->code==86;
    bool storm=v->code==95||v->code==96||v->code==99;
    bool rain=(v->code==51||v->code==53||v->code==55||v->code==56||v->code==57||v->code==61||v->code==63||v->code==65||v->code==66||v->code==67)||(v->code>=80&&v->code<=82);
    if(clear||partly) {
        int sx=partly?x+s*3/8:cx,sy=partly?y+s/3:cy;
        if(v->day) {
            int r=s/6;rect(layer,sx-r,sy-r,2*r,2*r,r,0xffd36d);
            for(unsigned i=0;i<8;i++) {
                float angle=i*0.78539816f;
                line(layer,sx+(int)(cosf(angle)*s/4),sy+(int)(sinf(angle)*s/4),sx+(int)(cosf(angle)*s/3),sy+(int)(sinf(angle)*s/3),2,0xffd36d);
            }
        }else {
            lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center=(lv_point_t){sx,sy};d.radius=s/3;d.width=s/6;
            d.start_angle=55;d.end_angle=290;d.rounded=1;d.color=lv_color_hex(0xb8d4ff);lv_draw_arc(layer,&d);
        }
        if(clear)return;
    }
    if(v->code<0 || !(partly||v->code==3||fog||snow||storm||rain)) {
        text(layer,"?",x,cy-10,s,MUTED,LV_TEXT_ALIGN_CENTER);return;
    }
    uint32_t cloud=0xd8e4f0;
    rect(layer,x+s/6,y+s/3,s/3,s/3,s/6,cloud);
    rect(layer,x+s/3,y+s/5,s*2/5,s*2/5,s/5,cloud);
    rect(layer,x+s/2,y+s*2/5,s/3,s/3,s/6,cloud);
    rect(layer,x+s/4,y+s/2,s/2,s/5,s/10,cloud);
    if(storm) {
        line(layer,cx+s/10,y+s*2/3,cx-s/10,y+s*4/5,3,0xffd36d);
        line(layer,cx-s/10,y+s*4/5,cx+s/10,y+s*4/5,3,0xffd36d);
        line(layer,cx+s/10,y+s*4/5,cx-s/10,y+s*19/20,3,0xffd36d);
    } else if(rain||snow)for(unsigned i=0;i<3;i++) {
        int px=x+s/3+i*s/6,py=y+s*4/5;
        line(layer,px,py,px-(snow?0:s/15),py+s/10,2,BLUE);
        if(snow)line(layer,px-s/20,py+s/20,px+s/20,py+s/20,2,BLUE);
    }
    if(fog)for(unsigned i=0;i<2;i++)line(layer,x+s/5,y+s*4/5+i*s/10,x+s*4/5,y+s*4/5+i*s/10,2,MUTED);
}
static void range_draw(lv_event_t *event)
{
    station_range_visual_t *v=lv_event_get_user_data(event);lv_obj_t *o=lv_event_get_target_obj(event);
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(o,&a);int w=lv_obj_get_width(o),h=lv_obj_get_height(o);
    rect(layer,a.x1,a.y1,w,h,h/2,GRID);
    if(!isfinite(v->low)||!isfinite(v->high)||v->low>v->high||v->maximum<=v->minimum)return;
    int left=(int)lroundf((v->low-v->minimum)/(v->maximum-v->minimum)*(w-h));
    int right=(int)lroundf((v->high-v->minimum)/(v->maximum-v->minimum)*(w-h));
    rect(layer,a.x1+left,a.y1,right-left+h,h,h/2,v->accent);
}
static lv_obj_t *visual(lv_obj_t *parent,int x,int y,int w,int h,lv_event_cb_t draw,void *state)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,draw,LV_EVENT_DRAW_MAIN,state);return o;
}
lv_obj_t *station_graph_create(lv_obj_t *p,int x,int y,int w,int h,station_graph_visual_t *s){return visual(p,x,y,w,h,graph_draw,s);}
lv_obj_t *station_icon_create(lv_obj_t *p,int x,int y,int size,station_icon_visual_t *s){return visual(p,x,y,size,size,icon_draw,s);}
lv_obj_t *station_range_create(lv_obj_t *p,int x,int y,int w,int h,station_range_visual_t *s){return visual(p,x,y,w,h,range_draw,s);}
