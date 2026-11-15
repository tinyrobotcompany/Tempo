#include "transport_memory.h"
#include "transport_service.h"
#include "transport_config.h"
#include "transport_json_filter.h"
#include "network_time.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if !defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3) || !defined(CONFIG_MBEDTLS_SSL_TLS1_3_KEXM_EPHEMERAL)
#error "PRIM requires TLS 1.3 with certificate-based ephemeral key exchange; enable it in sdkconfig"
#endif
#if !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
#error "PRIM requires cross-signed certificate verification with the full trusted bundle"
#endif
#endif
#define BODY_LIMIT (128*1024)
#define BASE "https://prim.iledefrance-mobilites.fr/marketplace/"
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static STATION_TRANSPORT_DATA station_transport_snapshot_t snapshot={.state=TRANS_LOCATION};
static bool requested,search_pending,line_pending,rediscover,station_override;
static STATION_TRANSPORT_DATA station_transport_search_t search_result;
static char connection_area[24];
static STATION_TRANSPORT_DATA station_transport_line_map_t line_result;
static int64_t search_at=-10000000;

static unsigned selection,command_generation;
static unsigned command_ticket(void){portENTER_CRITICAL(&mux);unsigned n=command_generation;portEXIT_CRITICAL(&mux);return n;}
static bool superseded(unsigned n){return command_ticket()!=n;}

static int64_t last_attempt=-120000000;
/* All provider work lives on this task. No HTTP/JSON work occurs under the UI lock. */
static unsigned requests;
static int64_t budget_start,blocked_until;
static station_transport_state_t fetch_failure=TRANS_RETRY;
static struct {char line[20],code[20];} line_cache[16];
static unsigned cached_lines;
static char *fetch(const char *url){
 unsigned ticket=command_ticket();
 int64_t now=esp_timer_get_time();
 if(now-budget_start>=86400000000LL){requests=0;budget_start=now;}
 if(now<blocked_until||requests>=900){fetch_failure=TRANS_QUOTA;return NULL;}
 requests++;
 fetch_failure=TRANS_RETRY;
 bool traffic=strstr(url,"/line_reports")!=NULL,network_data=strstr(url,"/navitia/")!=NULL&&!traffic;
 bool siri=strstr(url,"/stop-monitoring")!=NULL;
 bool project=traffic||network_data||siri;
 size_t capacity=siri?512*1024:project?256*1024:BODY_LIMIT,input_limit=project?8*1024*1024:BODY_LIMIT;
 char *body=malloc(capacity+1);if(!body){fetch_failure=TRANS_RETRY;return NULL;}
 esp_http_client_config_t config={.url=url,.timeout_ms=6000,.buffer_size=2048,.buffer_size_tx=1536,
  .tls_version=ESP_HTTP_CLIENT_TLS_VER_TLS_1_3,
  .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
 esp_http_client_handle_t client=esp_http_client_init(&config);if(!client){free(body);return NULL;}
 esp_http_client_set_header(client,"apikey",STATION_PRIM_API_KEY);
 esp_http_client_set_header(client,"Accept","application/json");esp_http_client_set_header(client,"Accept-Encoding","identity");
 esp_err_t err=esp_http_client_open(client,0);int http=0;size_t used=0;bool complete=false,oversized=false;
 if(err==ESP_OK){int64_t length=esp_http_client_fetch_headers(client);http=esp_http_client_get_status_code(client);
  esp_http_client_set_timeout_ms(client,2000); /* Bound cancellation latency between body reads. */
  if(http==200&&length>=0&&(uint64_t)length<=input_limit&&project){
   char *chunk=malloc(4096);transport_json_filter_t *filter=siri?transport_json_filter_create_siri(body,capacity+1):network_data?transport_json_filter_create_network(body,capacity+1):transport_json_filter_create(body,capacity+1);
   size_t received=0;bool parsed=chunk&&filter;
   while(parsed&&received<input_limit&&esp_timer_get_time()-now<20000000&&!superseded(ticket)){
    int n=esp_http_client_read(client,chunk,(int)((input_limit-received)>4096?4096:input_limit-received));
    if(n<0){parsed=false;break;}
    if(!n)break;
    received+=(size_t)n;parsed=transport_json_filter_feed(filter,chunk,(size_t)n);
   }
   complete=parsed&&esp_http_client_is_complete_data_received(client)&&transport_json_filter_finish(filter,&used);
   oversized=received==input_limit&&!esp_http_client_is_complete_data_received(client);
   ESP_LOGI("transport","%s stream: %u input bytes, %u retained",siri?"Departures":network_data?"Discovery/metadata":"Disruptions",(unsigned)received,(unsigned)used);
   transport_json_filter_free(filter);free(chunk);
  }else if(http==200&&length>=0&&length<=BODY_LIMIT&&!project){
   while(used<BODY_LIMIT&&esp_timer_get_time()-now<20000000&&!superseded(ticket)){
    int n=esp_http_client_read(client,body+used,(int)((BODY_LIMIT-used)>4096?4096:BODY_LIMIT-used));
    if(n<0)break;
    if(n==0){complete=esp_http_client_is_complete_data_received(client);break;}used+=(size_t)n;
   }
   if(esp_http_client_is_complete_data_received(client))complete=true;
   if(used==BODY_LIMIT&&!complete)oversized=true;
  }else if(length>(int64_t)input_limit)oversized=true;
 }
 if(superseded(ticket))complete=false;
 if(http==401||http==403)fetch_failure=TRANS_DENIED;
 else if(http==429){fetch_failure=TRANS_QUOTA;blocked_until=esp_timer_get_time()+900000000LL;}
 else fetch_failure=TRANS_RETRY;
 ESP_LOGI("transport","%s HTTP %d, %u bytes, complete=%d, oversized=%d, elapsed=%lld ms",siri?"Departures":network_data?"Discovery/metadata":traffic?"Disruptions":"Request",http,(unsigned)used,complete,oversized,(long long)((esp_timer_get_time()-now)/1000));
 esp_http_client_close(client);esp_http_client_cleanup(client);
 if(!complete||http!=200||oversized){free(body);return NULL;}body[used]=0;return body;
}
void station_transport_status(station_transport_snapshot_t *out){portENTER_CRITICAL(&mux);*out=snapshot;portEXIT_CRITICAL(&mux);}
bool station_transport_select(unsigned index){
 portENTER_CRITICAL(&mux);bool ok=index<snapshot.count;
 if(ok&&selection!=index){selection=index;requested=true;command_generation++;snapshot.selected=index;snapshot.selection_generation++;snapshot.discovering=false;snapshot.valid=false;snapshot.row_count=0;snapshot.alert_count=0;snapshot.traffic_valid=false;snapshot.state=TRANS_LOADING;snapshot.revision++;}
 portEXIT_CRITICAL(&mux);return ok;
}
bool station_transport_refresh(void){portENTER_CRITICAL(&mux);bool ok=esp_timer_get_time()-last_attempt>=10000000;if(ok){requested=true;command_generation++;}portEXIT_CRITICAL(&mux);return ok;}
static station_transport_stop_t nearby_stops[TRANSPORT_STOPS];
static unsigned nearby_count,nearby_generation;
static void choose_station(const station_transport_stop_t *stop){
 if(!station_override&&snapshot.count){nearby_count=snapshot.count;nearby_generation=snapshot.generation;memcpy(nearby_stops,snapshot.stops,sizeof(nearby_stops));}
 command_generation++;station_override=true;snapshot.stops[0]=*stop;snapshot.count=1;selection=0;snapshot.selected=0;snapshot.selection_generation++;
 snapshot.discovering=false;snapshot.valid=false;snapshot.row_count=0;snapshot.alert_count=0;snapshot.traffic_valid=false;snapshot.state=TRANS_LOADING;requested=true;snapshot.revision++;
}
bool station_transport_search(const char *query){
 if(!query||strlen(query)<2||strlen(query)>63)return false;
 for(const unsigned char *p=(const unsigned char *)query;*p;p++)if(*p<32||*p==127)return false;
 int64_t now=esp_timer_get_time();portENTER_CRITICAL(&mux);bool ok=now-search_at>=2000000;
 if(ok){connection_area[0]=0;search_result.connections=false;command_generation++;search_at=now;snprintf(search_result.query,sizeof(search_result.query),"%s",query);search_result.count=0;search_result.state=LOOKUP_LOADING;search_result.revision++;search_pending=true;}
 portEXIT_CRITICAL(&mux);return ok;
}
void station_transport_search_status(station_transport_search_t *out){portENTER_CRITICAL(&mux);*out=search_result;portEXIT_CRITICAL(&mux);}
bool station_transport_search_select(unsigned index){portENTER_CRITICAL(&mux);bool ok=search_result.state==LOOKUP_READY&&index<search_result.count;if(ok)choose_station(&search_result.stops[index]);portEXIT_CRITICAL(&mux);return ok;}
bool station_transport_line_map(const char *line){
 if(!line||line[0]!='C'||strlen(line)>18||strlen(line)<2)return false;
 for(const char *p=line+1;*p;p++)if(*p<'0'||*p>'9')return false;
 portENTER_CRITICAL(&mux);
 if(strcmp(line_result.line,line)||line_result.state==LOOKUP_FAILED||line_result.connections_failed){command_generation++;snprintf(line_result.line,20,"%s",line);line_result.count=0;line_result.connections_failed=false;line_result.connections_loading=false;line_result.state=LOOKUP_LOADING;line_result.revision++;line_pending=true;}
 portEXIT_CRITICAL(&mux);return true;
}
void station_transport_line_map_status(station_transport_line_map_t *out){portENTER_CRITICAL(&mux);*out=line_result;portEXIT_CRITICAL(&mux);}
bool station_transport_line_stop_select(unsigned index){
 portENTER_CRITICAL(&mux);bool ok=line_result.state==LOOKUP_READY&&index<line_result.count;
 if(ok){const station_line_stop_t *p=&line_result.stops[index];
  command_generation++;search_result.revision++;search_result.count=0;search_result.partial=false;search_result.connections=true;snprintf(search_result.query,sizeof(search_result.query),"%s",p->name);
  snprintf(connection_area,sizeof(connection_area),"%s",p->area);search_pending=connection_area[0]!=0;
  search_result.state=search_pending?LOOKUP_LOADING:LOOKUP_FAILED;
 }
 portEXIT_CRITICAL(&mux);return ok;
}
void station_transport_nearby_reset(void){
 station_network_status_t home;station_network_status(&home);
 portENTER_CRITICAL(&mux);
 if(station_override){snapshot.count=0;if(nearby_count&&nearby_generation==home.location_generation){snapshot.count=nearby_count;memcpy(snapshot.stops,nearby_stops,sizeof(nearby_stops));snapshot.generation=nearby_generation;}}
 if(snapshot.generation!=home.location_generation)snapshot.count=0;
 rediscover=snapshot.count==0;station_override=false;selection=snapshot.selected=0;command_generation++;snapshot.selection_generation++;snapshot.revision++;
 snapshot.valid=false;snapshot.row_count=0;snapshot.alert_count=0;snapshot.traffic_valid=false;snapshot.discovering=false;snapshot.state=TRANS_LOADING;requested=true;
 portEXIT_CRITICAL(&mux);
}
static void encode_query(char *out,size_t capacity,const char *query){
 static const char hex[]="0123456789ABCDEF";size_t n=0;
 for(const unsigned char *p=(const unsigned char *)query;*p&&n+3<capacity;p++){
  if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_')out[n++]=(char)*p;
  else {out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
 }out[n]=0;
}
static void lookups(const station_network_status_t *home){
 char query[96],line[20],area[24];unsigned sr,lr;bool search,map;
 portENTER_CRITICAL(&mux);search=search_pending;map=line_pending;search_pending=false;line_pending=false;sr=search_result.revision;lr=line_result.revision;snprintf(query,sizeof(query),"%s",search_result.query);snprintf(area,sizeof(area),"%s",connection_area);snprintf(line,20,"%s",line_result.line);portEXIT_CRITICAL(&mux);
 if(search){
  station_transport_search_t *result=calloc(1,sizeof(*result));if(result){char encoded[288],url[512];encode_query(encoded,sizeof(encoded),query);
   snprintf(url,sizeof(url),BASE "v2/navitia/places?q=%s&type%%5B%%5D=stop_area&type%%5B%%5D=stop_point&count=8&depth=2&disable_geojson=true&disable_disruption=true",encoded);
   if(area[0])snprintf(url,sizeof(url),BASE "v2/navitia/stop_areas/stop_area%%3AIDFM%%3A%s/stop_points?count=80&depth=3&disable_geojson=true&disable_disruption=true",area+3);
   char *body=fetch(url);bool ok=body&&station_transport_search_parse(body,home->latitude,home->longitude,result);free(body);
   portENTER_CRITICAL(&mux);if(search_result.revision==sr){result->connections=area[0]!=0;snprintf(result->query,sizeof(result->query),"%s",query);result->state=ok?LOOKUP_READY:LOOKUP_FAILED;result->revision=sr+1;search_result=*result;}portEXIT_CRITICAL(&mux);free(result);
  }else {portENTER_CRITICAL(&mux);if(search_result.revision==sr){search_result.state=LOOKUP_FAILED;search_result.revision++;}portEXIT_CRITICAL(&mux);}
 }
 if(map){
  station_transport_line_map_t *result=calloc(1,sizeof(*result));if(result){char url[512];snprintf(url,sizeof(url),BASE "v2/navitia/lines/line%%3AIDFM%%3A%s/route_schedules?count=16&depth=1&disable_geojson=true&disable_disruption=true&duration=60",line);
   char *body=fetch(url);bool ok=body&&station_transport_routes_parse(body,result);free(body);
   snprintf(result->line,20,"%s",line);result->state=ok?LOOKUP_READY:LOOKUP_FAILED;result->connections_loading=ok;result->revision=lr+1;
   portENTER_CRITICAL(&mux);if(line_result.revision==lr)line_result=*result;portEXIT_CRITICAL(&mux);
   /* Show ordered stations immediately; enrich all interchanges in one request. */
   if(ok){unsigned ticket=command_ticket();snprintf(url,sizeof(url),BASE "v2/navitia/lines/line%%3AIDFM%%3A%s/stop_areas?count=80&depth=3&disable_geojson=true&disable_disruption=true",line);
    body=fetch(url);bool enriched=body&&station_transport_interchanges_parse(body,result);free(body);
    result->connections_loading=false;result->connections_failed=!enriched;result->revision++;
    portENTER_CRITICAL(&mux);if(command_generation==ticket&&line_result.revision==lr+1)line_result=*result;else if(line_result.revision==lr+1){line_result.connections_loading=false;line_result.connections_failed=true;line_result.revision++;}portEXIT_CRITICAL(&mux);
   }free(result);
  }else {portENTER_CRITICAL(&mux);if(line_result.revision==lr){line_result.state=LOOKUP_FAILED;line_result.revision++;}portEXIT_CRITICAL(&mux);}
 }
}
static void publish(station_transport_snapshot_t *data){
 station_network_status_t current;station_network_status(&current);
 portENTER_CRITICAL(&mux);
 bool accepted=current.location_generation==data->generation&&selection==data->selected&&snapshot.selection_generation==data->selection_generation;
 if(accepted){data->revision=snapshot.revision+1;snapshot=*data;}
 portEXIT_CRITICAL(&mux);
 ESP_LOGI("transport","Publish %s: station=%s rows=%u valid=%d revision=%u",accepted?"accepted":"superseded",data->selected<data->count?data->stops[data->selected].id:"none",data->row_count,data->valid,data->revision);
}
static void state(station_transport_state_t next){portENTER_CRITICAL(&mux);if(snapshot.state!=next){snapshot.state=next;snapshot.revision++;}portEXIT_CRITICAL(&mux);}
static bool load_departures(station_transport_snapshot_t *work,unsigned selected){
 const char *id=work->stops[selected].id;bool area=!strncmp(id,"SP:",3);char url[256];
 snprintf(url,sizeof(url),BASE "stop-monitoring?MonitoringRef=STIF%%3A%s%%3A%s%%3A%s%%3A",area?"StopArea":"StopPoint",area?"SP":"Q",area?id+3:id);
 char *body=fetch(url);time_t now=time(NULL);bool ok=body&&station_transport_departures(body,id,now,work);free(body);
 ESP_LOGI("transport","Parsed departures: station=%s member_quays=%u ok=%d rows=%u utc=%lld",id,work->stops[selected].quay_count,ok,work->row_count,(long long)now);return ok;
}
/* Independent nearby overview: selected-station detail remains untouched. */
static STATION_TRANSPORT_DATA station_transport_snapshot_t summary_work;
static STATION_TRANSPORT_DATA station_transport_summary_t summary_pending;
static STATION_TRANSPORT_DATA station_transport_stop_t summary_targets[3];
static bool append_summary(const station_transport_row_t *row,const station_transport_stop_t *stop,time_t fetched){
 if(summary_pending.count>=3)return false;
 for(unsigned i=0;i<summary_pending.count;i++)if(!strcmp(summary_pending.stop_ids[i],stop->id)&&!strcmp(summary_pending.rows[i].line,row->line)&&!strcmp(summary_pending.rows[i].destination,row->destination)&&!strcmp(summary_pending.rows[i].point,row->point))return false;
 unsigned i=summary_pending.count++;summary_pending.rows[i]=*row;
 snprintf(summary_pending.stops[i],96,"%s",stop->name);snprintf(summary_pending.stop_ids[i],24,"%s",stop->id);summary_pending.fetched[i]=fetched;
 return true;
}
static void summary_line_code(station_transport_row_t *row){
 for(unsigned i=0;i<cached_lines;i++)if(!strcmp(line_cache[i].line,row->line)){snprintf(row->code,20,"%s",line_cache[i].code);return;}
 if(strcmp(row->code,row->line))return;
 char url[256],code[20]="";snprintf(url,sizeof(url),BASE "v2/navitia/lines/line%%3AIDFM%%3A%s?depth=0&disable_geojson=true&disable_disruption=true",row->line);
 char *body=fetch(url);bool ok=body&&station_transport_line(body,row->line,code,sizeof(code));free(body);
 if(ok){snprintf(row->code,20,"%s",code);if(cached_lines<16){snprintf(line_cache[cached_lines].line,20,"%s",row->line);snprintf(line_cache[cached_lines++].code,20,"%s",code);}}
 else snprintf(row->code,20,"?");
}
static unsigned collect_summary(station_transport_snapshot_t *work,unsigned selected,unsigned operation,bool override){
 unsigned targets=0,extra=0;bool selected_used=false;
 portENTER_CRITICAL(&mux);
 const station_transport_stop_t *source=override?nearby_stops:work->stops;
 unsigned count=override?(nearby_generation==work->generation?nearby_count:0):work->count;
 targets=count<3?count:3;memcpy(summary_targets,source,targets*sizeof(*source));
 portEXIT_CRITICAL(&mux);
 if(!targets)return 0;
 memset(&summary_pending,0,sizeof(summary_pending));work->summary.loading=true;publish(work);
 for(unsigned i=0;i<targets;i++){
  bool reuse=selected<work->count&&!strcmp(summary_targets[i].id,work->stops[selected].id)&&work->valid;
  station_transport_snapshot_t *data=work;bool ok=true;
  if(!reuse){memset(&summary_work,0,sizeof(summary_work));summary_work.count=1;summary_work.stops[0]=summary_targets[i];data=&summary_work;extra++;ok=load_departures(data,0);}
  else selected_used=true;
  if(superseded(operation))return extra;
  if(ok&&data->row_count){station_transport_row_t row=data->rows[0];summary_line_code(&row);if(superseded(operation))return extra;append_summary(&row,&summary_targets[i],data->fetched_at);}
  else if(!ok){
   summary_pending.partial=true;
   /* Keep a prior result explicitly timestamped rather than blanking the card. */
   for(unsigned j=0;j<work->summary.count;j++)if(!strcmp(work->summary.stop_ids[j],summary_targets[i].id))append_summary(&work->summary.rows[j],&summary_targets[i],work->summary.fetched[j]);
  }
 }
 /* If fewer nearby stations have predictions, retain distinct directions at the selected nearby stop. */
 if(selected_used)for(unsigned i=0;i<work->row_count&&summary_pending.count<3;i++){station_transport_row_t row=work->rows[i];summary_line_code(&row);if(superseded(operation))return extra;append_summary(&row,&work->stops[selected],work->fetched_at);}
 work->summary=summary_pending;publish(work);
 ESP_LOGI("transport","Nearby summary: stations=%u rows=%u partial=%d",targets,work->summary.count,work->summary.partial);
 return extra;
}
static void worker(void *unused){
 (void)unused;unsigned generation=UINT32_MAX,failures=0;int64_t discover_at=0,next=0;bool was_ready=false;
 station_transport_snapshot_t *work=calloc(1,sizeof(*work));if(!work){state(TRANS_RETRY);vTaskDelete(NULL);return;}
 for(;;){
  station_network_status_t home;station_network_status(&home);int64_t now=esp_timer_get_time();
  bool ready=home.location_valid&&home.connected&&home.source!=TIME_UNSET;
  if(ready&&!was_ready)next=0;
  was_ready=ready;
  if(generation!=home.location_generation){generation=home.location_generation;discover_at=0;next=0;failures=0;
   portENTER_CRITICAL(&mux);memset(&snapshot,0,sizeof(snapshot));snapshot.generation=generation;snapshot.state=TRANS_LOCATION;selection=0;requested=false;station_override=false;portEXIT_CRITICAL(&mux);
  }
  if(!STATION_PRIM_API_KEY[0])state(TRANS_KEY);
  else if(!home.location_valid)state(TRANS_LOCATION);
  else if(!home.connected)state(TRANS_WIFI);
  else if(home.source==TIME_UNSET)state(TRANS_TIME);
  else{
   lookups(&home);
   portENTER_CRITICAL(&mux);if(rediscover){discover_at=0;next=0;rediscover=false;}unsigned operation=command_generation;bool override=station_override;bool ask=requested;unsigned selected=selection;*work=snapshot;portEXIT_CRITICAL(&mux);
   if(now>=next||ask){
    portENTER_CRITICAL(&mux);requested=false;last_attempt=now;portEXIT_CRITICAL(&mux);
    state(TRANS_LOADING);char url[768];bool ok=true,preview=false,preview_attempted=false;unsigned summary_requests=0;
    if(!override&&(now>=discover_at||!work->count||discover_at==0)){
     bool first_load=!work->valid;char previous_stop[24]="";
     if(selected<work->count)snprintf(previous_stop,sizeof(previous_stop),"%s",work->stops[selected].id);
     /* Scan the radius before applying the display limit: nearby bus stops
        must not hide RER/metro on later pages. Bound every page and total work. */
     work->count=0;work->valid=false;work->row_count=0;work->traffic_valid=false;work->alert_count=0;work->discovery_partial=false;bool more=true;int64_t discovery_started=esp_timer_get_time();
     const char *filters[]={"&filter=commercial_mode.id%3Dcommercial_mode%3ARapidTransit","&filter=commercial_mode.id%3Dcommercial_mode%3AMetro",""};
     for(unsigned category=0;category<3;category++){
      more=true;
      for(unsigned page=0;page<8&&more;page++){
       snprintf(url,sizeof(url),BASE "v2/navitia/coord/%.6f;%.6f/places_nearby?type%%5B%%5D=stop_point&count=16&start_page=%u&distance=2000&depth=2&disable_geojson=true&disable_disruption=true%s",home.longitude,home.latitude,page,filters[category]);
       char *body=fetch(url);bool parsed=body&&station_transport_nearby_page(body,work,false,&more);free(body);
       if(superseded(operation)){ok=false;break;}
       if(!parsed){work->discovery_partial=true;break;}
       if(category==2&&work->count>=TRANSPORT_STOPS){more=false;break;}
       if(more&&(page==7||esp_timer_get_time()-discovery_started>=90000000)){work->discovery_partial=true;break;}
       station_network_status_t latest;station_network_status(&latest);
       if(latest.location_generation!=generation){ok=false;break;}
      }
      if(!ok||superseded(operation))break;
      /* Publish the highest-priority available station before looking for other modes. */
      if(first_load&&!preview_attempted&&work->count&&selected==0){
       preview_attempted=true;
       work->selected=0;
       if(load_departures(work,0)){
        preview=true;snprintf(previous_stop,sizeof(previous_stop),"%s",work->stops[0].id);
        work->state=TRANS_READY;work->discovering=true;publish(work);
       }
      }
      if(esp_timer_get_time()-discovery_started>=90000000)break;
     }
     work->discovering=false;
     if(!work->count&&work->discovery_partial)ok=false;

     if(ok){
      ESP_LOGI("transport","Discovery: %u stations, partial=%d",work->count,work->discovery_partial);
      discover_at=esp_timer_get_time()+(work->discovery_partial?300000000LL:21600000000LL);
      unsigned retained=0;bool found=false;
      for(unsigned i=0;i<work->count;i++)if(!strcmp(previous_stop,work->stops[i].id)){retained=i;found=true;break;}
      if(!found){work->valid=false;work->row_count=0;work->traffic_valid=false;work->alert_count=0;}
      portENTER_CRITICAL(&mux);
      /* Do not replace a selection made while discovery was in flight. */
      if(selection==selected&&snapshot.selection_generation==work->selection_generation)selection=retained;
      else requested=true;
      portEXIT_CRITICAL(&mux);
      selected=retained;work->selected=retained;
     }
    }
    if(superseded(operation))continue;
    if(ok&&work->count&&!strncmp(work->stops[selected].id,"SA:",3)){
     snprintf(url,sizeof(url),BASE "v2/navitia/stop_areas/stop_area%%3AIDFM%%3A%s/stop_points?count=80&depth=2&disable_geojson=true&disable_disruption=true",work->stops[selected].id+3);
     station_transport_search_t *children=calloc(1,sizeof(*children));char *body=children?fetch(url):NULL;
     ok=body&&station_transport_search_parse(body,home.latitude,home.longitude,children)&&children->count;
     if(ok){work->count=children->count<TRANSPORT_STOPS?children->count:TRANSPORT_STOPS;memcpy(work->stops,children->stops,sizeof(work->stops));work->selected=selected=0;}
     free(body);free(children);
    }
    if(superseded(operation))continue;
    if(ok&&work->count){
     if(selected>=work->count)selected=0;
     work->selected=selected;
     char previous_lines[TRANSPORT_ROWS][20];unsigned previous_count=work->row_count;
     for(unsigned i=0;i<previous_count;i++)snprintf(previous_lines[i],20,"%s",work->rows[i].line);
     char *body=NULL;ok=preview||load_departures(work,selected);
     if(superseded(operation))continue;
     if(ok){
      bool refresh_traffic=!work->traffic_at||time(NULL)-work->traffic_at>=(work->traffic_valid?1800:300);
      for(unsigned i=0;i<work->row_count;i++){bool known=false;for(unsigned j=0;j<previous_count;j++)if(!strcmp(previous_lines[j],work->rows[i].line))known=true;if(!known)refresh_traffic=true;}
      if(refresh_traffic){work->alert_count=0;work->traffic_valid=work->row_count>0;work->traffic_partial=false;}
      /* Publish departures before optional metadata and traffic requests. */
      if(refresh_traffic)work->traffic_valid=false;
      work->state=TRANS_READY;publish(work);
      summary_requests=collect_summary(work,selected,operation,override);
      if(superseded(operation))continue;
      if(refresh_traffic)work->traffic_valid=work->row_count>0;
      /* Check every displayed line, including all directions at a rail hub. */
      char seen[TRANSPORT_ROWS][20]={{0}};unsigned line_count=0;
      for(unsigned i=0;i<work->row_count&&line_count<TRANSPORT_ROWS;i++){
       if(superseded(operation)){work->traffic_partial=true;break;}
       bool duplicate=false;for(unsigned j=0;j<line_count;j++)if(!strcmp(seen[j],work->rows[i].line))duplicate=true;if(duplicate)continue;
       snprintf(seen[line_count++],20,"%s",work->rows[i].line);
       snprintf(url,sizeof(url),BASE "v2/navitia/lines/line%%3AIDFM%%3A%s?depth=0&disable_geojson=true&disable_disruption=true",work->rows[i].line);
       char code[20]="";for(unsigned c=0;c<cached_lines;c++)if(!strcmp(line_cache[c].line,work->rows[i].line))snprintf(code,20,"%s",line_cache[c].code);
       if(!code[0]){body=fetch(url);if(body&&station_transport_line(body,work->rows[i].line,code,sizeof(code))&&cached_lines<16){snprintf(line_cache[cached_lines].line,20,"%s",work->rows[i].line);snprintf(line_cache[cached_lines++].code,20,"%s",code);}free(body);}
       if(code[0])for(unsigned j=i;j<work->row_count;j++)if(!strcmp(work->rows[j].line,work->rows[i].line))snprintf(work->rows[j].code,20,"%s",code);
       if(superseded(operation))break;
       bool coverage=work->traffic_valid;if(refresh_traffic)work->traffic_valid=false;publish(work);work->traffic_valid=coverage;
       if(refresh_traffic){
        snprintf(url,sizeof(url),BASE "v2/navitia/line_reports/lines/line%%3AIDFM%%3A%s/line_reports?count=1&depth=0&disable_geojson=true",work->rows[i].line);
        body=fetch(url);if(!body||!station_transport_traffic(body,work->rows[i].line,work))work->traffic_valid=false;free(body);
       }
      }
      if(refresh_traffic)work->traffic_at=time(NULL);
      for(unsigned i=0;i<work->row_count;i++){bool covered=false;for(unsigned j=0;j<line_count;j++)if(!strcmp(seen[j],work->rows[i].line))covered=true;if(!covered)work->traffic_partial=true;}
     }
    }
    if(superseded(operation))continue;
    work->discovering=false;
    work->state=ok?(work->count?TRANS_READY:TRANS_EMPTY):fetch_failure;publish(work);
    /* Total requests, including metadata/traffic, are capped at 900 per running 24h.
       Two-minute baseline, slowed further as the remaining daily request budget requires. */
    if(ok)failures=0;else if(failures<4)failures++;
    unsigned retry=failures?15u<<(failures-1):15;
    next=(ok?last_attempt:esp_timer_get_time())+(int64_t)(ok?120:fetch_failure==TRANS_DENIED?3600:fetch_failure==TRANS_QUOTA?900:retry)*1000000;
    if(ok&&requests<900){int64_t remaining=86400000000LL-(esp_timer_get_time()-budget_start);int64_t fair=remaining*(1+summary_requests)/(900-requests);if(fair>120000000)next=esp_timer_get_time()+fair;}
   }
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
esp_err_t station_transport_start(void){return xTaskCreate(worker,"transport",8192,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
