#include "../firmware/main/phone_tls.c"
#include <assert.h>
#include <stdio.h>
static int init_error,open_error,read_error,parse_error,key_error,pair_error,start_error;
static unsigned starts,stops;static bool wrong_san;
static const char *name="device-a7f3.tempo.tinyrobotcompany.io";
static size_t fake_size;
int nvs_flash_init_partition(const char *p){assert(!strcmp(p,"phone_tls"));return init_error;}
int nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(p,"phone_tls")&&!strcmp(ns,"identity")&&mode==NVS_READONLY);*h=1;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==1);}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){assert(h==1);if(read_error)return read_error;const char *value=!strcmp(key,"hostname")?name:!strcmp(key,"cert")?"certificate":"private key";size_t length=strlen(value)+1;if(!out){*n=fake_size?fake_size:length;return 0;}if(*n<length)return ESP_FAIL;memcpy(out,value,length);*n=length;return 0;}
void mbedtls_pk_init(mbedtls_pk_context *p){(void)p;}void mbedtls_pk_free(mbedtls_pk_context *p){(void)p;}
void mbedtls_x509_crt_init(mbedtls_x509_crt *p){(void)p;}void mbedtls_x509_crt_free(mbedtls_x509_crt *p){(void)p;}
int mbedtls_x509_crt_parse(mbedtls_x509_crt *p,const unsigned char *s,size_t n){p->subject_alt_names=(mbedtls_x509_sequence){.buf={.tag=0x82,.len=strlen(name),.p=(unsigned char *)name}};if(wrong_san)p->subject_alt_names.buf.tag=0x81;assert(n==strlen((const char*)s)+1);return parse_error;}
int mbedtls_pk_parse_key(mbedtls_pk_context *p,const unsigned char *s,size_t n,const unsigned char *pwd,size_t len){(void)p;assert(n==strlen((const char*)s)+1&&!pwd&&!len);return key_error;}
int mbedtls_pk_check_pair(const mbedtls_pk_context *a,const mbedtls_pk_context *b){(void)a;(void)b;return pair_error;}
int httpd_ssl_start(httpd_handle_t *server,const httpd_ssl_config_t *cfg){starts++;assert(cfg->transport_mode==HTTPD_SSL_TRANSPORT_SECURE&&cfg->port_secure==443);assert(cfg->servercert_len==strlen((const char*)cfg->servercert)+1&&cfg->prvtkey_len==strlen((const char*)cfg->prvtkey_pem)+1);*server=(void*)1;return start_error;}
int httpd_ssl_stop(httpd_handle_t server){assert(server==(void*)1);stops++;return 0;}
int main(void){httpd_handle_t server=NULL;httpd_config_t config=HTTPD_DEFAULT_CONFIG();
 assert(!valid_hostname("")&&!valid_hostname("single")&&!valid_hostname("-bad.example")&&!valid_hostname("bad-.example")&&!valid_hostname("bad..example")&&!valid_hostname("bad.example.")&&!valid_hostname("*.example")&&!valid_hostname("UPPER.example"));
 char long_name[120];memset(long_name,'a',sizeof(long_name));long_name[119]=0;assert(!valid_hostname(long_name));long_name[70]='.';long_name[72]=0;assert(!valid_hostname(long_name));
 init_error=-1;assert(station_phone_tls_start(&server,&config)!=0);init_error=0;
 open_error=-1;assert(station_phone_tls_start(&server,&config)!=0);open_error=0;
 read_error=-1;assert(station_phone_tls_start(&server,&config)!=0);read_error=0;
 name="bad..example";assert(station_phone_tls_start(&server,&config)!=0);name="device-a7f3.tempo.tinyrobotcompany.io";
 fake_size=9000;assert(station_phone_tls_start(&server,&config)!=0);fake_size=1;assert(station_phone_tls_start(&server,&config)!=0);fake_size=0;
 parse_error=-1;assert(station_phone_tls_start(&server,&config)!=0);parse_error=0;
 key_error=-1;assert(station_phone_tls_start(&server,&config)!=0);key_error=0;
 pair_error=-1;assert(station_phone_tls_start(&server,&config)!=0);pair_error=0;
 wrong_san=true;assert(station_phone_tls_start(&server,&config)!=0);assert(!starts);wrong_san=false;
 start_error=-1;assert(station_phone_tls_start(&server,&config)!=0&&!station_phone_tls_hostname()[0]);start_error=0;
 assert(station_phone_tls_start(&server,&config)==0&&!strcmp(station_phone_tls_hostname(),name));station_phone_tls_stop(server);assert(stops==1&&!private_key&&!certificate);
 puts("PASS: HTTPS identity failures fail closed; keys match, hostname checked, TLS-only startup");}
