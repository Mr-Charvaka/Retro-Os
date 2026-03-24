#include "libnsfb.h"
#include <stdlib.h>
#include <string.h>

struct nsfb_t {
    nsfb_type_t type;
    int width, height, bpp;
    uint32_t *buffer;
    int linelen;
};

nsfb_t *nsfb_init(nsfb_type_t type) {
    nsfb_t *new_nsfb = (nsfb_t *)malloc(sizeof(nsfb_t));
    if (!new_nsfb) return NULL;
    memset(new_nsfb, 0, sizeof(nsfb_t));
    new_nsfb->type = type;
    return new_nsfb;
}

bool nsfb_set_geometry(nsfb_t *nsfb, int width, int height, int bpp) {
    if (!nsfb) return false;
    nsfb->width = width;
    nsfb->height = height;
    nsfb->bpp = bpp;
    nsfb->linelen = width * (bpp / 8);

    /* For RAM surface, allocate. For BGA, the buffer is assigned from window memory */
    if (nsfb->type == NSFB_SURFACE_RAM) {
        nsfb->buffer = (uint32_t *)malloc(nsfb->linelen * height);
        if (!nsfb->buffer) return false;
    }
    return true;
}

/* Primitives for high-performance rendering */

bool nsfb_plot_clg(nsfb_t *nsfb, nsfb_colour_t color) {
    if (!nsfb || !nsfb->buffer) return false;
    size_t total_pixels = nsfb->width * nsfb->height;
    for (size_t i = 0; i < total_pixels; i++) {
        nsfb->buffer[i] = color;
    }
    return true;
}

bool nsfb_plot_rectangle(nsfb_t *nsfb, int x, int y, int width, int height, nsfb_colour_t color) {
    if (!nsfb || !nsfb->buffer) return false;
    for (int j = y; j < y + height && j < nsfb->height; j++) {
        for (int i = x; i < x + width && i < nsfb->width; i++) {
            nsfb->buffer[j * nsfb->width + i] = color;
        }
    }
    return true;
}

uint8_t *nsfb_get_buffer(nsfb_t *nsfb, int *linelen) {
    if (!nsfb) return NULL;
    if (linelen) *linelen = nsfb->linelen;
    return (uint8_t *)nsfb->buffer;
}
