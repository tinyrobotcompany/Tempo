#include "transport_json_filter.h"
#include "station_transport.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(const char *text,bool valid){
 char out[2048];transport_json_filter_t *f=transport_json_filter_create(out,sizeof(out));bool ok=true;
 for(size_t i=0;text[i]&&ok;i++)ok=transport_json_filter_feed(f,text+i,1);
 size_t n=0;ok=ok&&transport_json_filter_finish(f,&n);assert(ok==valid);
 if(ok){cJSON *j=cJSON_Parse(out);assert(j);cJSON_Delete(j);}transport_json_filter_free(f);
}
int main(int argc,char **argv){
 check("{\"status\":\"active\",\"unused\":{\"huge\":[1,true,null,{\"xyz\":\"é\\\"x\"}]},\"id\":\"123\"}",true);
 check("{\"id\":\"x\",}",false);check("{\"unused\":[1,]}",false);check("{\"unused\":tru}",false);check("{\"id\":\"unterminated}",false);check("{}{}",false);check("{\"unused\":01}",false);
 char small[4];transport_json_filter_t *f=transport_json_filter_create(small,sizeof(small));assert(!transport_json_filter_feed(f,"{\"id\":123}",10));transport_json_filter_free(f);
 char projected[128];f=transport_json_filter_create(projected,sizeof(projected));assert(f);
 assert(transport_json_filter_feed(f,"{\"unused\":\"",11));char filler[4096];memset(filler,'x',sizeof(filler));
 for(unsigned i=0;i<80;i++)assert(transport_json_filter_feed(f,filler,sizeof(filler)));
 const char *tail="\",\"id\":\"retained\"}";assert(transport_json_filter_feed(f,tail,strlen(tail)));size_t retained;
 assert(transport_json_filter_finish(f,&retained)&&!strcmp(projected,"{\"id\":\"retained\"}"));transport_json_filter_free(f);
 if(argc>1){
  FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);char *input=malloc((size_t)size+1);assert(input);assert(fread(input,1,(size_t)size,file)==(size_t)size);fclose(file);input[size]=0;
  const size_t chunks[]={1,17,4096};
  for(unsigned i=0;i<3;i++){
   char *out=malloc(256*1024+1);assert(out);f=transport_json_filter_create(out,256*1024+1);assert(f);
   for(size_t pos=0;pos<(size_t)size;){size_t n=(size_t)size-pos;if(n>chunks[i])n=chunks[i];assert(transport_json_filter_feed(f,input+pos,n));pos+=n;}
   size_t used;assert(transport_json_filter_finish(f,&used));station_transport_snapshot_t *s=calloc(1,sizeof(*s));assert(s);
   assert(station_transport_traffic(out,"C01727",s)&&s->alert_count);assert(!station_transport_traffic(out,"C01268",s));
   printf("PASS: %ld traffic bytes projected to %zu bytes, chunk=%zu, alerts=%u\n",size,used,chunks[i],s->alert_count);
   free(s);transport_json_filter_free(f);free(out);
  }
  free(input);
 }
 puts("PASS: traffic JSON filtering, chunk boundaries, malformed discarded data and memory bounds");
}
