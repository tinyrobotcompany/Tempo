#include "identity_store.h"
#include "sdkconfig.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#include "esp_psa_its.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#if !CONFIG_MBEDTLS_PSA_ITS_CUSTOM_STORAGE_BACKEND ||     CONFIG_MBEDTLS_PSA_ITS_CUSTOM_BACKEND_UID_MIN != 0x30000001 ||     CONFIG_MBEDTLS_PSA_ITS_CUSTOM_BACKEND_UID_MAX != 0x30000002
#error "Identity keys require the reserved encrypted PSA backend"
#endif
#define MAX_KEY_RECORD 512
static bool ready;
static void wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static const char *key_name(psa_storage_uid_t uid)
{
    return uid==STATION_FACTORY_KEY_ID?"factory":uid==STATION_TLS_KEY_ID?"tls":NULL;
}
static psa_status_t status(esp_err_t err)
{
    if(err==ESP_OK)return PSA_SUCCESS;
    return err==ESP_ERR_NVS_NOT_FOUND?PSA_ERROR_DOES_NOT_EXIST:PSA_ERROR_STORAGE_FAILURE;
}
static psa_status_t load(psa_storage_uid_t uid,void *out,size_t *length)
{
    const char *name=key_name(uid);if(!ready||!name)return PSA_ERROR_STORAGE_FAILURE;
    nvs_handle_t h;esp_err_t err=nvs_open_from_partition("phone_tls","psa_keys",NVS_READONLY,&h);
    if(err!=ESP_OK)return status(err);
    err=nvs_get_blob(h,name,out,length);nvs_close(h);
    if(err==ESP_OK&&(*length==0||*length>MAX_KEY_RECORD))return PSA_ERROR_DATA_CORRUPT;
    return status(err);
}
static psa_status_t store_set(void *ctx,psa_storage_uid_t uid,uint32_t size,const void *data,psa_storage_create_flags_t flags)
{
    (void)ctx;const char *name=key_name(uid);
    if(!ready||!name)return PSA_ERROR_STORAGE_FAILURE;
    if(flags||!data||!size||size>MAX_KEY_RECORD)return PSA_ERROR_INVALID_ARGUMENT;
    nvs_handle_t h;esp_err_t err=nvs_open_from_partition("phone_tls","psa_keys",NVS_READWRITE,&h);
    if(err!=ESP_OK)return status(err);
    err=nvs_set_blob(h,name,data,size);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return status(err);
}
static psa_status_t store_get(void *ctx,psa_storage_uid_t uid,uint32_t offset,uint32_t size,void *out,size_t *written)
{
    (void)ctx;if(!written||(!out&&size))return PSA_ERROR_INVALID_ARGUMENT;*written=0;
    unsigned char data[MAX_KEY_RECORD];size_t length=sizeof(data);
    psa_status_t result=load(uid,data,&length);
    if(result==PSA_SUCCESS){
        if(offset>length)result=PSA_ERROR_INVALID_ARGUMENT;
        else {size_t n=length-offset;if(n>size)n=size;if(n)memcpy(out,data+offset,n);*written=n;}
    }
    wipe(data,sizeof(data));return result;
}
static psa_status_t store_info(void *ctx,psa_storage_uid_t uid,struct psa_storage_info_t *info)
{
    (void)ctx;if(!info)return PSA_ERROR_INVALID_ARGUMENT;
    size_t length=0;psa_status_t result=load(uid,NULL,&length);
    if(result==PSA_SUCCESS){info->size=length;info->flags=0;}return result;
}
static psa_status_t store_remove(void *ctx,psa_storage_uid_t uid)
{
    /* Device identities are not deleted by runtime crypto cleanup. Recovery is
       a separately authorized manufacturing operation, never automatic. */
    (void)ctx;(void)uid;return PSA_ERROR_NOT_PERMITTED;
}
static const esp_psa_its_custom_ops_t backend={.set=store_set,.get=store_get,.get_info=store_info,.remove=store_remove};
esp_err_t station_identity_store_init(void)
{
    if(ready)return ESP_OK;
    /* Check hardware on every build: a debug compiler setting is not permission
       to persist an unprotected manufacturing identity. */
    if(!esp_secure_boot_enabled()||!esp_flash_encryption_enabled())return ESP_ERR_INVALID_STATE;
    int id=CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID;
    if(id<0||id>5)return ESP_ERR_INVALID_STATE;
    esp_efuse_block_t block=(esp_efuse_block_t)(EFUSE_BLK_KEY0+id);
    if(esp_efuse_get_key_purpose(block)!=ESP_EFUSE_KEY_PURPOSE_HMAC_UP||
       !esp_efuse_get_key_dis_read(block)||!esp_efuse_get_key_dis_write(block)||
       !esp_efuse_get_keypurpose_dis_write(block))return ESP_ERR_INVALID_STATE;
    /* Domain-separated hardware-derived XTS keys; never generate or burn an
       HMAC key here. The factory must provision and lock it beforehand. */
    nvs_sec_cfg_t cfg={0};
    static const char encryption[]="Tempo identity NVS encryption v1";
    static const char tweak[]="Tempo identity NVS tweak v1";
    esp_err_t err=esp_hmac_calculate((hmac_key_id_t)id,encryption,sizeof(encryption)-1,cfg.eky);
    if(err==ESP_OK)err=esp_hmac_calculate((hmac_key_id_t)id,tweak,sizeof(tweak)-1,cfg.tky);
    if(err==ESP_OK)err=nvs_flash_secure_init_partition("phone_tls",&cfg);
    wipe(&cfg,sizeof(cfg));if(err!=ESP_OK)return err;
    if(esp_psa_its_register_custom_backend(&backend)!=PSA_SUCCESS)return ESP_ERR_INVALID_STATE;
    ready=true;return ESP_OK;
}
