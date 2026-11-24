#include "phone_tls.h"
#include "identity_store.h"
#include "esp_key_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/version.h"
#include "esp_random.h"
#include "sdkconfig.h"
#if CONFIG_TEMPO_PRODUCTION_IDENTITY
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#endif
#include <stdlib.h>
#include <string.h>

/* Separate from settings and OTA metadata; never erase on an initialization error. */
static char hostname[97];
static char *certificate;
static const esp_key_config_t tls_key={.source=ESP_KEY_SOURCE_PSA,.psa={.key_id=STATION_TLS_KEY_ID}};
static void clear_identity(void)
{
    free(certificate);certificate=NULL;hostname[0]=0;
}
static bool valid_hostname(const char *name)
{
    size_t n=strlen(name),label=0;bool dot=false;
    if(!n||n>96)return false;
    for(size_t i=0;i<n;i++){
        char c=name[i];
        if(c=='.'){if(!label||name[i-1]=='-')return false;label=0;dot=true;continue;}
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')||(!label&&c=='-')||++label>63)return false;
    }
    return dot&&label&&name[n-1]!='-';
}
static esp_err_t read_pem(nvs_handle_t h,const char *name,size_t maximum,char **out)
{
    size_t n=0;esp_err_t err=nvs_get_str(h,name,NULL,&n);
    if(err!=ESP_OK)return err;
    if(n<2||n>maximum)return ESP_ERR_INVALID_ARG;
    *out=calloc(1,n);if(!*out)return ESP_ERR_NO_MEM;
    err=nvs_get_str(h,name,*out,&n);
    return err;
}
static bool matches_identity(void)
{
    mbedtls_x509_crt cert;mbedtls_pk_context key;
    mbedtls_x509_crt_init(&cert);mbedtls_pk_init(&key);
    int result=mbedtls_x509_crt_parse(&cert,(const unsigned char *)certificate,strlen(certificate)+1);
    if(result==0){
        result=mbedtls_pk_wrap_psa(&key,STATION_TLS_KEY_ID);
        if(result==0)result=mbedtls_pk_check_pair(&cert.pk,&key);
    }
    if(result==0){
        /* Issuer trust is checked before installation. At startup require the
           exact DNS SAN and key pair; a normal CA-issued leaf is not its own CA.
           Validity dates are enforced by the browser, including before SNTP. */
        /* cert is the first PEM certificate; issuers live in cert.next.
           This sequence contains only names on that first certificate. */
        const mbedtls_x509_sequence *san=&cert.subject_alt_names;
        result=-1;
        for(;san;san=san->next){
            if(san->buf.tag==0x82 && san->buf.len==strlen(hostname) &&
               !memcmp(san->buf.p,hostname,san->buf.len)){result=0;break;}
        }
    }
    mbedtls_pk_free(&key);mbedtls_x509_crt_free(&cert);return result==0;
}
esp_err_t station_phone_tls_start(httpd_handle_t *server,const httpd_config_t *config)
{
#if CONFIG_TEMPO_PRODUCTION_IDENTITY
    if(!esp_flash_encryption_enabled()||!esp_secure_boot_enabled())return ESP_ERR_INVALID_STATE;
#endif
    clear_identity();
    esp_err_t err=station_identity_store_init();if(err!=ESP_OK)return err;
    nvs_handle_t h;err=nvs_open_from_partition("phone_tls","identity",NVS_READONLY,&h);if(err!=ESP_OK)return err;
    size_t n=sizeof(hostname);err=nvs_get_str(h,"hostname",hostname,&n);
    if(err==ESP_OK&&!valid_hostname(hostname))err=ESP_ERR_INVALID_ARG;
    if(err==ESP_OK)err=read_pem(h,"cert",8192,&certificate);
    nvs_close(h);
    if(err==ESP_OK&&!matches_identity())err=ESP_ERR_INVALID_ARG;
    if(err!=ESP_OK){clear_identity();return err;}
    httpd_ssl_config_t ssl=HTTPD_SSL_CONFIG_DEFAULT();ssl.httpd=*config;
    ssl.transport_mode=HTTPD_SSL_TRANSPORT_SECURE;ssl.port_secure=443;
    /* ESP-IDF passes this entire PEM chain to ssl_conf_own_cert.
       cacert_pem is for client authentication, not server intermediates. */
    ssl.servercert=(const unsigned char *)certificate;ssl.servercert_len=strlen(certificate)+1;
    ssl.server_key=&tls_key;
    err=httpd_ssl_start(server,&ssl);
    if(err!=ESP_OK)clear_identity();
    return err;
}
esp_err_t station_phone_tls_stop(httpd_handle_t server){esp_err_t err=httpd_ssl_stop(server);if(err==ESP_OK)clear_identity();return err;}
const char *station_phone_tls_hostname(void){return hostname;}
