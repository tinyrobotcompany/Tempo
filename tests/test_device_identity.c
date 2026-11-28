#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
static time_t fake_time(time_t *out){time_t now=1800000000;if(out)*out=now;return now;}
#define time fake_time
#include "../firmware/main/device_identity.c"
#undef time
const char device_roots_start[]="roots";
const char device_roots_end[]="";
static bool encrypted=true,secure=true;
static int storage_error,crypto_error,commit_error,parse_error,verify_error;
static char stored_cert[8192];
static psa_key_attributes_t saved_keys[2];
static unsigned generated,commits,public_logs;
bool esp_flash_encryption_enabled(void){return encrypted;}
bool esp_secure_boot_enabled(void){return secure;}
void test_log(const char *tag,const char *format,...){(void)tag;(void)format;public_logs++;}
int station_identity_store_init(void){return storage_error;}
int nvs_open_from_partition(const char *p,const char *space,int mode,nvs_handle_t *out){(void)mode;assert(!strcmp(p,"phone_tls"));*out=!strcmp(space,"factory")?1:2;return storage_error;}
int nvs_get_str(nvs_handle_t h,const char *name,char *out,size_t *size){
 (void)h;assert(!strcmp(name,"cert"));const char *value=stored_cert;
 if(!value[0])return ESP_ERR_NVS_NOT_FOUND;
 size_t n=strlen(value)+1;if(*size<n)return ESP_FAIL;memcpy(out,value,n);*size=n;return 0;
}
int nvs_set_str(nvs_handle_t h,const char *name,const char *value){
 if(commit_error)return commit_error;
 (void)h;assert(strcmp(name,"key"));
 if(!strcmp(name,"cert"))strcpy(stored_cert,value);
 return 0;
}
int nvs_commit(nvs_handle_t h){(void)h;commits++;return commit_error;}
void nvs_close(nvs_handle_t h){(void)h;}
void psa_set_key_id(psa_key_attributes_t *a,unsigned id){a->id=id;a->lifetime=1;}
void psa_set_key_type(psa_key_attributes_t *a,int v){a->type=v;}
void psa_set_key_bits(psa_key_attributes_t *a,int v){a->bits=v;}
void psa_set_key_usage_flags(psa_key_attributes_t *a,int v){assert(v==3);a->usage=v|24;}
void psa_set_key_algorithm(psa_key_attributes_t *a,int v){a->alg=v;}
void psa_reset_key_attributes(psa_key_attributes_t *a){memset(a,0,sizeof(*a));}
int psa_crypto_init(void){return crypto_error;}
static unsigned key_index(unsigned id){assert(id==STATION_FACTORY_KEY_ID||id==STATION_TLS_KEY_ID);return id-STATION_FACTORY_KEY_ID;}
int psa_get_key_attributes(unsigned id,psa_key_attributes_t *a){*a=saved_keys[key_index(id)];return parse_error?parse_error:a->id?0:PSA_ERROR_INVALID_HANDLE;}
unsigned psa_get_key_type(const psa_key_attributes_t *a){return a->type;}
unsigned psa_get_key_bits(const psa_key_attributes_t *a){return a->bits;}
unsigned psa_get_key_lifetime(const psa_key_attributes_t *a){return a->lifetime;}
unsigned psa_get_key_algorithm(const psa_key_attributes_t *a){return a->alg;}
unsigned psa_get_key_usage_flags(const psa_key_attributes_t *a){return a->usage;}
int psa_generate_key(psa_key_attributes_t *a,mbedtls_svc_key_id_t *id){assert(a->usage==27&&a->bits==256&&a->lifetime==1);if(crypto_error)return crypto_error;saved_keys[key_index(a->id)]=*a;*id=a->id;generated++;return 0;}
int psa_hash_compute(int alg,const unsigned char *data,size_t length,unsigned char *out,size_t capacity,size_t *written){
 assert(alg==1&&data&&length&&capacity==32);memset(out,0xab,32);*written=32;return crypto_error;
}
void mbedtls_pk_init(mbedtls_pk_context *p){memset(p,0,sizeof(*p));}
void mbedtls_pk_free(mbedtls_pk_context *p){(void)p;}
int mbedtls_pk_parse_key(mbedtls_pk_context *p,const unsigned char *s,size_t n,const unsigned char *pw,size_t pwlen){(void)p;assert(n==strlen((const char*)s)+1&&!pw&&!pwlen);return parse_error;}
int mbedtls_pk_wrap_psa(mbedtls_pk_context *out,unsigned key){(void)out;assert(saved_keys[key_index(key)].id==key);return crypto_error;}
int mbedtls_pk_write_key_pem(mbedtls_pk_context *key,unsigned char *out,size_t size){(void)key;assert(size>=8);strcpy((char*)out,"private");return crypto_error;}
int mbedtls_pk_write_pubkey_pem(mbedtls_pk_context *key,unsigned char *out,size_t size){(void)key;assert(size>=7);strcpy((char*)out,"public");return crypto_error;}
int mbedtls_pk_write_pubkey_der(mbedtls_pk_context *key,unsigned char *out,size_t size){(void)key;out[size-1]=1;return crypto_error?-1:1;}
int mbedtls_pk_sign(mbedtls_pk_context *key,int alg,const unsigned char *hash,size_t hashlen,unsigned char *out,size_t size,size_t *written){(void)key;(void)hash;assert(alg==1&&hashlen==32&&size>=70);memset(out,1,70);*written=70;return crypto_error;}
int mbedtls_base64_encode(unsigned char *out,size_t capacity,size_t *written,const unsigned char *in,size_t length){(void)in;assert(length==70);if(capacity<10)return -1;strcpy((char*)out,"signature");*written=9;return crypto_error;}
void mbedtls_x509write_csr_init(mbedtls_x509write_csr *p){(void)p;}
void mbedtls_x509write_csr_free(mbedtls_x509write_csr *p){(void)p;}
void mbedtls_x509write_csr_set_key(mbedtls_x509write_csr *c,mbedtls_pk_context *k){(void)c;(void)k;}
void mbedtls_x509write_csr_set_md_alg(mbedtls_x509write_csr *c,int alg){(void)c;assert(alg==1);}
int mbedtls_x509write_csr_set_subject_name(mbedtls_x509write_csr *c,const char *subject){(void)c;assert(!strncmp(subject,"CN=d-",5));return crypto_error;}
int mbedtls_x509write_csr_set_extension(mbedtls_x509write_csr *c,const char *oid,size_t oidlen,int critical,const unsigned char *value,size_t size){(void)c;assert(!memcmp(oid,"\x55\x1d\x11",3)&&oidlen==3&&!critical);assert(value[0]==0x30&&value[2]==0x82&&size==strlen(station_identity_hostname())+4);return crypto_error;}
int mbedtls_x509write_csr_pem(mbedtls_x509write_csr *c,unsigned char *out,size_t size){(void)c;if(size<4)return -1;strcpy((char*)out,"csr");return crypto_error;}
void mbedtls_x509_crt_init(mbedtls_x509_crt *c){memset(c,0,sizeof(*c));}
void mbedtls_x509_crt_free(mbedtls_x509_crt *c){(void)c;}
int mbedtls_x509_crt_parse(mbedtls_x509_crt *c,const unsigned char *input,size_t size){(void)input;(void)size;c->valid_from=(mbedtls_x509_time){2027,1,1,0,0,0};c->valid_to=(mbedtls_x509_time){2027,3,1,0,0,0};return parse_error;}
int mbedtls_pk_check_pair(const mbedtls_pk_context *a,const mbedtls_pk_context *b){(void)a;(void)b;return crypto_error;}
int mbedtls_x509_crt_verify(mbedtls_x509_crt *c,mbedtls_x509_crt *roots,void *crl,const char *name,uint32_t *flags,void *cb,void *ctx){assert(c!=roots&&!crl&&!strcmp(name,station_identity_hostname())&&!cb&&!ctx);*flags=verify_error?1:0;return verify_error;}
int main(void){
 const char *bad_dns[]={"",".test","test.","a..test","-a.test","a-.test",
   "a,b.test","a+b.test","a;test","a=bad.test","a\\b.test","a\"b.test","a b.test","a_b.test"};
 for(size_t i=0;i<sizeof(bad_dns)/sizeof(bad_dns[0]);i++)assert(!dns_name_valid(bad_dns[i]));
 assert(dns_name_valid("A-z0.tempo.test"));
 char label[70];memset(label,'a',63);strcpy(label+63,".test");assert(dns_name_valid(label));
 memset(label,'a',64);strcpy(label+64,".test");assert(!dns_name_valid(label));

 setenv("TZ","EST5EDT",1);tzset();
 assert(epoch(&(mbedtls_x509_time){1970,1,1,0,0,0})==0);
 assert(epoch(&(mbedtls_x509_time){1969,12,31,0,0,0})==-86400);
 assert(epoch(&(mbedtls_x509_time){2000,2,29,0,0,0})==951782400);
 assert(epoch(&(mbedtls_x509_time){2100,3,1,0,0,0})==4107542400LL);
 assert(epoch(&(mbedtls_x509_time){2100,2,29,0,0,0})==-1);
 assert(epoch(&(mbedtls_x509_time){0})==-1);
 encrypted=false;assert(station_identity_init()==ESP_ERR_INVALID_STATE&&!generated);encrypted=true;
 secure=false;assert(station_identity_init()==ESP_ERR_INVALID_STATE&&!generated);secure=true;
 storage_error=-1;assert(station_identity_init()!=0&&!generated);storage_error=0;
 crypto_error=-1;assert(station_identity_init()!=0&&!generated);crypto_error=0;
 assert(station_identity_init()==0&&generated==2);assert(strlen(station_identity_id())==32);assert(!strncmp(station_identity_hostname(),"d-abab",6));
 assert(station_identity_init()==0&&generated==2); /* repeated initialization does not replace keys */
 assert(public_logs==1);
 saved_keys[0].usage|=PSA_KEY_USAGE_EXPORT;assert(station_identity_init()!=0&&generated==2);saved_keys[0].usage&=~PSA_KEY_USAGE_EXPORT;
 parse_error=-1;assert(station_identity_init()!=0&&generated==2);parse_error=0;
 char output[2048];assert(station_identity_csr(output,sizeof(output))==0&&!strcmp(output,"csr"));
 crypto_error=-1;assert(station_identity_csr(output,sizeof(output))!=0);crypto_error=0;
 assert(station_identity_csr(output,1)!=0);
 assert(station_identity_csr(NULL,32)==ESP_ERR_INVALID_ARG);
 char original[sizeof(device_hostname)];strcpy(original,device_hostname);
 memset(device_hostname,'a',sizeof(device_hostname)-1);device_hostname[0]='d';device_hostname[1]='-';device_hostname[34]='.';device_hostname[96]=0;
 assert(station_identity_csr(output,sizeof(output))==ESP_OK);
 device_hostname[0]=0;assert(station_identity_csr(output,sizeof(output))==ESP_ERR_INVALID_ARG);strcpy(device_hostname,original);
 assert(station_identity_sign(NULL,output,sizeof(output))==ESP_ERR_INVALID_ARG);
 assert(station_identity_sign("{}",NULL,sizeof(output))==ESP_ERR_INVALID_ARG);
 assert(station_identity_sign("{}",output,0)==ESP_ERR_INVALID_ARG);
 assert(station_identity_sign("{}",output,sizeof(output))==0&&!strcmp(output,"signature"));
 assert(station_identity_sign("{}",output,1)!=0);
 crypto_error=-1;assert(station_identity_sign("{}",output,sizeof(output))!=0);crypto_error=0;
 assert(station_identity_install(NULL,NULL)==ESP_ERR_INVALID_ARG);
 int64_t no_cert=station_identity_expires_at();
 assert(station_identity_install("chain",NULL)==ESP_ERR_INVALID_ARG&&!stored_cert[0]);
 int64_t renewed=0;assert(station_identity_install("",&renewed)!=0);
 verify_error=-1;assert(station_identity_install("chain",&renewed)!=0&&!stored_cert[0]);verify_error=0;
 parse_error=-1;assert(station_identity_install("chain",&renewed)!=0);parse_error=0;
 commit_error=-1;assert(station_identity_install("chain",&renewed)!=0&&!station_identity_changed());commit_error=0;
 assert(station_identity_expires_at()==no_cert); /* Rejected or unsaved certificates never change expiry. */
 assert(station_identity_install("chain",&renewed)==0&&renewed>0&&station_identity_renew_after()==renewed);
 assert(station_identity_expires_at()==epoch(&(mbedtls_x509_time){2027,3,1,0,0,0}));
 assert(station_identity_install("chain",&renewed)==0); /* Duplicate delivery must preserve pending reload. */
 assert(station_identity_changed()&&!station_identity_changed());assert(station_identity_init()==0&&station_identity_renew_after()==renewed&&station_identity_expires_at()==epoch(&(mbedtls_x509_time){2027,3,1,0,0,0}));
 assert(station_identity_install("chain",&renewed)==0&&!station_identity_changed());
 int64_t eastern=renewed;setenv("TZ","JST-9",1);tzset();
 assert(station_identity_install("chain",&renewed)==0&&renewed==eastern);
 puts("PASS: on-device identity persistence, hardware guard, CSR/signing flow and verified certificate install");
}
