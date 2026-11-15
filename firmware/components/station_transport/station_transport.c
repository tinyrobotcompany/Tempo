#include "station_transport.h"
#include "transport_lookup.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
static const cJSON *get(const cJSON *j,const char *k){return cJSON_GetObjectItemCaseSensitive(j,k);}
static const char *str(const cJSON *j){return cJSON_IsString(j)?j->valuestring:"";}
static const char *value(const cJSON *j){if(cJSON_IsArray(j))j=cJSON_GetArrayItem(j,0);return cJSON_IsObject(j)?str(get(j,"value")):str(j);}
static void copy(char *out,size_t size,const char *in){size_t n=strlen(in);if(n>=size){n=size-1;while(n&&((unsigned char)in[n]&0xc0)==0x80)n--;}memcpy(out,in,n);out[n]=0;}
static bool digits(const char *s){if(!*s||strlen(s)>12)return false;for(;*s;s++)if(!isdigit((unsigned char)*s))return false;return true;}
static bool failed(const cJSON *j){const cJSON *s=get(j,"Status");return get(j,"ErrorCondition")||cJSON_IsFalse(s)||!strcmp(str(s),"false");}
time_t station_transport_timestamp(const char *s){
 int y,m,d,h,mi,se,n=0;if(!s||sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d%n",&y,&m,&d,&h,&mi,&se,&n)!=6)return 0;
 const char *tail=s+n;if(*tail=='.'){tail++;if(!isdigit((unsigned char)*tail))return 0;while(isdigit((unsigned char)*tail))tail++;}
 if(strcmp(tail,"Z")||y<2020||y>2100||m<1||m>12||d<1||h<0||h>23||mi<0||mi>59||se<0||se>59)return 0;
 const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};int leap=y%4==0&&(y%100!=0||y%400==0);
 if(d>lengths[m-1]+(m==2&&leap))return 0;
 int year=y-(m<=2),era=year/400,yoe=year-era*400;
 int doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 int64_t days=(int64_t)era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
 return (time_t)(days*86400+h*3600+mi*60+se);
}
static unsigned mode_priority(const char *mode){
 if(!strcmp(mode,"RER"))return 0;
 if(!strcmp(mode,"Métro")||!strcmp(mode,"Metro"))return 1;
 if(!strcmp(mode,"Bus")||!strcmp(mode,"Tramway")||!strcmp(mode,"Tram"))return 2;
 return 3;
}
static bool coordinate(const cJSON *v,double *out){
 if(cJSON_IsNumber(v)){*out=v->valuedouble;return isfinite(*out);}
 const char *s=str(v);char *end;*out=strtod(s,&end);return *s&&!*end&&isfinite(*out);
}
bool station_transport_nearby_page(const char *json,station_transport_snapshot_t *out,bool reset,bool *more){
 cJSON *root=cJSON_Parse(json);const cJSON *places=get(root,"places_nearby");
 if(!cJSON_IsArray(places)){cJSON_Delete(root);return false;}
 if(reset)out->count=0;
 const cJSON *pagination=get(root,"pagination");
 const cJSON *total=get(pagination,"total_result"),*start=get(pagination,"start_page"),*size=get(pagination,"items_per_page");
 *more=cJSON_IsNumber(total)&&cJSON_IsNumber(start)&&cJSON_IsNumber(size)&&size->valueint>0&&
  (start->valuedouble+1)*size->valuedouble<total->valuedouble;
 const cJSON *p;cJSON_ArrayForEach(p,places){
  if(strcmp(str(get(p,"embedded_type")),"stop_point"))continue;
  const cJSON *stop=get(p,"stop_point");const char *id=str(get(stop,"id"));
  if(strncmp(id,"stop_point:IDFM:",16))continue;
  const char *raw=id+16;bool monomodal=!strncmp(raw,"monomodalStopPlace:",19)&&digits(raw+19);
  if(!digits(raw)&&!monomodal)continue;
  const char *distance=str(get(p,"distance"));char *end;unsigned long metres=strtoul(distance,&end,10);
  if(!*distance||*end||metres>2000)continue;
  station_transport_stop_t candidate={0};if(monomodal)snprintf(candidate.id,sizeof(candidate.id),"SP:%s",raw+19);else copy(candidate.id,sizeof(candidate.id),raw);
  copy(candidate.name,sizeof(candidate.name),str(get(stop,"name")));
  const cJSON *m;candidate.priority=3;
  cJSON_ArrayForEach(m,get(stop,"commercial_modes")){
   const char *name=str(get(m,"name"));unsigned rank=mode_priority(name);
   if(rank<candidate.priority||!candidate.mode[0]){candidate.priority=rank;copy(candidate.mode,sizeof(candidate.mode),name);}
  }
  candidate.metres=(unsigned)metres;
  const cJSON *coord=get(stop,"coord");
  candidate.coordinates=coordinate(get(coord,"lat"),&candidate.latitude)&&coordinate(get(coord,"lon"),&candidate.longitude)&&fabs(candidate.latitude)<=90&&fabs(candidate.longitude)<=180;
  /* Rail requests use the explicitly documented monomodal stop-place code.
     Never infer a SNCF station code from a quay number or a name. */
  if(candidate.priority<2){
   const cJSON *code;cJSON_ArrayForEach(code,get(stop,"codes")){
    const char *v=str(get(code,"value"));
    if(!strcmp(str(get(code,"type")),"netex_monomodal_stopplace")&&!strncmp(v,"monomodalStopPlace:",19)&&digits(v+19)){
     snprintf(candidate.id,sizeof(candidate.id),"SP:%s",v+19);break;
    }
   }
   if(strncmp(candidate.id,"SP:",3)&&candidate.priority==0)snprintf(candidate.id,sizeof(candidate.id),"SP:%s",raw);
   if(!monomodal&&digits(raw)){copy(candidate.quays[0],sizeof(candidate.quays[0]),raw);candidate.quay_count=1;}
   cJSON_ArrayForEach(code,get(stop,"codes")){
    const char *v=str(get(code,"value"));if(!strcmp(str(get(code,"type")),"netex_zder_quay")&&digits(v)&&candidate.quay_count<16)copy(candidate.quays[candidate.quay_count++],24,v);
   }
  }
  unsigned duplicate=0;for(;duplicate<out->count;duplicate++)if(!strcmp(out->stops[duplicate].id,candidate.id))break;
  if(duplicate<out->count){
   station_transport_stop_t *old=&out->stops[duplicate];bool known=false;
   for(unsigned n=0;n<candidate.quay_count;n++){
    known=false;for(unsigned q=0;q<old->quay_count;q++)if(!strcmp(old->quays[q],candidate.quays[n]))known=true;
    if(!known&&old->quay_count<16)copy(old->quays[old->quay_count++],24,candidate.quays[n]);
   }
   continue;
  }
  unsigned at=out->count;
  while(at&&(out->stops[at-1].priority>candidate.priority||
    (out->stops[at-1].priority==candidate.priority&&out->stops[at-1].metres>candidate.metres)))at--;
  if(at>=TRANSPORT_STOPS)continue;
  unsigned last=out->count<TRANSPORT_STOPS?out->count:TRANSPORT_STOPS-1;
  for(unsigned i=last;i>at;i--)out->stops[i]=out->stops[i-1];
  out->stops[at]=candidate;if(out->count<TRANSPORT_STOPS)out->count++;
 }
 cJSON_Delete(root);return true;
}
bool station_transport_nearby(const char *json,station_transport_snapshot_t *out){bool more;return station_transport_nearby_page(json,out,true,&more);}
static bool stop_matches(const char *reference,const char *stop_id,const station_transport_snapshot_t *out){
 char expected[64];bool area=!strncmp(stop_id,"SP:",3);
 snprintf(expected,sizeof(expected),area?"STIF:StopArea:SP:%s:":"STIF:StopPoint:Q:%s:",area?stop_id+3:stop_id);
 if(!strcmp(reference,expected))return true;
 if(area)for(unsigned i=0;i<out->count;i++)if(!strcmp(out->stops[i].id,stop_id)){
  for(unsigned q=0;q<out->stops[i].quay_count;q++){
   snprintf(expected,sizeof(expected),"STIF:StopPoint:Q:%s:",out->stops[i].quays[q]);if(!strcmp(reference,expected))return true;
  }
 }
 return false;
}
bool station_transport_departures(const char *json,const char *stop_id,time_t now,station_transport_snapshot_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *delivery=get(get(root,"Siri"),"ServiceDelivery"),*items=get(delivery,"StopMonitoringDelivery");
 if(failed(delivery)||!cJSON_IsArray(items)||!cJSON_GetArraySize(items)){cJSON_Delete(root);return false;}
 const cJSON *check;cJSON_ArrayForEach(check,items)if(failed(check)){cJSON_Delete(root);return false;}
 out->row_count=0;
 const cJSON *item; cJSON_ArrayForEach(item,items){
  if(failed(item)){cJSON_Delete(root);return false;}
  const cJSON *visit;cJSON_ArrayForEach(visit,get(item,"MonitoredStopVisit")){
   if(!stop_matches(value(get(visit,"MonitoringRef")),stop_id,out))continue;
   const cJSON *journey=get(visit,"MonitoredVehicleJourney"),*call=get(journey,"MonitoredCall");
   /* Arrival-only visits at the journey terminus are not boardable departures. */
   if(!strcmp(value(get(journey,"DestinationRef")),value(get(visit,"MonitoringRef")))&&
      !station_transport_timestamp(str(get(call,"ExpectedDepartureTime")))&&
      !station_transport_timestamp(str(get(call,"AimedDepartureTime"))))continue;
   const char *line=value(get(journey,"LineRef"));size_t length=strlen(line);
   if(strncmp(line,"STIF:Line::C",12)||length<14||length>28||line[length-1]!=':')continue;
   char id[20];copy(id,sizeof(id),line+11);id[strlen(id)-1]=0;if(!digits(id+1))continue;
   const char *destination=value(get(journey,"DestinationName"));if(!*destination)destination=value(get(journey,"DirectionName"));
   time_t expected_arrival=station_transport_timestamp(str(get(call,"ExpectedArrivalTime")));
   time_t expected_departure=station_transport_timestamp(str(get(call,"ExpectedDepartureTime")));
   time_t aimed_arrival=station_transport_timestamp(str(get(call,"AimedArrivalTime")));
   time_t aimed_departure=station_transport_timestamp(str(get(call,"AimedDepartureTime")));
   bool at_stop=cJSON_IsTrue(get(call,"VehicleAtStop"))||!strcmp(str(get(call,"VehicleAtStop")),"true");
   time_t at=expected_arrival;bool arrival=at!=0,estimated=at!=0;
   if(expected_departure&&(!at||((at_stop||at<now-30)&&expected_departure>=now-30))){at=expected_departure;arrival=false;estimated=true;}
   if(!at){at=aimed_arrival;arrival=at!=0;}
   if(aimed_departure&&(!at||((at_stop||at<now-30)&&aimed_departure>=now-30&&!estimated))){at=aimed_departure;arrival=false;}
   const char *status=str(get(call,arrival?"ArrivalStatus":"DepartureStatus"));
   bool cancelled=!strcmp(status,"cancelled")||!strcmp(str(get(call,"DepartureStatus")),"cancelled");
   time_t aimed=station_transport_timestamp(str(get(call,arrival?"AimedArrivalTime":"AimedDepartureTime")));
   int delay=estimated&&aimed&&at>aimed?(int)((at-aimed)/60):0;
   const char *point=value(get(call,"StopPointRef"));if(!*point)point=value(get(visit,"MonitoringRef"));
   char dest[128];copy(dest,sizeof(dest),destination);
   if(!at||at<now-30||at>now+86400)continue;
   unsigned index=0;for(;index<out->row_count;index++)if(!strcmp(out->rows[index].line,id)&&!strcmp(out->rows[index].destination,dest)&&!strcmp(out->rows[index].point,point))break;
   if(index==out->row_count){if(index>=TRANSPORT_ROWS)continue;station_transport_row_t *r=&out->rows[out->row_count++];memset(r,0,sizeof(*r));copy(r->line,sizeof(r->line),id);copy(r->code,sizeof(r->code),id);copy(r->destination,sizeof(r->destination),destination);copy(r->status,sizeof(r->status),status);r->estimated=estimated;copy(r->point,sizeof(r->point),point);copy(r->pickup,sizeof(r->pickup),value(get(call,"StopPointName")));copy(r->platform,sizeof(r->platform),value(get(call,arrival?"ArrivalPlatformName":"DeparturePlatformName")));if(!r->platform[0])copy(r->platform,sizeof(r->platform),value(get(call,"DeparturePlatformName")));}
   station_transport_row_t *r=&out->rows[index];if(r->at[0]==at||r->at[1]==at)continue;
   unsigned slot=2;
   if(!r->at[0]||at<r->at[0]){
    r->at[1]=r->at[0];r->arrival[1]=r->arrival[0];r->predicted[1]=r->predicted[0];r->cancelled[1]=r->cancelled[0];r->delay_minutes[1]=r->delay_minutes[0];slot=0;
   }else if(!r->at[1]||at<r->at[1])slot=1;
   if(slot<2){r->at[slot]=at;r->arrival[slot]=arrival;r->predicted[slot]=estimated;r->cancelled[slot]=cancelled;r->delay_minutes[slot]=delay;
    if(slot==0){copy(r->platform,sizeof(r->platform),value(get(call,arrival?"ArrivalPlatformName":"DeparturePlatformName")));if(!r->platform[0])copy(r->platform,sizeof(r->platform),value(get(call,"DeparturePlatformName")));r->estimated=estimated;copy(r->status,sizeof(r->status),status);}}

  }
 }
 for(unsigned i=1;i<out->row_count;i++){station_transport_row_t r=out->rows[i];unsigned k=i;while(k&&out->rows[k-1].at[0]>r.at[0]){out->rows[k]=out->rows[k-1];k--;}out->rows[k]=r;}
 out->valid=true;out->fetched_at=now;cJSON_Delete(root);return true;
}
bool station_transport_line(const char *json,const char *line,char *code,size_t size){
 cJSON *root=cJSON_Parse(json);char wanted[40];snprintf(wanted,sizeof(wanted),"line:IDFM:%s",line);bool ok=false;
 const cJSON *item;cJSON_ArrayForEach(item,get(root,"lines")){if(!strcmp(str(get(item,"id")),wanted)&&*str(get(item,"code"))){copy(code,size,str(get(item,"code")));ok=true;break;}}
 cJSON_Delete(root);return ok;
}
/* Disruption links in the returned line report establish relevance, including its stops. */
static bool references(const cJSON *j,const char *id,unsigned depth){
 if(!j||depth>20)return false;
 if(cJSON_IsObject(j)&&!strcmp(str(get(j,"type")),"disruption")&&!strcmp(str(get(j,"id")),id))return true;
 const cJSON *v; cJSON_ArrayForEach(v,j){if(v->child&&references(v,id,depth+1))return true;}
 return false;
}
bool station_transport_traffic(const char *json,const char *line,station_transport_snapshot_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *reports=get(root,"line_reports"),*disruptions=get(root,"disruptions"),*report=NULL,*r;
 if(!cJSON_IsArray(reports)||!cJSON_IsArray(disruptions)){cJSON_Delete(root);return false;}
 char wanted[40];snprintf(wanted,sizeof(wanted),"line:IDFM:%s",line);
 cJSON_ArrayForEach(r,reports)if(!strcmp(str(get(get(r,"line"),"id")),wanted)){report=r;break;}
 if(!report){cJSON_Delete(root);return false;}
 cJSON_ArrayForEach(r,disruptions){if(strcmp(str(get(r,"status")),"active")||!references(report,str(get(r,"id")),0))continue;
  const cJSON *m;const char *title=NULL;cJSON_ArrayForEach(m,get(r,"messages")){
   const cJSON *channel=get(m,"channel");if(!strcmp(str(get(channel,"content_type")),"text/plain")){title=str(get(m,"text"));if(!strcmp(str(get(channel,"name")),"titre"))break;}}
  if(!title||!*title)title=str(get(get(r,"severity"),"name"));
  if(out->alert_count<TRANSPORT_ALERTS){char short_title[161];copy(short_title,sizeof(short_title),title);const char *display=line;for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].line,line)){display=out->rows[i].code;break;}snprintf(out->alerts[out->alert_count++],192,"%s: %s",display,short_title);}else out->traffic_partial=true;
 }
 cJSON_Delete(root);return true;
}

static unsigned distance_metres(double a,double b,double c,double d){
 double dy=(c-a)*111320,dx=(d-b)*111320*cos(a*0.0174532925199433);double n=hypot(dx,dy);return n>40000000?40000000:(unsigned)n;
}
bool station_transport_search_parse(const char *json,double lat,double lon,station_transport_search_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *places=get(root,"places");bool children=false;if(!places){places=get(root,"stop_points");children=true;}if(!cJSON_IsArray(places)){cJSON_Delete(root);return false;}out->count=0;
 const cJSON *p;cJSON_ArrayForEach(p,places){
  const char *type=children?"stop_point":str(get(p,"embedded_type"));bool area=!strcmp(type,"stop_area");if(!area&&strcmp(type,"stop_point"))continue;
  const cJSON *obj=children?p:get(p,type);bool complex=area;const char *id=str(get(obj,"id"));const char *number=NULL;
  if(area&&!strncmp(id,"stop_area:IDFM:",15)&&digits(id+15))number=id+15;
  else if(!area&&!strncmp(id,"stop_point:IDFM:monomodalStopPlace:",35)&&digits(id+35)){number=id+35;area=true;}
  else if(!area&&!strncmp(id,"stop_point:IDFM:",16)&&digits(id+16))number=id+16;
  if(!number)continue;
  station_transport_stop_t stop={0};if(area)snprintf(stop.id,sizeof(stop.id),complex?"SA:%s":"SP:%s",number);else copy(stop.id,sizeof(stop.id),number);
  const char *name=str(get(obj,"label"));if(!*name)name=str(get(obj,"name"));copy(stop.name,sizeof(stop.name),name);
  const cJSON *coord=get(obj,"coord");stop.coordinates=coordinate(get(coord,"lat"),&stop.latitude)&&coordinate(get(coord,"lon"),&stop.longitude)&&fabs(stop.latitude)<=90&&fabs(stop.longitude)<=180;
  stop.metres=stop.coordinates?distance_metres(lat,lon,stop.latitude,stop.longitude):0;stop.priority=3;
  const cJSON *m;cJSON_ArrayForEach(m,get(obj,"commercial_modes")){const char *mode=str(get(m,"name"));unsigned priority=mode_priority(mode);if(priority<stop.priority||!stop.mode[0]){stop.priority=priority;copy(stop.mode,sizeof(stop.mode),mode);}}
  bool duplicate=false;for(unsigned i=0;i<out->count;i++)if(!strcmp(out->stops[i].id,stop.id))duplicate=true;if(duplicate)continue;
  unsigned at=out->count;while(at&&(out->stops[at-1].priority>stop.priority||(out->stops[at-1].priority==stop.priority&&out->stops[at-1].metres>stop.metres)))at--;
  if(at>=TRANSPORT_SEARCH_STOPS)continue;
  unsigned last=out->count<TRANSPORT_SEARCH_STOPS?out->count:TRANSPORT_SEARCH_STOPS-1;for(unsigned i=last;i>at;i--){out->stops[i]=out->stops[i-1];copy(out->services[i],128,out->services[i-1]);}out->stops[at]=stop;out->services[at][0]=0;const cJSON *line;cJSON_ArrayForEach(line,get(obj,"lines")){const char *code=str(get(line,"code"));size_t used=strlen(out->services[at]);if(*code&&used+strlen(code)+3<128)snprintf(out->services[at]+used,128-used,"%s%s",used?", ":"",code);}if(out->count<TRANSPORT_SEARCH_STOPS)out->count++;
 }
 const cJSON *total=get(get(root,"pagination"),"total_result");out->partial=cJSON_IsNumber(total)&&total->valueint>(int)out->count;
 cJSON_Delete(root);return true;
}
bool station_transport_line_stops_parse(const char *json,station_transport_line_map_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *stops=get(root,"stop_points");if(!cJSON_IsArray(stops)){cJSON_Delete(root);return false;}out->count=0;
 const cJSON *p;cJSON_ArrayForEach(p,stops){
  if(out->count==TRANSPORT_LINE_STOPS){out->partial=true;break;}
  const char *id=str(get(p,"id"));if(strncmp(id,"stop_point:IDFM:",16))continue;const char *number=id+16;bool area=!strncmp(number,"monomodalStopPlace:",19);if(area)number+=19;if(!digits(number))continue;
  station_line_stop_t *stop=&out->stops[out->count++];memset(stop,0,sizeof(*stop));if(area)snprintf(stop->id,sizeof(stop->id),"SP:%s",number);else copy(stop->id,sizeof(stop->id),number);
  copy(stop->name,sizeof(stop->name),str(get(p,"name")));const cJSON *coord=get(p,"coord");stop->coordinates=coordinate(get(coord,"lat"),&stop->latitude)&&coordinate(get(coord,"lon"),&stop->longitude)&&fabs(stop->latitude)<=90&&fabs(stop->longitude)<=180;
  copy(stop->mode,sizeof(stop->mode),str(get(cJSON_GetArrayItem(get(p,"commercial_modes"),0),"name")));
 }
 const cJSON *total=get(get(root,"pagination"),"total_result");if(cJSON_IsNumber(total)&&total->valueint>(int)out->count)out->partial=true;
 cJSON_Delete(root);return true;
}

/* Schedule table rows are ordered by the provider; never sort them alphabetically. */
bool station_transport_routes_parse(const char *json,station_transport_line_map_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *schedules=get(root,"route_schedules");
 if(!cJSON_IsArray(schedules)){cJSON_Delete(root);return false;}
 out->count=out->route_count=0;out->partial=false;out->color=0x5091cb;
 const cJSON *schedule;cJSON_ArrayForEach(schedule,schedules){
  if(out->route_count==16){out->partial=true;break;}
  station_line_route_t *route=&out->routes[out->route_count];memset(route,0,sizeof(*route));
  const cJSON *display=get(schedule,"display_informations"),*rows=get(get(schedule,"table"),"rows"),*row;
  const char *color=str(get(display,"color"));if(strlen(color)==6){char *end;unsigned long value=strtoul(color,&end,16);if(!*end)out->color=(uint32_t)value;}
  cJSON_ArrayForEach(row,rows){
   const cJSON *p=get(row,"stop_point");const char *id=str(get(p,"id"));if(strncmp(id,"stop_point:IDFM:",16)){out->partial=true;continue;}
   const char *number=id+16;bool area=!strncmp(number,"monomodalStopPlace:",19);if(area)number+=19;if(!digits(number)){out->partial=true;continue;}
   char key[24];snprintf(key,sizeof(key),area?"SP:%s":"%s",number);unsigned index=0;while(index<out->count&&strcmp(out->stops[index].id,key))index++;
   if(index==out->count){if(out->count==TRANSPORT_LINE_STOPS){out->partial=true;continue;}station_line_stop_t *stop=&out->stops[out->count++];memset(stop,0,sizeof(*stop));copy(stop->id,sizeof(stop->id),key);copy(stop->name,sizeof(stop->name),str(get(p,"name")));copy(stop->mode,sizeof(stop->mode),str(get(display,"commercial_mode")));const char *parent=str(get(get(p,"stop_area"),"id"));if(!strncmp(parent,"stop_area:IDFM:",15)&&digits(parent+15))snprintf(stop->area,sizeof(stop->area),"SA:%s",parent+15);const cJSON *coord=get(p,"coord");stop->coordinates=coordinate(get(coord,"lat"),&stop->latitude)&&coordinate(get(coord,"lon"),&stop->longitude);}
   if(route->count&&route->stops[route->count-1]==index)continue;
   if(route->count==TRANSPORT_LINE_STOPS){out->partial=true;break;}route->stops[route->count++]=(uint8_t)index;
  }
  if(route->count>1){snprintf(route->name,sizeof(route->name),"%s > %s",out->stops[route->stops[0]].name,out->stops[route->stops[route->count-1]].name);out->route_count++;}
 }
 const cJSON *total=get(get(root,"pagination"),"total_result");if(cJSON_IsNumber(total)&&total->valueint>cJSON_GetArraySize(schedules))out->partial=true;
 cJSON_Delete(root);return out->route_count>0;
}

/* Parent station membership, not proximity or matching names, defines interchanges. */
bool station_transport_interchanges_parse(const char *json,station_transport_line_map_t *out){
 cJSON *root=cJSON_Parse(json);const cJSON *areas=get(root,"stop_areas"),*area;
 if(!cJSON_IsArray(areas)){cJSON_Delete(root);return false;}
 cJSON_ArrayForEach(area,areas){const char *id=str(get(area,"id"));if(strncmp(id,"stop_area:IDFM:",15))continue;
  for(unsigned i=0;i<out->count;i++){station_line_stop_t *stop=&out->stops[i];if(strncmp(stop->area,"SA:",3)||strcmp(stop->area+3,id+15))continue;
   stop->connections[0]=0;stop->connections_known=true;
   /* Keep rail first while retaining all provider modes, including tram and bus. */
   for(unsigned priority=0;priority<4;priority++){const cJSON *line;cJSON_ArrayForEach(line,get(area,"lines")){
    const char *mode=str(get(get(line,"commercial_mode"),"name")),*code=str(get(line,"code")),*key=str(get(line,"id"));
    if(mode_priority(mode)!=priority||!*code||(!strncmp(key,"line:IDFM:",10)&&!strcmp(key+10,out->line)))continue;
    size_t used=strlen(stop->connections);if(used+strlen(mode)+strlen(code)+5>=sizeof(stop->connections)){out->partial=true;continue;}
    snprintf(stop->connections+used,sizeof(stop->connections)-used,"%s%s %s",used?" · ":"",mode,code);
   }}
  }
 }
 bool complete=true;for(unsigned i=0;i<out->count;i++)if(!out->stops[i].connections_known)complete=false;
 cJSON_Delete(root);return complete;
}
