/* Real SDK Mbed TLS; only ESP storage/server calls are host seams. */
#include "../firmware/main/phone_tls.c"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
static char *chain,*secret;
static const char *dns;
int nvs_flash_init_partition(const char *p){(void)p;return 0;}
int nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h){(void)p;(void)ns;(void)mode;*h=1;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){
 (void)h;const char *value=!strcmp(key,"hostname")?dns:!strcmp(key,"cert")?chain:secret;
 size_t length=strlen(value)+1;if(out){assert(*n>=length);memcpy(out,value,length);}*n=length;return 0;
}
int httpd_ssl_start(httpd_handle_t *server,const httpd_ssl_config_t *cfg){(void)cfg;*server=(void *)1;return 0;}
int httpd_ssl_stop(httpd_handle_t server){(void)server;return 0;}
static char *read_file(const char *path){
 FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long n=ftell(file);assert(n>0);
 rewind(file);char *out=calloc(1,(size_t)n+1);assert(out);assert(fread(out,1,(size_t)n,file)==(size_t)n);fclose(file);return out;
}
int main(int argc,char **argv){
 assert(argc==5);assert(psa_crypto_init()==PSA_SUCCESS);
 chain=read_file(argv[1]);secret=read_file(argv[2]);dns="device-test.tempo.tinyrobotcompany.io";
 httpd_handle_t server=NULL;httpd_config_t config=HTTPD_DEFAULT_CONFIG();
 assert(station_phone_tls_start(&server,&config)==0);station_phone_tls_stop(server);
 dns="wrong.tempo.tinyrobotcompany.io";assert(station_phone_tls_start(&server,&config)!=0);
 dns="device-test.tempo.tinyrobotcompany.io";free(secret);secret=read_file(argv[3]);
 assert(station_phone_tls_start(&server,&config)!=0);
 free(secret);secret=read_file(argv[2]);free(chain);chain=read_file(argv[4]);
 assert(station_phone_tls_start(&server,&config)!=0);
 free(chain);free(secret);puts("PASS: real CA-issued leaf accepted; wrong SAN, key and malformed certificate rejected");
}
