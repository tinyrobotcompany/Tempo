#pragma once
#include <stddef.h>
typedef struct {int unused;} mbedtls_pk_context;
#define MBEDTLS_MD_SHA256 1
void mbedtls_pk_init(mbedtls_pk_context *);
void mbedtls_pk_free(mbedtls_pk_context *);
int mbedtls_pk_parse_key(mbedtls_pk_context *,const unsigned char *,size_t,const unsigned char *,size_t);
int mbedtls_pk_check_pair(const mbedtls_pk_context *,const mbedtls_pk_context *);
int mbedtls_pk_copy_from_psa(int,mbedtls_pk_context *);
int mbedtls_pk_write_key_pem(mbedtls_pk_context *,unsigned char *,size_t);
int mbedtls_pk_write_pubkey_pem(mbedtls_pk_context *,unsigned char *,size_t);
int mbedtls_pk_write_pubkey_der(mbedtls_pk_context *,unsigned char *,size_t);
int mbedtls_pk_sign(mbedtls_pk_context *,int,const unsigned char *,size_t,unsigned char *,size_t,size_t *);
