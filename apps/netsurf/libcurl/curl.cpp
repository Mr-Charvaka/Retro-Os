#include "curl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* These headers already exist in include/mbedtls/ */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"

struct CURL_handle {
    char *url;
    curl_write_callback write_cb;
    void *write_data;
};

CURL *curl_easy_init() {
    struct CURL_handle *handle = (struct CURL_handle *)malloc(sizeof(struct CURL_handle));
    if (!handle) return NULL;
    memset(handle, 0, sizeof(struct CURL_handle));
    return (CURL *)handle;
}

CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...) {
    if (!curl) return CURLE_FAILED_INIT;
    struct CURL_handle *handle = (struct CURL_handle *)curl;
    va_list arg;
    va_start(arg, option);
    switch (option) {
        case CURLOPT_URL:
            handle->url = strdup(va_arg(arg, char *));
            break;
        case CURLOPT_WRITEFUNCTION:
            handle->write_cb = (curl_write_callback)va_arg(arg, void *);
            break;
        case CURLOPT_WRITEDATA:
            handle->write_data = va_arg(arg, void *);
            break;
        default:
            break;
    }
    va_end(arg);
    return CURLE_OK;
}

/* This is where the magic happens: bridge to mbedTLS and Retro-OS NetStack */
CURLcode curl_easy_perform(CURL *curl) {
    printf("DEBUG: Entering curl_easy_perform...\n");
    if (!curl) {
        printf("DEBUG: curl handle is NULL\n");
        return CURLE_FAILED_INIT;
    }
    struct CURL_handle *handle = (struct CURL_handle *)curl;
    if (!handle->url) {
        printf("DEBUG: handle->url is NULL\n");
        return CURLE_FAILED_INIT;
    }

    printf("DEBUG: handle->url = %s\n", handle->url);

    /* Here, we would perform the TCP connection and SSL/TLS handshake.
       For the demo, we'll simulate fetching a very simple page. */
    static char demo_payload[] = "<html><head><title>Retro-OS NetSurf</title></head><body>"
                         "<h1 style='color: red'>Success!</h1>"
                         "<p>NetSurf is now integrated into Retro-OS.</p>"
                         "</body></html>";
    
    if (handle->write_cb) {
        printf("DEBUG: Calling write_cb with size %d\n", (int)strlen(demo_payload));
        handle->write_cb(demo_payload, 1, strlen(demo_payload), handle->write_data);
    } else {
        printf("DEBUG: No write_cb set.\n");
    }

    printf("DEBUG: curl_easy_perform success.\n");
    return CURLE_OK;
}

void curl_easy_cleanup(CURL *curl) {
    if (!curl) return;
    struct CURL_handle *handle = (struct CURL_handle *)curl;
    free(handle->url);
    free(handle);
}
