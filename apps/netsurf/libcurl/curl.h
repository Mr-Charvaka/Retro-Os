#ifndef CURL_H
#define CURL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Minimal LibCURL types for NetSurf */
typedef void CURL;
typedef enum {
    CURLE_OK = 0,
    CURLE_FAILED_INIT,
    CURLE_COULDNT_CONNECT,
    CURLE_COULDNT_RESOLVE_HOST,
    CURLE_SSL_CONNECT_ERROR
} CURLcode;

typedef enum {
    CURLOPT_URL,
    CURLOPT_WRITEFUNCTION,
    CURLOPT_WRITEDATA,
    CURLOPT_SSL_VERIFYPEER,
    CURLOPT_SSL_VERIFYHOST,
    CURLOPT_USERAGENT
} CURLoption;

/* Callback function type for receiving data */
typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb, void *userdata);

CURL *curl_easy_init();
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
void curl_easy_cleanup(CURL *curl);

#endif /* CURL_H */
