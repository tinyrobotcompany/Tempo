#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int psa_key_attributes_t;
typedef int psa_status_t;
typedef int mbedtls_svc_key_id_t;
#define PSA_KEY_ATTRIBUTES_INIT 0
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
