#include "../firmware/main/credentials.c"
#include <assert.h>
#include <stdio.h>
static char stored[513],pending[513];static int open_error,read_error,write_error,commit_error;
int nvs_open(const char *n,int mode,nvs_handle_t *h){assert(!strcmp(n,"tempo_secret"));(void)mode;*h=1;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==1);}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size){(void)h;assert(!strcmp(key,"prim"));if(read_error)return read_error;assert(*size>strlen(stored));strcpy(out,stored);*size=strlen(stored)+1;return 0;}
int nvs_set_str(nvs_handle_t h,const char *key,const char *value){(void)h;assert(!strcmp(key,"prim"));if(write_error)return write_error;strcpy(pending,value);return 0;}
int nvs_commit(nvs_handle_t h){(void)h;if(commit_error)return commit_error;strcpy(stored,pending);return 0;}
int main(void){char key[513];assert(!station_credential_configured());assert(!station_credential_get(NULL,513));assert(!station_credential_get(key,2));assert(!station_credential_get(key,sizeof(key)));
 assert(station_credential_set(NULL)!=0);assert(station_credential_set("has space")!=0);assert(station_credential_set("bad\n")!=0);
 char long_key[515];memset(long_key,'a',514);long_key[514]=0;assert(station_credential_set(long_key)!=0);
 assert(station_credential_set("fixture-123")==0);assert(station_credential_configured());assert(station_credential_get(key,sizeof(key))&&!strcmp(key,"fixture-123"));
 open_error=-1;assert(!station_credential_get(key,sizeof(key))&&key[0]==0);assert(station_credential_set("new")!=0);open_error=0;
 read_error=-1;assert(!station_credential_get(key,sizeof(key))&&key[0]==0);read_error=0;
 write_error=-1;assert(station_credential_set("new")!=0);write_error=0;commit_error=-1;assert(station_credential_set("new")!=0);commit_error=0;
 assert(station_credential_get(key,sizeof(key))&&!strcmp(key,"fixture-123"));assert(station_credential_set("")==0);assert(!station_credential_get(key,sizeof(key)));
 puts("PASS: credential validation, secret-only namespace, persistence, erase and failed-write preservation");}
