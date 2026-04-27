#ifndef NETSURF_IMAGE_H
#define NETSURF_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* NetSurf Image decoding types */
typedef struct {
    uint32_t width, height;
    uint32_t *pixels; /* ARGB8888 */
} ns_image_t;

typedef enum {
    NS_IMAGE_PNG,
    NS_IMAGE_JPEG,
    NS_IMAGE_GIF,
    NS_IMAGE_BMP
} ns_image_type;

bool ns_image_decode(const uint8_t *data, size_t len, ns_image_type type, ns_image_t **res);
void ns_image_destroy(ns_image_t *img);

#endif /* NETSURF_IMAGE_H */
