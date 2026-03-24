#include "parserutils.h"
#include <stdlib.h>
#include <string.h>

/* Resizable Buffer Implementation */

parserutils_error parserutils_buffer_create(size_t initial_alloc, parserutils_buffer **buf) {
    if (!buf) return PARSERUTILS_BADPARM;
    parserutils_buffer *new_buf = (parserutils_buffer *)malloc(sizeof(parserutils_buffer));
    if (!new_buf) return PARSERUTILS_NOMEM;

    new_buf->data = (uint8_t *)malloc(initial_alloc);
    new_buf->len = 0;
    new_buf->alloc = initial_alloc;
    *buf = new_buf;
    return PARSERUTILS_OK;
}

parserutils_error parserutils_buffer_append(parserutils_buffer *buf, const uint8_t *data, size_t len) {
    if (!buf || !data) return PARSERUTILS_BADPARM;
    if (buf->len + len > buf->alloc) {
        size_t new_alloc = buf->alloc * 2 + len;
        uint8_t *new_data = (uint8_t *)realloc(buf->data, new_alloc);
        if (!new_data) return PARSERUTILS_NOMEM;
        buf->data = new_data;
        buf->alloc = new_alloc;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return PARSERUTILS_OK;
}

parserutils_error parserutils_buffer_destroy(parserutils_buffer *buf) {
    if (!buf) return PARSERUTILS_BADPARM;
    free(buf->data);
    free(buf);
    return PARSERUTILS_OK;
}

/* Character Handling - Basic UTF-8 */

parserutils_error parserutils_charset_utf8_to_ucs4(const uint8_t *s, size_t len, parserutils_ucs4 *res, size_t *consumed) {
    if (!s || len == 0 || !res || !consumed) return PARSERUTILS_BADPARM;
    uint8_t c = s[0];
    if (c < 0x80) {
        *res = c;
        *consumed = 1;
    } else if ((c & 0xe0) == 0xc0 && len >= 2) {
        *res = ((c & 0x1f) << 6) | (s[1] & 0x3f);
        *consumed = 2;
    } else if ((c & 0xf0) == 0xe0 && len >= 3) {
        *res = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        *consumed = 3;
    } else if ((c & 0xf8) == 0xf0 && len >= 4) {
        *res = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
        *consumed = 4;
    } else {
        return PARSERUTILS_INVALID;
    }
    return PARSERUTILS_OK;
}

parserutils_error parserutils_charset_ucs4_to_utf8(parserutils_ucs4 ucs4, uint8_t *res, size_t *len) {
    if (!res || !len) return PARSERUTILS_BADPARM;
    if (ucs4 < 0x80) {
        res[0] = (uint8_t)ucs4;
        *len = 1;
    } else if (ucs4 < 0x800) {
        res[0] = (uint8_t)(0xc0 | (ucs4 >> 6));
        res[1] = (uint8_t)(0x80 | (ucs4 & 0x3f));
        *len = 2;
    } else if (ucs4 < 0x10000) {
        res[0] = (uint8_t)(0xe0 | (ucs4 >> 12));
        res[1] = (uint8_t)(0x80 | ((ucs4 >> 6) & 0x3f));
        res[2] = (uint8_t)(0x80 | (ucs4 & 0x3f));
        *len = 3;
    } else if (ucs4 < 0x200000) {
        res[0] = (uint8_t)(0xf0 | (ucs4 >> 18));
        res[1] = (uint8_t)(0x80 | ((ucs4 >> 12) & 0x3f));
        res[2] = (uint8_t)(0x80 | ((ucs4 >> 6) & 0x3f));
        res[3] = (uint8_t)(0x80 | (ucs4 & 0x3f));
        *len = 4;
    } else {
        return PARSERUTILS_INVALID;
    }
    return PARSERUTILS_OK;
}
