#ifndef LIBCSS_H
#define LIBCSS_H

#include <stddef.h>
#include <stdint.h>
#include "../libwapcaplet/wapcaplet.h"

/* CSS Error codes */
typedef enum {
    CSS_OK = 0,
    CSS_NOMEM = 1,
    CSS_BADPARM = 2,
    CSS_INVALID = 3,
    CSS_FILENOTFOUND = 4
} css_error;

/* CSS Property types */
typedef enum {
    CSS_PROP_COLOR,
    CSS_PROP_BACKGROUND_COLOR,
    CSS_PROP_WIDTH,
    CSS_PROP_HEIGHT,
    CSS_PROP_FONT_SIZE,
    CSS_PROP_DISPLAY,
    CSS_PROP_POSITION,
    CSS_PROP_MARGIN,
    CSS_PROP_PADDING,
    CSS_PROP_BORDER
} css_prop_type;

/* CSS StyleSheet type */
typedef struct css_stylesheet css_stylesheet;

/* CSS Selector type */
typedef struct css_selector css_selector;

/* CSS Select result structure */
typedef struct {
    uint32_t color;
    uint32_t bg_color;
    float width, height;
} css_select_results;

css_error css_stylesheet_create(const char *url, const char *title, css_stylesheet **sheet);
css_error css_stylesheet_append_data(css_stylesheet *sheet, const uint8_t *data, size_t len);
css_error css_stylesheet_completed(css_stylesheet *sheet);
css_error css_stylesheet_destroy(css_stylesheet *sheet);

css_error css_select_style(void *node, css_stylesheet *sheet, css_select_results *results);

#endif /* LIBCSS_H */
