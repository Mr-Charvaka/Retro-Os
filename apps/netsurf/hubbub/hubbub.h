#ifndef HUBBUB_H
#define HUBBUB_H

#include <stddef.h>
#include <stdint.h>
#include "../libparserutils/parserutils.h"
#include "../libwapcaplet/wapcaplet.h"

/* Hubbub error codes */
typedef enum {
    HUBBUB_OK = 0,
    HUBBUB_NOMEM = 1,
    HUBBUB_BADPARM = 2,
    HUBBUB_INVALID = 3,
    HUBBUB_FILENOTFOUND = 4,
    HUBBUB_NEEDDATA = 5,
    HUBBUB_EOF = 6
} hubbub_error;

/* Hubbub token types */
typedef enum {
    HUBBUB_TOKEN_DOCTYPE,
    HUBBUB_TOKEN_START_TAG,
    HUBBUB_TOKEN_END_TAG,
    HUBBUB_TOKEN_COMMENT,
    HUBBUB_TOKEN_CHARACTER,
    HUBBUB_TOKEN_EOF
} hubbub_token_type;

/* Hubbub parser type */
typedef struct hubbub_parser hubbub_parser;

/* Hubbub tree handler (The callback structure for building DOM) */
typedef struct {
    void (*create_element)(void *ctx, lwc_string *tag, void **node);
    void (*append_child)(void *ctx, void *parent, void *child);
    void (*insert_text)(void *ctx, void *parent, const char *data, size_t len);
} hubbub_tree_handler;

hubbub_error hubbub_parser_create(const char *enc, bool fix_enc, hubbub_parser **parser);
hubbub_error hubbub_parser_parse_chunk(hubbub_parser *parser, const uint8_t *data, size_t len);
hubbub_error hubbub_parser_completed(hubbub_parser *parser);
hubbub_error hubbub_parser_destroy(hubbub_parser *parser);

hubbub_error hubbub_parser_set_tree_handler(hubbub_parser *parser, hubbub_tree_handler *handler, void *ctx);

#endif /* HUBBUB_H */
