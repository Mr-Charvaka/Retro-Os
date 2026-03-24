#include "libcss.h"
#include "../libwapcaplet/wapcaplet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct css_stylesheet {
    char *url;
    char *title;
    char *data;
    size_t len;
    size_t alloc;
};

css_error css_stylesheet_create(const char *url, const char *title, css_stylesheet **sheet) {
    if (!sheet) return CSS_BADPARM;
    css_stylesheet *new_s = (css_stylesheet *)malloc(sizeof(css_stylesheet));
    if (!new_s) return CSS_NOMEM;
    memset(new_s, 0, sizeof(css_stylesheet));
    if (url) new_s->url = strdup(url);
    if (title) new_s->title = strdup(title);
    *sheet = new_s;
    return CSS_OK;
}

css_error css_stylesheet_append_data(css_stylesheet *sheet, const uint8_t *data, size_t len) {
    if (!sheet || !data) return CSS_BADPARM;
    if (sheet->len + len > sheet->alloc) {
        size_t new_alloc = sheet->alloc * 2 + len + 1024;
        char *new_data = (char *)realloc(sheet->data, new_alloc);
        if (!new_data) return CSS_NOMEM;
        sheet->data = new_data;
        sheet->alloc = new_alloc;
    }
    memcpy(sheet->data + sheet->len, data, len);
    sheet->len += len;
    sheet->data[sheet->len] = '\0';
    return CSS_OK;
}

css_error css_select_style(void *node, css_stylesheet *sheet, css_select_results *results) {
    if (!sheet || !results) return CSS_BADPARM;
    /* Default results */
    results->color = 0xFFFFFFFF;
    results->bg_color = 0x000000FF;
    results->width = 100.0f;
    results->height = 100.0f;

    /* Very basic inline parsing of properties for the demo */
    if (sheet->data) {
        if (strstr(sheet->data, "color: red")) results->color = 0xFF0000FF;
        if (strstr(sheet->data, "color: blue")) results->color = 0x0000FFFF;
        if (strstr(sheet->data, "background: black")) results->bg_color = 0x000000FF;
    }

    return CSS_OK;
}

css_error css_stylesheet_completed(css_stylesheet *sheet) {
    return CSS_OK;
}

css_error css_stylesheet_destroy(css_stylesheet *sheet) {
    if (!sheet) return CSS_BADPARM;
    free(sheet->url);
    free(sheet->title);
    free(sheet->data);
    free(sheet) ;
    return CSS_OK;
}
