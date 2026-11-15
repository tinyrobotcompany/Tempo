#include "phone_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
 const char *text="{\"name\":\"Simon\",\"brightness\":65,\"theme\":3,\"color\":12345,\"font\":1,\"dwell\":20,\"rotate\":true}";
 assert(station_phone_json_shallow(text));assert(!station_phone_json_shallow("[[[[[[[[[]]]]]]]]]"));assert(!station_phone_json_shallow("{\"name\":\"unfinished"));
 assert(station_phone_json_shallow("{\"name\":\"[[[[[[[[[\"}"));
 cJSON *j=cJSON_Parse(text);station_settings_t s;assert(station_phone_parse_settings(j,&s));assert(s.brightness==65&&!strcmp(s.user_name,"Simon"));
 cJSON_ReplaceItemInObject(j,"font",cJSON_CreateNumber(3));assert(station_phone_parse_settings(j,&s)&&s.clock_font==3);
 cJSON_ReplaceItemInObject(j,"font",cJSON_CreateNumber(9));assert(station_phone_parse_settings(j,&s)&&s.clock_font==9);
 cJSON_ReplaceItemInObject(j,"font",cJSON_CreateNumber(10));assert(!station_phone_parse_settings(j,&s));
 cJSON_ReplaceItemInObject(j,"font",cJSON_CreateNumber(1));
 cJSON_ReplaceItemInObject(j,"brightness",cJSON_CreateNumber(65.5));assert(!station_phone_parse_settings(j,&s));
 cJSON_ReplaceItemInObject(j,"brightness",cJSON_CreateNumber(101));assert(!station_phone_parse_settings(j,&s));
 cJSON_ReplaceItemInObject(j,"brightness",cJSON_CreateNumber(65));cJSON_ReplaceItemInObject(j,"name",cJSON_CreateString("bad\nname"));assert(!station_phone_parse_settings(j,&s));cJSON_Delete(j);
 char coordinates[64],name[64];j=cJSON_Parse("{\"latitude\":48.81,\"longitude\":2.22,\"name\":\"Home\"}");
 assert(station_phone_parse_home(j,coordinates,name));assert(!strcmp(coordinates,"48.810000,2.220000"));
 cJSON_ReplaceItemInObject(j,"name",cJSON_CreateString("Châtenay-Malabry – L’Haÿ-les-Roses"));assert(station_phone_parse_home(j,coordinates,name));assert(!strcmp(name,"Châtenay-Malabry – L’Haÿ-les-Roses"));
 cJSON_ReplaceItemInObject(j,"latitude",cJSON_CreateNumber(91));assert(!station_phone_parse_home(j,coordinates,name));
 cJSON_ReplaceItemInObject(j,"latitude",cJSON_CreateNull());assert(!station_phone_parse_home(j,coordinates,name));cJSON_Delete(j);
 assert(!station_phone_parse_home(NULL,coordinates,name));assert(!station_phone_parse_settings(NULL,&s));
 puts("PASS: phone protocol bounds, JSON depth, settings ranges, names and location validation");
}
