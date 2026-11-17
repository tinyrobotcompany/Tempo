#include "credentials.h"
#include "nvs.h"
#include <string.h>
#include <stdatomic.h>
static atomic_bool configured;
bool station_credential_configured(void){return atomic_load(&configured);}
bool station_credential_get(char *out, size_t size)
{
    if(!out || size < STATION_PRIM_KEY_MAX+1) return false;
    out[0]=0;
    nvs_handle_t h;
    if(nvs_open("tempo_secret",NVS_READONLY,&h)!=ESP_OK) { atomic_store(&configured,false); return false; }
    size_t length=size;
    esp_err_t err=nvs_get_str(h,"prim",out,&length);
    nvs_close(h);
    if(err!=ESP_OK || !length || length>size || !out[0]) { memset(out,0,size); atomic_store(&configured,false); return false; }
    atomic_store(&configured,true);
    return true;
}
esp_err_t station_credential_set(const char *key)
{
    if(!key || strlen(key)>STATION_PRIM_KEY_MAX) return ESP_ERR_INVALID_ARG;
    for(const unsigned char *p=(const unsigned char *)key;*p;p++)
        if(*p<33 || *p>126) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err=nvs_open("tempo_secret",NVS_READWRITE,&h);
    if(err!=ESP_OK) return err;
    err=nvs_set_str(h,"prim",key);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK)atomic_store(&configured,key[0]!=0);
    return err;
}
