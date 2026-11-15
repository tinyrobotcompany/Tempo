#include "transport_json_filter.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define DEPTH 64
/* Object: 0 initial key/end, 1 colon, 2 value, 3 comma/end, 4 required key.
   Array: 0 initial value/end, 3 comma/end, 4 required value. */
typedef struct {char kind;unsigned state,count;bool emit,keep;char key[48];} frame_t;
struct transport_json_filter {char *out;size_t cap,used;frame_t frames[DEPTH];unsigned depth;
 bool network,siri;bool failed,root_started,root_done,string,key,escape,emit,key_overflow;
 unsigned unicode;char token[64];size_t token_len;bool primitive;};
static bool write_char(transport_json_filter_t *f,char c){
 if(f->used+1>=f->cap){f->failed=true;return false;}f->out[f->used++]=c;return true;
}
static bool keep_key(transport_json_filter_t *f,const char *s){
 if(f->siri){
  static const char *keys[]={"Siri","ServiceDelivery","StopMonitoringDelivery","Status","ErrorCondition","MonitoredStopVisit","MonitoringRef","MonitoredVehicleJourney","LineRef","DirectionName","DestinationRef","DestinationName","MonitoredCall","StopPointRef","StopPointName","ArrivalPlatformName","DeparturePlatformName","ExpectedArrivalTime","ExpectedDepartureTime","AimedArrivalTime","AimedDepartureTime","ArrivalStatus","DepartureStatus","VehicleAtStop","value"};
  for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(!strcmp(s,keys[i]))return true;
  return false;
 }
 if(f->network){
  static const char *keys[]={"route_schedules","display_informations","direction","table","rows","places","places_nearby","pagination","total_result","start_page","items_per_page","items_on_page","stop_areas","stop_points","stop_point","stop_area","id","name","label","embedded_type","distance","coord","lat","lon","commercial_modes","commercial_mode","codes","type","value","lines","code","color","network"};
  for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(!strcmp(s,keys[i]))return true;
  return false;
 }
 static const char *keys[]={"line_reports","line","id","links","type","pt_objects","embedded_type","stop_point","stop_area","disruptions","status","messages","text","channel","content_type","name","severity"};
 for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(!strcmp(s,keys[i]))return true;
 return false;
}
static bool begin_value(transport_json_filter_t *f,bool *emit){
 if(!f->depth){if(f->root_started)return false;f->root_started=true;*emit=true;return true;}
 frame_t *p=&f->frames[f->depth-1];
 if((p->kind=='{'&&p->state!=2)||(p->kind=='['&&p->state!=0&&p->state!=4))return false;
 *emit=p->emit&&(p->kind=='['||p->keep);
 if(*emit){
  if(p->count&&!write_char(f,','))return false;
  if(p->kind=='{'){
   if(!write_char(f,'"'))return false;
   for(const char *s=p->key;*s;s++)if(!write_char(f,*s))return false;
   if(!write_char(f,'"')||!write_char(f,':'))return false;
  }
  p->count++;
 }
 p->state=3;return true;
}
static void end_value(transport_json_filter_t *f){if(!f->depth)f->root_done=true;}
static bool valid_primitive(const char *s){
 if(!strcmp(s,"true")||!strcmp(s,"false")||!strcmp(s,"null"))return true;
 if(*s=='-')s++;
 if(*s=='0')s++;else {if(*s<'1'||*s>'9')return false;while(isdigit((unsigned char)*s))s++;}
 if(*s=='.'){s++;if(!isdigit((unsigned char)*s))return false;while(isdigit((unsigned char)*s))s++;}
 if(*s=='e'||*s=='E'){s++;if(*s=='+'||*s=='-')s++;if(!isdigit((unsigned char)*s))return false;while(isdigit((unsigned char)*s))s++;}
 return !*s;
}
transport_json_filter_t *transport_json_filter_create(char *out,size_t cap){
 if(!out||!cap)return NULL;
 transport_json_filter_t *f=calloc(1,sizeof(*f));if(f){f->out=out;f->cap=cap;out[0]=0;}return f;
}
bool transport_json_filter_feed(transport_json_filter_t *f,const char *data,size_t length){
 if(!f||f->failed)return false;
 for(size_t i=0;i<length;i++){
  unsigned char c=(unsigned char)data[i];
  if(f->string){
   if(f->unicode){if(!isxdigit(c))goto bad;f->unicode--;}
   else if(f->escape){f->escape=false;if(c=='u')f->unicode=4;else if(!strchr("\"\\/bfnrt",c))goto bad;}
   else if(c=='\\'){f->escape=true;if(f->key)f->key_overflow=true;}
   else if(c=='"'){
    f->string=false;
    if(f->key){frame_t *p=&f->frames[f->depth-1];p->key[f->token_len]=0;p->keep=!f->key_overflow&&keep_key(f,p->key);p->state=1;}
    else {if(f->emit&&!write_char(f,(char)c))goto bad;end_value(f);}continue;
   }else if(c<32)goto bad;
   if(f->key){if(f->token_len<sizeof(f->frames[0].key)-1)f->frames[f->depth-1].key[f->token_len++]=(char)c;else f->key_overflow=true;}
   else if(f->emit&&!write_char(f,(char)c))goto bad;
   continue;
  }
  if(f->primitive){
   if(!isspace(c)&&c!=','&&c!=']'&&c!='}'){
    if(f->token_len+1>=sizeof(f->token))goto bad;
    f->token[f->token_len++]=(char)c;
    if(f->emit&&!write_char(f,(char)c))goto bad;
    continue;
   }
   f->token[f->token_len]=0;if(!valid_primitive(f->token))goto bad;
   f->primitive=false;end_value(f);
  }
  if(isspace(c))continue;
  if(f->root_done)goto bad;
  frame_t *p=f->depth?&f->frames[f->depth-1]:NULL;
  if(c=='}'||c==']'){
   if(!p||p->kind!=(c=='}'?'{':'[')||(p->state!=0&&p->state!=3))goto bad;
   if(p->emit&&!write_char(f,(char)c))goto bad;
   f->depth--;end_value(f);continue;
  }
  if(c==','){if(!p||p->state!=3)goto bad;p->state=4;continue;}
  if(c==':'){if(!p||p->kind!='{'||p->state!=1)goto bad;p->state=2;continue;}
  if(c=='"'&&p&&p->kind=='{'&&(p->state==0||p->state==4)){
   f->string=true;f->key=true;f->token_len=0;f->key_overflow=false;continue;
  }
  bool emit=false;if(!begin_value(f,&emit))goto bad;
  if(c=='{'||c=='['){
   if(f->depth==DEPTH)goto bad;
   if(emit&&!write_char(f,(char)c))goto bad;
   f->frames[f->depth++]=(frame_t){.kind=(char)c,.emit=emit};continue;
  }
  f->emit=emit;f->key=false;f->token_len=0;
  if(c=='"')f->string=true;
  else {if(c!='-'&&!isdigit(c)&&c!='t'&&c!='f'&&c!='n')goto bad;f->primitive=true;f->token[f->token_len++]=(char)c;}
  if(emit&&!write_char(f,(char)c))goto bad;
 }
 return true;
 bad:f->failed=true;return false;
}
bool transport_json_filter_finish(transport_json_filter_t *f,size_t *length){
 if(!f||f->failed||f->string||f->depth)return false;
 if(f->primitive){f->token[f->token_len]=0;if(!valid_primitive(f->token))return false;f->primitive=false;end_value(f);}
 if(!f->root_done)return false;
 f->out[f->used]=0;*length=f->used;return true;
}
void transport_json_filter_free(transport_json_filter_t *f){free(f);}

transport_json_filter_t *transport_json_filter_create_network(char *out,size_t cap){transport_json_filter_t *f=transport_json_filter_create(out,cap);if(f)f->network=true;return f;}

transport_json_filter_t *transport_json_filter_create_siri(char *out,size_t cap){transport_json_filter_t *f=transport_json_filter_create(out,cap);if(f)f->siri=true;return f;}
