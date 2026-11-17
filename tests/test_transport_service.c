#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <time.h>
static time_t now_seconds=1794557252;
static time_t test_time(time_t *out){time_t n=now_seconds;if(out)*out=n;return n;}
#define time test_time
#include "../firmware/main/transport_service.c"
bool station_credential_get(char *out,size_t size){snprintf(out,size,"host-fixture");return true;}
static int64_t clock_us=1000000,declared=2;
static int status_code=200,reads,opened,closed,cleaned;
static size_t received,total=2;
static bool broken,slow;
static const char *response;
static bool summary_scenario,summary_fail;
static char summary_response[2048];
static char last_url[768];
static station_network_status_t network;
static bool scenario,interrupt_traffic,doing_traffic,resolved_search;static unsigned loops;
static const char *expected_stop="43186";static unsigned recovery_mode,departure_calls;static int64_t failure_at;
static jmp_buf worker_done;static char *near_fixture,*children_fixture,*siri_fixture;
static char *load_fixture(const char *name){FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *data=malloc((size_t)n+1);assert(data&&fread(data,1,(size_t)n,f)==(size_t)n);data[n]=0;fclose(f);return data;}

int64_t esp_timer_get_time(void){return clock_us;}
void station_network_status(station_network_status_t *s){*s=network;}
void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
void esp_crt_bundle_attach(void){}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)fn;(void)name;(void)arg;(void)handle;assert(stack==8192&&priority==2);return pdPASS;}
void vTaskDelay(unsigned ticks){(void)ticks;if(scenario){loops++;assert(loops<5);if(snapshot.valid&&resolved_search)longjmp(worker_done,1);
 if(recovery_mode==1){assert(snapshot.state==TRANS_RETRY);failure_at=clock_us;clock_us+=15000000;}
 else if(recovery_mode==2){if(loops==1)network.connected=false;else network.connected=true;clock_us+=1000000;}
 else clock_us+=1000000;}}
void vTaskDelete(void *task){(void)task;}
void *esp_http_client_init(esp_http_client_config_t *config){assert(config->disable_auto_redirect&&config->crt_bundle_attach&&config->buffer_size_tx>=1024);assert(config->tls_version==ESP_HTTP_CLIENT_TLS_VER_TLS_1_3);received=0;snprintf(last_url,sizeof(last_url),"%s",config->url);
 if(summary_scenario){
  unsigned id=strstr(config->url,"200")?200:300;char stamp[40];time_t future=now_seconds+900;struct tm civil;gmtime_r(&future,&civil);strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&civil);
  snprintf(summary_response,sizeof(summary_response),"{\"Siri\":{\"ServiceDelivery\":{\"StopMonitoringDelivery\":[{\"Status\":true,\"MonitoredStopVisit\":[{\"MonitoringRef\":{\"value\":\"STIF:StopPoint:Q:%u:\"},\"MonitoredVehicleJourney\":{\"LineRef\":{\"value\":\"STIF:Line::C00001:\"},\"DestinationName\":[{\"value\":\"Destination %u\"}],\"MonitoredCall\":{\"StopPointName\":[{\"value\":\"Station %u\"}],\"ExpectedDepartureTime\":\"%s\"}}}]}]}}}",id,id,id,stamp);
  response=summary_response;declared=0;total=strlen(response);status_code=summary_fail&&id==300?503:200;
 }
 else if(scenario){status_code=200;doing_traffic=strstr(config->url,"/line_reports")!=NULL;
  if(strstr(config->url,"/places_nearby")){
   if(strstr(config->url,"commercial_mode%3AMetro"))if(!recovery_mode)assert(snapshot.valid&&snapshot.row_count&&snapshot.discovering);
   response=near_fixture;
  }
  else if(strstr(config->url,"/stop_areas/")){assert(strstr(config->url,"427230"));resolved_search=true;response=children_fixture;}
  else if(strstr(config->url,"stop-monitoring")){
   if(strstr(config->url,expected_stop)){response=siri_fixture;if(recovery_mode&&departure_calls++<2)status_code=503;else if(recovery_mode==1)assert(clock_us-failure_at==15000000);}
   else response="{\"Siri\":{\"ServiceDelivery\":{\"StopMonitoringDelivery\":[{\"Status\":true,\"MonitoredStopVisit\":[]}]}}}";
  }
  else if(doing_traffic)response="{\"line_reports\":[],\"disruptions\":[]}";
  else response="{\"lines\":[{\"id\":\"line:IDFM:C01743\",\"code\":\"B\"}]}";
  declared=0;total=strlen(response);
 }
 return (void*)1;}
int esp_http_client_set_header(void *c,const char *name,const char *value){(void)c;if(!strcmp(name,"apikey"))assert(!strcmp(value,"host-fixture"));return 0;}
int esp_http_client_set_timeout_ms(void *c,int ms){(void)c;assert(ms==2000);return ESP_OK;}
int esp_http_client_open(void *c,int length){(void)c;assert(length==0);opened++;return ESP_OK;}
int64_t esp_http_client_fetch_headers(void *c){(void)c;return declared;}
int esp_http_client_get_status_code(void *c){(void)c;return status_code;}
int esp_http_client_read(void *c,char *out,int length){(void)c;reads++;
 if(scenario&&doing_traffic&&interrupt_traffic){interrupt_traffic=false;search_result.state=LOOKUP_READY;search_result.count=1;memset(&search_result.stops[0],0,sizeof(search_result.stops[0]));strcpy(search_result.stops[0].id,"SA:427230");strcpy(search_result.stops[0].name,"Robinson search result");assert(station_transport_search_select(0));}
 if(broken)return -1;if(slow)clock_us+=5000000;size_t n=total-received;if(n>(size_t)length)n=(size_t)length;if(response)memcpy(out,response+received,n);else memset(out,' ',n);received+=n;return (int)n;}
bool esp_http_client_is_complete_data_received(void *c){(void)c;return received==total;}
void esp_http_client_close(void *c){(void)c;closed++;}
void esp_http_client_cleanup(void *c){(void)c;cleaned++;}
int main(void){
 char *body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(body&&received==2);free(body);assert(closed==1&&cleaned==1);
 declared=2545746;total=(size_t)declared;int before=reads;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body&&reads==before);assert(closed==2&&cleaned==2);
 declared=0;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body&&received==BODY_LIMIT);assert(closed==3&&cleaned==3);
 status_code=403;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body&&fetch_failure==TRANS_DENIED);
 status_code=429;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body&&fetch_failure==TRANS_QUOTA);before=opened;assert(!fetch("https://prim.iledefrance-mobilites.fr/marketplace/test")&&opened==before);
 clock_us+=901000000;status_code=200;declared=0;total=40000;slow=true;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body&&received<total);slow=false;
 broken=true;body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/test");assert(!body);broken=false;
 requests=900;before=opened;assert(!fetch("https://prim.iledefrance-mobilites.fr/marketplace/test")&&opened==before);
 assert(closed==cleaned&&opened==closed);
 network.location_generation=2;selection=0;snapshot.generation=2;snapshot.valid=false;
 station_transport_snapshot_t result={.generation=1,.selected=0,.valid=true};publish(&result);assert(!snapshot.valid);
 result.generation=2;selection=1;publish(&result);assert(!snapshot.valid);
 selection=0;publish(&result);assert(snapshot.valid);
 snapshot.count=2;snapshot.row_count=1;snapshot.traffic_valid=true;snapshot.alert_count=1;assert(station_transport_select(1));assert(!snapshot.valid&&!snapshot.row_count&&!snapshot.alert_count&&!snapshot.traffic_valid&&snapshot.state==TRANS_LOADING);
 requests=0;blocked_until=0;status_code=200;declared=0;
 response="{\"line_reports\":[],\"disruptions\":[],\"unused\":{\"payload\":[1,2,3]}}";total=strlen(response);
 body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia/line_reports/lines/test/line_reports");assert(body&&strstr(body,"disruptions")&&!strstr(body,"unused"));free(body);
 response="{\"unused\":[1,]}";total=strlen(response);body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia/line_reports/lines/test/line_reports");assert(!body);
 assert(closed==cleaned&&opened==closed);

 char *hub_response=load_fixture("tests/fixtures/transport/defense-departures.json");
 response=hub_response;total=strlen(response);declared=(int64_t)total;assert(total>128*1024);
 body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF:StopArea:SP:470549:");
 assert(body);station_transport_snapshot_t hub={0};time_t captured=station_transport_timestamp("2026-11-13T13:47:06Z");
 assert(station_transport_departures(body,"SP:470549",captured,&hub)&&hub.valid&&hub.row_count);
 bool boarding=false;for(unsigned i=0;i<hub.row_count;i++)if(!strcmp(hub.rows[i].destination,"Poissy")&&!hub.rows[i].arrival[0]&&hub.rows[i].at[0]==station_transport_timestamp("2026-11-13T13:47:23Z"))boarding=true;assert(boarding);
 printf("PASS: La Défense %zu-byte SIRI response projected to %zu bytes, %u departure rows, train already boarding retained\n",total,strlen(body),hub.row_count);
 free(body);free(hub_response);response=NULL;
 declared=8*1024*1024+1;total=(size_t)declared;before=reads;
 assert(!fetch("https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring")&&reads==before);
 response="{\"Siri\":{\"ServiceDelivery\":{\"StopMonitoringDelivery\":[{\"Status\":false,\"MonitoredStopVisit\":[]}]}}}";declared=0;total=strlen(response);
 body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring");assert(body&&!station_transport_departures(body,"SP:470549",captured,&hub));free(body);response=NULL;total=0;declared=0;
 result=snapshot;result.valid=true;search_result.state=LOOKUP_READY;search_result.count=1;strcpy(search_result.stops[0].id,"SA:427230");
 char *route_fixture=load_fixture("tests/fixtures/transport/rer-b-route-schedules.json");response=route_fixture;declared=total=strlen(response);status_code=200;
 char *route_body=fetch("https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia/lines/line:IDFM:C01743/route_schedules");assert(route_body);
 station_transport_line_map_t *routes=calloc(1,sizeof(*routes));assert(routes&&station_transport_routes_parse(route_body,routes));assert(routes->route_count==8&&routes->count==47&&!routes->partial);
 snprintf(routes->line,20,"C01743");
 char *areas=load_fixture("tests/fixtures/transport/rer-b-interchanges.json");response=areas;total=strlen(areas);declared=0;received=0;
 char *area_body=fetch(BASE "v2/navitia/lines/line:IDFM:C01743/stop_areas");assert(area_body);assert(station_transport_interchanges_parse(area_body,routes));free(area_body);free(areas);
 unsigned interchanges=0;bool metro=false,bus=false,tram=false;for(unsigned i=0;i<routes->count;i++){assert(routes->stops[i].area[0]);if(routes->stops[i].connections[0])interchanges++;if(strstr(routes->stops[i].connections,"Métro"))metro=true;if(strstr(routes->stops[i].connections,"Bus"))bus=true;if(strstr(routes->stops[i].connections,"Tram"))tram=true;}
 assert(interchanges>10&&metro&&bus&&tram);
 line_result=*routes;line_result.state=LOOKUP_READY;assert(station_transport_line_stop_select(0));assert(search_pending&&search_result.connections&&search_result.state==LOOKUP_LOADING&&!strcmp(connection_area,routes->stops[0].area));search_pending=false;
 char *connections=load_fixture("tests/fixtures/transport/interchange-boarding-points.json");response=connections;total=strlen(connections);declared=0;received=0;
 char *connections_body=fetch(BASE "v2/navitia/stop_areas/fixture/stop_points");assert(connections_body);station_transport_search_t *all=calloc(1,sizeof(*all));assert(all&&station_transport_search_parse(connections_body,48.83,2.33,all));assert(all->count==21&&!all->partial);metro=bus=false;for(unsigned i=0;i<all->count;i++){if(strstr(all->stops[i].mode,"Métro"))metro=true;if(strstr(all->stops[i].mode,"Bus"))bus=true;assert(all->services[i][0]);}assert(metro&&bus);search_pending=true;lookups(&network);assert(search_result.connections&&search_result.state==LOOKUP_READY&&search_result.count==21);assert(strstr(last_url,"/stop_areas/")&&strstr(last_url,"depth=3"));assert(station_transport_search_select(20));assert(!strcmp(snapshot.stops[0].id,all->stops[20].id));free(all);free(connections_body);free(connections);response=NULL;
 puts("PASS: route parent IDs, cross-mode interchanges, 21 boarding points and selection beyond nearby limit");

 assert(!strcmp(routes->stops[routes->routes[0].stops[0]].name,"Aéroport Charles de Gaulle 2 (Terminal 2)"));assert(!strcmp(routes->stops[routes->routes[0].stops[routes->routes[0].count-1]].name,"Robinson"));
 printf("PASS: full RER B route projection, %u branches, %u stations in provider order\n",routes->route_count,routes->count);free(routes);free(route_body);free(route_fixture);response=NULL;declared=total=0;
 search_result.count=1;strcpy(search_result.stops[0].id,"SA:427230");line_result.state=LOOKUP_FAILED;
 assert(station_transport_search_select(0)&&station_override&&snapshot.count==1&&!snapshot.valid);
 publish(&result);assert(!snapshot.valid); /* same index, different selected station */
 assert(!station_transport_search_select(1));station_transport_nearby_reset();assert(!rediscover&&!station_override&&snapshot.count>0&&!snapshot.valid);
 assert(station_transport_search("Robinson"));assert(!station_transport_search("Robinson"));
 assert(!station_transport_line_map("C01743/evil"));assert(station_transport_line_map("C01743")&&line_pending);

 near_fixture=load_fixture("tests/fixtures/transport/live-rer.json");children_fixture=load_fixture("tests/fixtures/transport/live-search-children.json");siri_fixture=load_fixture("tests/fixtures/transport/live-robinson-siri.json");
 network=(station_network_status_t){.location_valid=true,.connected=true,.source=TIME_NETWORK,.location_generation=7,.latitude=48.766968,.longitude=2.280719};
 requests=0;blocked_until=0;search_pending=line_pending=rediscover=false;scenario=interrupt_traffic=true;
 if(!setjmp(worker_done))worker(NULL);
 assert(!interrupt_traffic&&resolved_search&&snapshot.valid&&snapshot.row_count>0&&!strcmp(snapshot.stops[snapshot.selected].id,"SP:43186"));
 assert(loops==1);free(near_fixture);free(children_fixture);free(siri_fixture);

 near_fixture=load_fixture("tests/fixtures/transport/defense-rer.json");siri_fixture=load_fixture("tests/fixtures/transport/defense-departures.json");expected_stop="470549";
 now_seconds=station_transport_timestamp("2026-11-13T13:47:06Z");network.latitude=48.8922;network.longitude=2.2369;network.location_generation++;loops=0;resolved_search=true;
 if(!setjmp(worker_done))worker(NULL);
 assert(snapshot.valid&&snapshot.row_count>0&&!strcmp(snapshot.stops[0].id,"SP:470549")&&loops==1);
 for(recovery_mode=1;recovery_mode<=2;recovery_mode++){
  loops=departure_calls=0;network.connected=true;network.location_generation++;failure_at=0;
  if(!setjmp(worker_done))worker(NULL);
  assert(snapshot.valid&&snapshot.row_count>0&&departure_calls==3);
  assert(loops==(recovery_mode==1?2:3));
 }
 free(near_fixture);free(siri_fixture);
 station_override=false;snapshot.generation=network.location_generation;snapshot.count=2;strcpy(snapshot.stops[0].id,"SP:43186");strcpy(snapshot.stops[1].id,"SP:46007");
 search_result.count=1;search_result.state=LOOKUP_READY;strcpy(search_result.stops[0].id,"SP:99999");
 assert(station_transport_search_select(0)&&snapshot.count==1);station_transport_nearby_reset();assert(!station_override&&!rediscover&&snapshot.count==2&&!strcmp(snapshot.stops[0].id,"SP:43186")&&!snapshot.row_count&&requested);
 assert(station_transport_search_select(0));network.location_generation++;station_transport_nearby_reset();assert(rediscover&&!snapshot.count&&!snapshot.valid);
 scenario=false;summary_scenario=true;requests=0;blocked_until=0;cached_lines=1;strcpy(line_cache[0].line,"C00001");strcpy(line_cache[0].code,"X");
 station_transport_snapshot_t *overview=calloc(1,sizeof(*overview));assert(overview);overview->generation=network.location_generation;overview->selection_generation=snapshot.selection_generation;overview->count=3;overview->valid=true;overview->row_count=1;overview->fetched_at=now_seconds;selection=0;
 for(unsigned i=0;i<3;i++){snprintf(overview->stops[i].id,24,"%u",(i+1)*100);snprintf(overview->stops[i].name,96,"Station %u",(i+1)*100);}
 strcpy(overview->rows[0].line,"C00001");strcpy(overview->rows[0].code,"X");strcpy(overview->rows[0].destination,"Selected station's only direction");overview->rows[0].at[0]=now_seconds+600;
 assert(collect_summary(overview,0,command_generation,false)==2);assert(overview->row_count==1&&overview->summary.count==3);assert(!strcmp(overview->summary.stop_ids[1],"200")&&!strcmp(overview->summary.stop_ids[2],"300"));
 overview->summary.fetched[2]=now_seconds-400;summary_fail=true;collect_summary(overview,0,command_generation,false);assert(overview->summary.count==3&&overview->summary.partial&&overview->summary.fetched[2]==now_seconds-400);
 free(overview);summary_scenario=false;
 puts("PASS: one selected-station row becomes three real nearby-station rows; failed refresh retains original timestamp");
 puts("PASS: nearby list restores immediately; changed home rejects obsolete cached stations");
 puts("PASS: La Défense initial publication before métro discovery, 15-second transient retry and immediate Wi-Fi reconnect retry");
 puts("PASS: real worker interrupts a disruption download, resolves a searched station and publishes its departures without a polling delay");
 puts("PASS: production transport HTTP caps, stream deadline, TLS/auth headers, no redirects, quotas, cleanup and stale-location/stop suppression");
}
