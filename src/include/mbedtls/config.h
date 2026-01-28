#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#include "../types.h" // For size_t

/* System support */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_EXIT_ALT

/* Required for SSL/TLS 1.2 Client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_CIPHER_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#define MBEDTLS_PLATFORM_ENTROPY

/* Added dependencies for PK_C, RSA, and ECC */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_GENPRIME
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_GCM

/* Ciphersuites */
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* Disable things that need OS headers */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_SELF_TEST

/* Retro-OS specific glue */
#ifdef __cplusplus
extern "C" {
#endif
void *retroos_calloc(size_t nmemb, size_t size);
void retroos_free(void *ptr);
int retroos_printf(const char *format, ...);
void mbedtls_platform_exit(int status);
int retroos_snprintf(char *str, size_t size, const char *format, ...);
#ifdef __cplusplus
}
#endif

#define MBEDTLS_PLATFORM_CALLOC_MACRO retroos_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO retroos_free
#define MBEDTLS_PLATFORM_PRINTF_MACRO retroos_printf
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO retroos_snprintf

#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
