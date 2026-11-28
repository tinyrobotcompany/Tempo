/* Local mode: plain HTTP on port 80, switched only from the display. Included by test_phone_server.c. */
#define FIXTURE_IP_TOKEN(ip,t) "http://" ip "/#token=" t
static unsigned query(void){return station_phone_local_mode(STATION_LOCAL_QUERY);}
static void local_mode_tests(void)
{
 const char *old="Bearer cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd",*fresh="Bearer efefefefefefefefefefefefefefefef";
 station_phone_link_t link;station_phone_command_t cmd;httpd_req_t r;
 /* Behaviour 7: a new device never opens port 80. */
 assert(http_starts==0&&!http_running&&local_saves==0);
 unsigned flags=query();
 assert(!(flags&(STATION_LOCAL_ON|STATION_LOCAL_BUSY|STATION_LOCAL_FAILED|STATION_LOCAL_OFFER_NO_HTTPS|STATION_LOCAL_OFFER_RENEWAL)));
 assert(flags&STATION_LOCAL_HTTPS_READY);
 r=request("/api/status",NULL,old);assert(r.code==200&&strstr(r.response,"\"localMode\":false"));
 /* Behaviour 1: no phone route can switch Local mode on, authorised or not. */
 static const char *const attempts[][2]={
  {"/api/settings","{\"localMode\":true,\"brightness\":50}"},{"/api/view","{\"view\":0,\"localMode\":true}"},
  {"/api/update","{\"action\":\"localMode\"}"},{"/api/update","{\"action\":\"local\",\"enabled\":true}"},
  {"/api/location","{\"latitude\":1,\"longitude\":2,\"name\":\"x\",\"localMode\":true}"},
  {"/api/credential","{\"key\":\"fixture-only\",\"localMode\":true}"}};
 for(unsigned i=0;i<sizeof(attempts)/sizeof(attempts[0]);i++){request(attempts[i][0],attempts[i][1],NULL);request(attempts[i][0],attempts[i][1],old);}
 while(station_phone_take(&cmd));
 for(unsigned i=0;i<3;i++)station_phone_maintenance();
 assert(http_starts==0&&local_saves==0&&!(query()&STATION_LOCAL_ON));
 for(unsigned i=0;i<route_count;i++)assert(!strstr(routes[i].uri,"local"));
 /* Unknown request codes are ignored. */
 assert(!(station_phone_local_mode(3)&STATION_LOCAL_BUSY)&&!(station_phone_local_mode(-1)&STATION_LOCAL_BUSY));
 /* A display request that cannot be saved leaves it off and reports the failure. */
 local_save_error=ESP_FAIL;
 flags=station_phone_local_mode(STATION_LOCAL_REQUEST_ON);assert((flags&STATION_LOCAL_BUSY)&&!(flags&STATION_LOCAL_ON));
 station_phone_maintenance();flags=query();
 assert((flags&STATION_LOCAL_FAILED)&&!(flags&(STATION_LOCAL_ON|STATION_LOCAL_BUSY))&&http_starts==0&&saved_local==STATION_LOCAL_MODE_OFF);
 local_save_error=0;
 /* Behaviour 5: the choice is saved first, then port 80 opens; start failures retry. */
 http_start_fail=true;station_phone_local_mode(STATION_LOCAL_REQUEST_ON);station_phone_maintenance();
 flags=query();assert((flags&STATION_LOCAL_ON)&&!(flags&STATION_LOCAL_FAILED)&&saved_local==STATION_LOCAL_MODE_ON&&http_starts==1&&!http_running);
 station_phone_link(&link);assert(!link.online);
 http_start_fail=false;http_route_fail=true;station_phone_maintenance();assert(http_starts==2&&http_stops==1&&!http_running);
 http_stop_fail=true;station_phone_maintenance();assert(http_starts==3&&http_stops==2&&http_running);
 station_phone_maintenance();assert(http_starts==3&&http_stops==3);
 http_stop_fail=false;http_route_fail=false;station_phone_maintenance();assert(http_starts==4&&http_stops==4&&http_running&&http_route_count==11);
 unsigned tls_before=tls_stops,saves=local_saves;
 station_phone_local_mode(STATION_LOCAL_REQUEST_ON);station_phone_maintenance();assert(http_starts==4&&local_saves==saves);
 /* Behaviour 3: same page and API on port 80, QR shows http://<IPv4>/ with the token, HTTPS keeps running. */
 station_phone_link(&link);assert(link.online&&!strcmp(link.url,FIXTURE_IP_TOKEN("172.20.10.3","cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd")));
 flags=query();assert((flags&STATION_LOCAL_ON)&&(flags&STATION_LOCAL_HTTPS_READY)&&route_count==11&&tls_stops==tls_before);
 for(unsigned i=0;i<route_count;i++)assert(!strcmp(routes[i].uri,http_routes[i].uri)&&routes[i].method==http_routes[i].method&&routes[i].handler==http_routes[i].handler);
 r=plain("/",NULL,NULL);assert(strstr(r.response,"Your weather station"));
 r=plain("/api/status",NULL,NULL);assert(r.code==401);
 unsigned takes=lock_takes;
 r=plain("/api/status",NULL,old);assert(r.code==200&&strstr(r.response,"\"localMode\":true")&&lock_takes==takes+1);
 r=request("/api/status",NULL,old);assert(r.code==200&&strstr(r.response,"\"localMode\":true"));
 r=plain("/api/view","{\"view\":2}",old);assert(r.code==202&&station_phone_take(&cmd)&&cmd.view==VIEW_TRANSPORT);
 r=plain("/api/credential","{\"key\":\"plain-fixture\"}",NULL);assert(r.code==401);
 /* Phones cannot switch it off either; network loss hides the QR. */
 request("/api/settings","{\"localMode\":false}",old);plain("/api/update","{\"action\":\"localMode\"}",old);
 station_phone_maintenance();assert((query()&STATION_LOCAL_ON)&&http_running);
 network.connected=false;station_phone_link(&link);assert(!link.online);network.connected=true;
 /* Behaviour 2: the display offers Local mode once renewal has failed for 7 days. */
 renew_after=time(NULL)-6*86400;assert(!(query()&STATION_LOCAL_OFFER_RENEWAL));
 renew_after=time(NULL)-7*86400-1;assert(query()&STATION_LOCAL_OFFER_RENEWAL);renew_after=0;
 /* Behaviour 4: turning off. A failed save keeps it on. */
 unsigned before=rotations;
 local_save_error=ESP_FAIL;station_phone_local_mode(STATION_LOCAL_REQUEST_OFF);station_phone_maintenance();
 flags=query();assert((flags&STATION_LOCAL_FAILED)&&(flags&STATION_LOCAL_ON)&&http_running&&rotations==before);local_save_error=0;
 /* Port 80 closes before the token is replaced; no QR is shown meanwhile. */
 http_stop_fail=true;station_phone_local_mode(STATION_LOCAL_REQUEST_OFF);station_phone_maintenance();
 flags=query();assert(saved_local==STATION_LOCAL_MODE_ROTATE&&http_running&&rotations==before&&(flags&STATION_LOCAL_BUSY)&&!(flags&STATION_LOCAL_ON));
 station_phone_link(&link);assert(!link.online);
 http_stop_fail=false;rotate_fail=true;station_phone_maintenance();assert(!http_running&&rotations==before+1&&saved_local==STATION_LOCAL_MODE_ROTATE);
 station_phone_link(&link);assert(!link.online);
 rotate_fail=false;local_save_error=ESP_FAIL;station_phone_maintenance();assert(rotations==before+2&&saved_local==STATION_LOCAL_MODE_ROTATE);
 r=request("/api/status",NULL,old);assert(r.code==401);r=request("/api/status",NULL,fresh);assert(r.code==200);
 station_phone_link(&link);assert(!link.online);
 local_save_error=0;station_phone_maintenance();assert(rotations==before+2&&saved_local==STATION_LOCAL_MODE_OFF);
 flags=query();assert(!(flags&(STATION_LOCAL_ON|STATION_LOCAL_BUSY|STATION_LOCAL_FAILED)));
 station_phone_link(&link);assert(link.online&&strstr(link.url,"https://device-a7f3.tempo.tinyrobotcompany.io/#token=efef"));
 station_phone_maintenance();assert(rotations==before+2&&!http_running&&http_starts==4);
 r=request("/api/status",NULL,fresh);assert(r.code==200&&strstr(r.response,"\"localMode\":false"));
 puts("PASS: Local mode: display-only switch, port 80 same API, http QR, renewal offer, close then rotate token");
}
static int boot_case(const char *name)
{
 station_settings_t initial=station_defaults();network.connected=true;fail_start=false;fail_route=false;
 station_phone_link_t link;httpd_req_t r;
 if(!strcmp(name,"--boot-local-unlocked")){
  /* Unlocked board: no identity, so HTTPS cannot start; saved Local mode still works after reboot. */
  saved_local=STATION_LOCAL_MODE_ON;fail_start=true;
  assert(station_phone_start(&initial)==ESP_FAIL&&http_running&&http_route_count==11&&route_count==0);
  unsigned flags=query();assert((flags&STATION_LOCAL_ON)&&(flags&STATION_LOCAL_OFFER_NO_HTTPS)&&!(flags&STATION_LOCAL_HTTPS_READY));
  station_phone_link(&link);assert(link.online&&!strcmp(link.url,FIXTURE_IP_TOKEN("172.20.10.2","abababababababababababababababab")));
  r=plain("/api/status",NULL,"Bearer abababababababababababababababab");assert(r.code==200&&strstr(r.response,"\"localMode\":true"));
  station_phone_local_mode(STATION_LOCAL_REQUEST_OFF);station_phone_maintenance();
  assert(!http_running&&rotations==1&&saved_local==STATION_LOCAL_MODE_OFF);
  flags=query();assert(!(flags&STATION_LOCAL_ON)&&(flags&STATION_LOCAL_OFFER_NO_HTTPS));
  station_phone_link(&link);assert(!link.online);
 }else if(!strcmp(name,"--boot-local-on")){
  saved_local=STATION_LOCAL_MODE_ON;
  assert(station_phone_start(&initial)==ESP_OK&&http_running&&route_count==11&&http_route_count==11);
  station_phone_maintenance();assert((query()&STATION_LOCAL_HTTPS_READY)&&http_starts==1);
 }else if(!strcmp(name,"--boot-rotate")||!strcmp(name,"--boot-unreadable")){
  /* Power was lost while turning off, or the flag is unreadable: stay closed and replace the token. */
  if(name[7]=='r')saved_local=STATION_LOCAL_MODE_ROTATE;else local_load_error=ESP_FAIL;
  assert(station_phone_start(&initial)==ESP_OK&&!http_running&&http_starts==0);
  assert(query()&STATION_LOCAL_BUSY);station_phone_link(&link);assert(!link.online);
  station_phone_maintenance();assert(rotations==1&&saved_local==STATION_LOCAL_MODE_OFF&&http_starts==0);
  station_phone_link(&link);assert(link.online&&strstr(link.url,"https://device-a7f3.tempo.tinyrobotcompany.io/#token=cdcd"));
 }else if(!strcmp(name,"--boot-no-lock")){
  lock_fail=true;assert(station_phone_start(&initial)==ESP_ERR_NO_MEM);lock_fail=false;
  assert(station_phone_start(&initial)==ESP_OK);
 }else return 2;
 printf("PASS: Local mode boot case %s\n",name);return 0;
}
