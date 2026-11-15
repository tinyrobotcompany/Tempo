#include "station_transport.h"
#include "transport_lookup.h"
#include "transport_json_filter.h"
#include "station_map.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *read_file(const char *name){FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=malloc((size_t)n+1);assert(s);assert(fread(s,1,(size_t)n,f)==(size_t)n);s[n]=0;fclose(f);return s;}
int main(void){
 station_transport_snapshot_t s={0};
 char *near=read_file("tests/fixtures/transport/nearby.json");assert(station_transport_nearby(near,&s));assert(s.count==8);assert(!strcmp(s.stops[0].id,"12269"));assert(s.stops[0].metres==80);free(near);
 char *departures=read_file("tests/fixtures/transport/departures-1.json");time_t now=station_transport_timestamp("2026-11-13T06:54:02.128Z");assert(now>0);
 assert(station_transport_departures(departures,"12269",now,&s));assert(s.row_count==1);assert(!strcmp(s.rows[0].line,"C01268"));assert(strstr(s.rows[0].destination,"Châtillon"));assert(s.rows[0].at[1]>s.rows[0].at[0]);assert(s.rows[0].estimated);
 assert(station_transport_departures(departures,"999",now,&s)&&s.row_count==0);
 assert(station_transport_departures(departures,"12269",now+86400,&s)&&s.row_count==0);free(departures);
 assert(!station_transport_departures("{\"Siri\":{\"ServiceDelivery\":{\"StopMonitoringDelivery\":[{\"Status\":\"false\"}]}}}","1",now,&s));
 assert(!station_transport_timestamp("2026-02-30T00:00:00Z"));assert(!station_transport_timestamp("2026-11-13T06:54:02+02:00"));
 assert(!station_transport_nearby("{}",&s));
 char code[20];assert(station_transport_line("{\"lines\":[{\"id\":\"line:IDFM:C01268\",\"code\":\"194\"}]}","C01268",code,sizeof(code)));assert(!strcmp(code,"194"));
 char *traffic=read_file("tests/fixtures/transport/traffic-compact.json");s.alert_count=0;assert(station_transport_traffic(traffic,"C01727",&s));assert(s.alert_count>0);assert(!station_transport_traffic(traffic,"C01268",&s));free(traffic);
 bool more=false;s=(station_transport_snapshot_t){0};
 char *page0=read_file("tests/fixtures/transport/discovery-page-0.json");assert(station_transport_nearby_page(page0,&s,true,&more)&&more);free(page0);assert(s.count==8);
 char *page1=read_file("tests/fixtures/transport/discovery-page-1.json");assert(station_transport_nearby_page(page1,&s,false,&more)&&!more);free(page1);
 assert(!strcmp(s.stops[1].id,"SP:45001")&&s.stops[1].metres==1500&&s.stops[1].quay_count==2);
 assert(!strcmp(s.stops[2].id,"SP:45002")&&!strcmp(s.stops[3].mode,"Métro"));
 assert(s.stops[0].coordinates&&s.stops[0].latitude==48.77);
 for(unsigned i=0;i<s.count;i++)assert(strcmp(s.stops[i].name,"Outside"));
 char *hub=read_file("tests/fixtures/transport/hub-departures.json");assert(station_transport_departures(hub,"SP:45001",now,&s));free(hub);
 assert(s.row_count==1&&s.rows[0].arrival[0]&&s.rows[0].predicted[0]);
 assert(s.rows[0].at[0]==station_transport_timestamp("2026-11-13T07:03:00Z"));
 assert(s.rows[0].delay_minutes[0]==3&&s.rows[0].cancelled[1]);assert(!strcmp(s.rows[0].platform,"2"));

 s=(station_transport_snapshot_t){0};
 char *live=read_file("tests/fixtures/transport/live-rer.json");
 assert(station_transport_nearby(live,&s));free(live);
 assert(s.count==2&&!strcmp(s.stops[0].id,"SP:43186")&&!strcmp(s.stops[1].id,"SP:46007"));
 assert(s.stops[0].metres==1477&&s.stops[1].metres==1818);
 live=read_file("tests/fixtures/transport/live-robinson-siri.json");
 /* Live capture: evaluate at its response timestamp, not the test machine clock. */
 now=station_transport_timestamp("2026-11-13T08:07:32Z");
 assert(station_transport_departures(live,"SP:43186",now,&s));free(live);assert(s.row_count>0);for(unsigned i=0;i<s.row_count;i++)assert(strcmp(s.rows[i].destination,"Robinson"));
 station_transport_search_t found={0};
 live=read_file("tests/fixtures/transport/live-search.json");assert(station_transport_search_parse(live,48.766968,2.280719,&found));free(live);
 assert(found.count&&!strcmp(found.stops[0].id,"SA:427230"));
 live=read_file("tests/fixtures/transport/live-search-children.json");assert(station_transport_search_parse(live,48.766968,2.280719,&found));free(live);
 assert(found.count&&!strcmp(found.stops[0].id,"SP:43186"));
 live=read_file("tests/fixtures/transport/live-line-stops.json");
 char *projected=malloc(256*1024);assert(projected);transport_json_filter_t *filter=transport_json_filter_create_network(projected,256*1024);assert(filter);
 for(size_t at=0;at<strlen(live);){size_t n=strlen(live)-at;if(n>31)n=31;assert(transport_json_filter_feed(filter,live+at,n));at+=n;}
 size_t length;assert(transport_json_filter_finish(filter,&length));transport_json_filter_free(filter);free(live);
 station_transport_line_map_t line_map={0};assert(station_transport_line_stops_parse(projected,&line_map));free(projected);assert(line_map.count==47&&!line_map.partial);
 bool robinson=false;for(unsigned i=0;i<line_map.count;i++)if(!strcmp(line_map.stops[i].id,"SP:43186"))robinson=line_map.stops[i].coordinates;assert(robinson);
 for(unsigned z=3;z<=18;z++){double x,y,lat,lon;station_map_project(48.766968,2.280719,z,&x,&y);station_map_unproject(x,y,z,&lat,&lon);assert(fabs(lat-48.766968)<1e-8&&fabs(lon-2.280719)<1e-8);}
 puts("PASS: live RER identifiers, station-complex resolution, 47 line stations, network JSON projection and map coordinates");
 puts("PASS: captured nearby/SIRI/traffic contracts, UTF-8, stop matching, expiry, timestamp validation, provider errors and line relevance");
}
