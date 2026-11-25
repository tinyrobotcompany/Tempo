#pragma once
#include "pk.h"
#include <stdint.h>
typedef struct {int year,mon,day,hour,min,sec;} mbedtls_x509_time;
typedef struct {mbedtls_pk_context pk;mbedtls_x509_time valid_from,valid_to;} mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *,const unsigned char *,size_t);
int mbedtls_x509_crt_verify(mbedtls_x509_crt *,mbedtls_x509_crt *,void *,const char *,uint32_t *,void *,void *);
