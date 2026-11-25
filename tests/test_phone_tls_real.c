#define ESP_ERR_INVALID_STATE 4
#define CONFIG_TEMPO_DEVICE_SUFFIX "tempo.tinyrobotcompany.io"
/* Real SDK Mbed TLS; only ESP storage/server calls are host seams. */
#include "../firmware/main/phone_tls.c"
#include "../firmware/main/device_identity.c"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
static char *chain,*secret;
static const char *dns;
const char device_roots_start[]="unused for CSR test";
const char device_roots_end[]="";
void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
int nvs_set_str(nvs_handle_t h,const char *name,const char *value){(void)h;(void)value;assert(!strcmp(name,"hostname"));return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return 0;}
int station_identity_store_init(void){return 0;}
int nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h){(void)p;(void)ns;(void)mode;*h=1;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){
 (void)h;if(!chain)return ESP_ERR_NVS_NOT_FOUND;const char *value=!strcmp(key,"hostname")?dns:!strcmp(key,"cert")?chain:secret;
 size_t length=strlen(value)+1;if(out){assert(*n>=length);memcpy(out,value,length);}*n=length;return 0;
}
int httpd_ssl_start(httpd_handle_t *server,const httpd_ssl_config_t *cfg){assert(cfg->servercert_len==strlen(chain)+1&&!memcmp(cfg->servercert,chain,cfg->servercert_len));*server=(void *)1;return 0;}
int httpd_ssl_stop(httpd_handle_t server){(void)server;return 0;}
static void import_test_key(void){
 mbedtls_pk_context key;mbedtls_pk_init(&key);
 assert(mbedtls_pk_parse_key(&key,(unsigned char *)secret,strlen(secret)+1,NULL,0)==0);
 psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
 assert(mbedtls_pk_get_psa_attributes(&key,PSA_KEY_USAGE_SIGN_HASH,&attributes)==0);
 psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_HASH|PSA_KEY_USAGE_VERIFY_HASH);
 psa_set_key_id(&attributes,STATION_TLS_KEY_ID);
 psa_destroy_key(STATION_TLS_KEY_ID);
 mbedtls_svc_key_id_t id=0;assert(mbedtls_pk_import_into_psa(&key,&attributes,&id)==0);
 psa_reset_key_attributes(&attributes);mbedtls_pk_free(&key);
 unsigned char raw[256];size_t n=0;
 assert(psa_export_key(id,raw,sizeof(raw),&n)==PSA_ERROR_NOT_PERMITTED);
 mbedtls_pk_init(&key);assert(mbedtls_pk_wrap_psa(&key,id)==0);
 unsigned char hash[32]={0},sig[80];size_t siglen=0;
 assert(mbedtls_pk_sign(&key,MBEDTLS_MD_SHA256,hash,sizeof(hash),sig,sizeof(sig),&siglen)==0&&siglen>0);
 mbedtls_pk_free(&key);
}
static char *read_file(const char *path){
 FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long n=ftell(file);assert(n>0);
 rewind(file);char *out=calloc(1,(size_t)n+1);assert(out);assert(fread(out,1,(size_t)n,file)==(size_t)n);fclose(file);return out;
}
int main(int argc,char **argv){
 assert(argc==5);assert(psa_crypto_init()==PSA_SUCCESS);
 psa_destroy_key(STATION_FACTORY_KEY_ID);psa_destroy_key(STATION_TLS_KEY_ID);
 assert(station_identity_init()==ESP_OK);
 char first[33];strcpy(first,station_identity_id());
 assert(psa_purge_key(STATION_FACTORY_KEY_ID)==PSA_SUCCESS);
 assert(psa_purge_key(STATION_TLS_KEY_ID)==PSA_SUCCESS);
 assert(station_identity_init()==ESP_OK&&!strcmp(first,station_identity_id()));
 unsigned char raw[512];size_t raw_size=0;
 assert(psa_export_key(STATION_FACTORY_KEY_ID,raw,sizeof(raw),&raw_size)==PSA_ERROR_NOT_PERMITTED);
 assert(psa_export_key(STATION_TLS_KEY_ID,raw,sizeof(raw),&raw_size)==PSA_ERROR_NOT_PERMITTED);
 char request[2048];assert(station_identity_csr(request,sizeof(request))==ESP_OK);
 FILE *csr_file=fopen("generated.csr","wb");assert(csr_file);fputs(request,csr_file);fclose(csr_file);
 char saved_name[sizeof(device_hostname)];strcpy(saved_name,device_hostname);
 memset(device_hostname,'a',96);device_hostname[0]='d';device_hostname[1]='-';device_hostname[34]='.';device_hostname[96]=0;
 assert(station_identity_csr(request,sizeof(request))==ESP_OK);
 csr_file=fopen("generated-max.csr","wb");assert(csr_file);fputs(request,csr_file);fclose(csr_file);
 strcpy(device_hostname,saved_name);
 char signature[112];assert(station_identity_sign("{}",signature,sizeof(signature))==ESP_OK);
 psa_destroy_key(STATION_FACTORY_KEY_ID);psa_destroy_key(STATION_TLS_KEY_ID);
 chain=read_file(argv[1]);secret=read_file(argv[2]);dns="device-test.tempo.tinyrobotcompany.io";
 import_test_key();
 httpd_handle_t server=NULL;httpd_config_t config=HTTPD_DEFAULT_CONFIG();
 assert(station_phone_tls_start(&server,&config)==0);station_phone_tls_stop(server);
 /* The bundle root has this SAN: it must not substitute for the leaf SAN. */
 dns="wrong.tempo.tinyrobotcompany.io";assert(station_phone_tls_start(&server,&config)!=0);
 dns="device-test.tempo.tinyrobotcompany.io";free(secret);secret=read_file(argv[3]);
 import_test_key();assert(station_phone_tls_start(&server,&config)!=0);
 free(secret);secret=read_file(argv[2]);free(chain);chain=read_file(argv[4]);
 assert(station_phone_tls_start(&server,&config)!=0);
 psa_destroy_key(STATION_TLS_KEY_ID);free(chain);free(secret);puts("PASS: real CA-issued leaf accepted; wrong SAN, key and malformed certificate rejected");
}
