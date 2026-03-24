#include "curl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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

/* This is where the magic happens: bridge to Retro-OS NetStack */
CURLcode curl_easy_perform(CURL *curl) {
    if (!curl) return CURLE_FAILED_INIT;
    struct CURL_handle *handle = (struct CURL_handle *)curl;
    if (!handle->url) return CURLE_FAILED_INIT;

    printf("NET: Fetching REAL URL: %s\n", handle->url);

    // Allocate a buffer for the response (128KB for now)
    size_t buffer_size = 128 * 1024;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    if (!buffer) return CURLE_FAILED_INIT;

    // Call SYS_HTTP_GET (Syscall 158)
    int bytes_received = 0;
    asm volatile("int $0x80"
                 : "=a"(bytes_received)
                 : "a"(158), "b"(handle->url), "c"(buffer), "d"(buffer_size));

    if (bytes_received > 0) {
        printf("NET: Received %d bytes from web.\n", bytes_received);
        if (handle->write_cb) {
            handle->write_cb((char *)buffer, 1, bytes_received, handle->write_data);
        }
    } else {
        printf("NET: ERROR - Failed to fetch URL (code %d)\n", bytes_received);
        free(buffer);
        return CURLE_FAILED_INIT;
    }

    free(buffer);
    return CURLE_OK;
}

void curl_easy_cleanup(CURL *curl) {
    if (!curl) return;
    struct CURL_handle *handle = (struct CURL_handle *)curl;
    free(handle->url);
    free(handle);
}
