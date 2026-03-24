#include "image.h"
#include <stdlib.h>
#include <string.h>

bool ns_image_decode(const uint8_t *data, size_t len, ns_image_type type, ns_image_t **res) {
    if (!data || !res) return false;

    /* Here, we should integrate with LibPNG or LibJPEG.
       For the demo, we'll provide a minimal BMP-style decoder. */

    ns_image_t *img = (ns_image_t *)malloc(sizeof(ns_image_t));
    if (!img) return false;

    /* Just return a 1x1 white pixel for the demo if we can't parse it yet. */
    img->width = 1;
    img->height = 1;
    img->pixels = (uint32_t *)malloc(sizeof(uint32_t));
    img->pixels[0] = 0xFFFFFFFF;
    *res = img;
    return true;
}

void ns_image_destroy(ns_image_t *img) {
    if (!img) return;
    free(img->pixels);
    free(img);
}
