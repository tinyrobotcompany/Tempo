#include "phone_protocol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
/* Bound parser stack usage before passing untrusted request data to cJSON. */
bool station_phone_json_shallow(const char *text)
{
    unsigned depth=0; bool quoted=false,escaped=false;
    for(const unsigned char *p=(const unsigned char*)text;*p;p++) {
        if(quoted) {if(escaped)escaped=false;else if(*p=='\\')escaped=true;else if(*p=='"')quoted=false;continue;}
        if(*p=='"')quoted=true;
        else if(*p=='{'||*p=='[') {if(++depth>8)return false;}
        else if(*p=='}'||*p==']') {if(!depth)return false;depth--;}
    }
    return !quoted&&!depth;
}
static bool number(const cJSON *j,const char *key,double min,double max,double *value)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<min||v->valuedouble>max)return false;
    *value=v->valuedouble;return true;
}
static bool string(const cJSON *j,const char *key,char *value,size_t size)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsString(v)||strlen(v->valuestring)>=size)return false;
    for(const unsigned char *p=(const unsigned char*)v->valuestring;*p;p++)if(*p<32||*p==127)return false;
    snprintf(value,size,"%s",v->valuestring);return true;
}
bool station_phone_parse_settings(const cJSON *j,station_settings_t *s)
{
    double brightness,theme,color,font,dwell;
    const cJSON *rotate=cJSON_GetObjectItemCaseSensitive(j,"rotate");
    if(!cJSON_IsObject(j)||!number(j,"brightness",5,100,&brightness)||!number(j,"theme",0,3,&theme)||
       !number(j,"color",0,16777215,&color)||!number(j,"font",0,9,&font)||!number(j,"dwell",10,300,&dwell)||
       floor(brightness)!=brightness||floor(theme)!=theme||floor(color)!=color||floor(font)!=font||floor(dwell)!=dwell||
       !cJSON_IsBool(rotate)||!string(j,"name",s->user_name,sizeof(s->user_name)))return false;
    s->brightness=brightness;s->theme=theme;s->custom_color=color;s->clock_font=font;
    s->rotate=cJSON_IsTrue(rotate);s->dwell_seconds=dwell;
    const char *keys[]={"nightMode","nightBrightness","nightStart","nightEnd"};
    unsigned present=0;for(unsigned i=0;i<4;i++)present+=cJSON_GetObjectItemCaseSensitive(j,keys[i])!=NULL;
    if(present) {
        double mode,level,start,end;
        if(present!=4||!number(j,keys[0],0,2,&mode)||!number(j,keys[1],1,100,&level)||
           !number(j,keys[2],0,1439,&start)||!number(j,keys[3],0,1439,&end)||
           floor(mode)!=mode||floor(level)!=level||floor(start)!=start||floor(end)!=end)return false;
        s->night_mode=mode;s->night_brightness=level;s->night_start=start;s->night_end=end;
    }
    return station_settings_valid(s);
}
bool station_phone_parse_home(const cJSON *j,char coordinates[64],char name[64])
{
    double lat,lon;
    if(!cJSON_IsObject(j)||!number(j,"latitude",-90,90,&lat)||!number(j,"longitude",-180,180,&lon)||!string(j,"name",name,64))return false;
    snprintf(coordinates,64,"%.6f,%.6f",lat,lon);
    return true;
}
