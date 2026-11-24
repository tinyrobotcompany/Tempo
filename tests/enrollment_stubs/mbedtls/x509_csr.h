#pragma once
#include "pk.h"
typedef struct {int unused;} mbedtls_x509write_csr;
void mbedtls_x509write_csr_init(mbedtls_x509write_csr *);
void mbedtls_x509write_csr_free(mbedtls_x509write_csr *);
void mbedtls_x509write_csr_set_key(mbedtls_x509write_csr *,mbedtls_pk_context *);
void mbedtls_x509write_csr_set_md_alg(mbedtls_x509write_csr *,int);
int mbedtls_x509write_csr_set_subject_name(mbedtls_x509write_csr *,const char *);
int mbedtls_x509write_csr_set_extension(mbedtls_x509write_csr *,const char *,size_t,int,const unsigned char *,size_t);
int mbedtls_x509write_csr_pem(mbedtls_x509write_csr *,unsigned char *,size_t);
