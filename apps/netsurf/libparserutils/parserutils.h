#ifndef LIBPARSERUTILS_H
#define LIBPARSERUTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Basic return codes */
typedef enum {
    PARSERUTILS_OK = 0,
    PARSERUTILS_NOMEM = 1,
    PARSERUTILS_BADPARM = 2,
    PARSERUTILS_INVALID = 3,
    PARSERUTILS_FILENOTFOUND = 4,
    PARSERUTILS_NEEDDATA = 5,
    PARSERUTILS_EOF = 6,
    PARSERUTILS_ITERATOR_END = 7
} parserutils_error;

/* Resizable buffer (Used by Hubbub for collecting token strings) */
typedef struct parserutils_buffer {
    uint8_t *data;
    size_t len;
    size_t alloc;
} parserutils_buffer;

parserutils_error parserutils_buffer_create(size_t initial_alloc, parserutils_buffer **buf);
parserutils_error parserutils_buffer_append(parserutils_buffer *buf, const uint8_t *data, size_t len);
parserutils_error parserutils_buffer_destroy(parserutils_buffer *buf);

/* UTF-8 Character Handling */
typedef uint32_t parserutils_ucs4;

parserutils_error parserutils_charset_utf8_to_ucs4(const uint8_t *s, size_t len, parserutils_ucs4 *res, size_t *consumed);
parserutils_error parserutils_charset_ucs4_to_utf8(parserutils_ucs4 ucs4, uint8_t *res, size_t *len);

#endif /* LIBPARSERUTILS_H */
