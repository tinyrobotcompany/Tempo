#include "pairing.h"
#include "nvs.h"
#include "esp_random.h"
#include <string.h>
#include <stdbool.h>
static bool valid(const char *token,size_t size)
{
    if(size!=33 || token[32]!=0)return false;
    for(unsigned i=0;i<32;i++)
        if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return false;
    return true;
}
esp_err_t station_pairing_load(char token[33])
{
    nvs_handle_t handle;
    token[0]=0;
    esp_err_t err=nvs_open("tempo_pair",NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    size_t size=33;
    err=nvs_get_str(handle,"token",token,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND){
        unsigned char random[16];static const char hex[]="0123456789abcdef";
        esp_fill_random(random,sizeof(random));
        for(unsigned i=0;i<16;i++){token[i*2]=hex[random[i]>>4];token[i*2+1]=hex[random[i]&15];}
        token[32]=0;
        err=nvs_set_str(handle,"token",token);
        if(err==ESP_OK)err=nvs_commit(handle);
    }else if(err==ESP_OK&&!valid(token,size))err=ESP_FAIL;
    nvs_close(handle);
    if(err!=ESP_OK)memset(token,0,33); /* Never serve an unpersisted or corrupt credential. */
    return err;
}
