#ifndef LIBNSFB_H
#define LIBNSFB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* NSFB color type (ARGB8888) */
typedef uint32_t nsfb_colour_t;

/* NSFB surface structure */
typedef struct nsfb_t nsfb_t;

/* NSFB backend types */
typedef enum {
    NSFB_SURFACE_RAM,
    NSFB_SURFACE_BGA_RETRO_OS /* Custom Retro-OS Backend */
} nsfb_type_t;

nsfb_t *nsfb_init(nsfb_type_t type);
bool nsfb_set_geometry(nsfb_t *nsfb, int width, int height, int bpp);
bool nsfb_get_geometry(nsfb_t *nsfb, int *width, int *height, int *bpp);

/* Drawing primitives (Used by NetSurf's renderer) */
bool nsfb_plot_clg(nsfb_t *nsfb, nsfb_colour_t color);
bool nsfb_plot_copy(nsfb_t *nsfb, int x, int y, int width, int height, nsfb_colour_t *src, int src_width);
bool nsfb_plot_rectangle(nsfb_t *nsfb, int x, int y, int width, int height, nsfb_colour_t color);

/* Memory access (for direct manipulation) */
uint8_t *nsfb_get_buffer(nsfb_t *nsfb, int *linelen);

#endif /* LIBNSFB_H */
