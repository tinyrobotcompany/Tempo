#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {unsigned id,type,bits,usage,alg,lifetime;} psa_key_attributes_t;
typedef int psa_status_t;
typedef unsigned mbedtls_svc_key_id_t;
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define PSA_KEY_TYPE_ECC_KEY_PAIR(x) (x)
#define PSA_ECC_FAMILY_SECP_R1 1
#define PSA_KEY_USAGE_SIGN_HASH 1
#define PSA_KEY_USAGE_VERIFY_HASH 2
#define PSA_KEY_USAGE_EXPORT 4
#define PSA_ALG_SHA_256 1
#define PSA_ALG_ECDSA(x) (x)
#define PSA_SUCCESS 0
void psa_set_key_type(psa_key_attributes_t *,int);
void psa_set_key_bits(psa_key_attributes_t *,int);
void psa_set_key_usage_flags(psa_key_attributes_t *,int);
void psa_set_key_algorithm(psa_key_attributes_t *,int);
void psa_reset_key_attributes(psa_key_attributes_t *);
int psa_crypto_init(void);
int psa_generate_key(psa_key_attributes_t *,mbedtls_svc_key_id_t *);
int psa_destroy_key(mbedtls_svc_key_id_t);
int psa_hash_compute(int,const unsigned char *,size_t,unsigned char *,size_t,size_t *);

#define PSA_ERROR_DOES_NOT_EXIST -140
#define PSA_KEY_LIFETIME_PERSISTENT 1
#define PSA_KEY_USAGE_SIGN_MESSAGE 8
#define PSA_KEY_USAGE_VERIFY_MESSAGE 16
void psa_set_key_id(psa_key_attributes_t *,unsigned);
int psa_get_key_attributes(unsigned,psa_key_attributes_t *);
unsigned psa_get_key_type(const psa_key_attributes_t *);
unsigned psa_get_key_bits(const psa_key_attributes_t *);
unsigned psa_get_key_lifetime(const psa_key_attributes_t *);
unsigned psa_get_key_algorithm(const psa_key_attributes_t *);
unsigned psa_get_key_usage_flags(const psa_key_attributes_t *);

#define PSA_ERROR_INVALID_HANDLE -136
