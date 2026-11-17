#include "../firmware/main/pairing.c"
#include <assert.h>
#include <stdio.h>
static char stored[33],pending[33];static int open_error,read_error,write_error,commit_error;static unsigned random_calls;
int nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"tempo_pair")&&mode==NVS_READWRITE);*h=1;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==1);}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size){(void)h;assert(!strcmp(key,"token"));if(read_error)return read_error;if(!stored[0])return ESP_ERR_NVS_NOT_FOUND;strcpy(out,stored);*size=strlen(stored)+1;return ESP_OK;}
int nvs_set_str(nvs_handle_t h,const char *key,const char *value){(void)h;assert(!strcmp(key,"token"));if(write_error)return write_error;strcpy(pending,value);return ESP_OK;}
int nvs_commit(nvs_handle_t h){(void)h;if(commit_error)return commit_error;strcpy(stored,pending);return ESP_OK;}
void esp_fill_random(void *out,size_t size){random_calls++;memset(out,0xab,size);}
int main(void){char token[33],first[33];
 assert(station_pairing_load(token)==ESP_OK);assert(strlen(token)==32&&random_calls==1);strcpy(first,token);
 assert(station_pairing_load(token)==ESP_OK&&!strcmp(token,first)&&random_calls==1);
 stored[3]='z';assert(station_pairing_load(token)==ESP_FAIL&&!token[0]);
 strcpy(stored,"invalid");assert(station_pairing_load(token)==ESP_FAIL&&!token[0]);
 stored[0]=0;open_error=-1;assert(station_pairing_load(token)==-1&&!token[0]);open_error=0;
 read_error=-1;assert(station_pairing_load(token)==-1&&!token[0]);read_error=0;
 write_error=-1;assert(station_pairing_load(token)==-1&&!token[0]);write_error=0;
 commit_error=-1;assert(station_pairing_load(token)==-1&&!token[0]);commit_error=0;
 assert(station_pairing_load(token)==ESP_OK&&!strcmp(token,first));
 puts("PASS: pairing persists across restart; corrupt/read/write/commit failures fail closed");
}
