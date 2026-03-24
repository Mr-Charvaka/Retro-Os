#include "hubbub.h"
#include "../libwapcaplet/wapcaplet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct hubbub_parser {
    hubbub_tree_handler handler;
    void *ctx;
    void *current_node;
    void *document_node;
    bool in_body;
};

hubbub_error hubbub_parser_create(const char *enc, bool fix_enc, hubbub_parser **parser) {
    if (!parser) return HUBBUB_BADPARM;
    hubbub_parser *new_p = (hubbub_parser *)malloc(sizeof(hubbub_parser));
    if (!new_p) return HUBBUB_NOMEM;
    
    /* Zero out EVERYTHING to prevent junk data at offset 20 */
    memset(new_p, 0, sizeof(hubbub_parser));
    new_p->current_node = NULL;
    new_p->document_node = NULL;
    new_p->in_body = false;
    
    *parser = new_p;
    return HUBBUB_OK;
}

hubbub_error hubbub_parser_set_tree_handler(hubbub_parser *parser, hubbub_tree_handler *handler, void *ctx) {
    if (!parser || !handler) return HUBBUB_BADPARM;
    
    /* Copy handler function pointers safely */
    memcpy(&parser->handler, handler, sizeof(hubbub_tree_handler));
    parser->ctx = ctx;
    parser->document_node = ctx; /* Offset 20 is now initialized! */
    return HUBBUB_OK;
}

/* Minimal state-machine parser loop for demo/initial purposes */
hubbub_error hubbub_parser_parse_chunk(hubbub_parser *parser, const uint8_t *data, size_t len) {
    printf("TRACE: hubbub_parser_parse_chunk(parser=%p, len=%d)\n", parser, (int)len);
    if (!parser || !data) return HUBBUB_BADPARM;

    const char *p = (const char *)data;
    const char *end = p + len;

    while (p < end) {
        if (*p == '<') {
            printf("TRACE: Found tag start at offset %d\n", (int)(p - (const char *)data));
            p++;
            if (*p == '/') {
                /* End Tag */
                p++;
                const char *tag_start = p;
                while (p < end && *p != '>') p++;
                /* Handlers would be called here to pop from stack */
            } else {
                /* Start Tag */
                const char *tag_start = p;
                while (p < end && *p != '>' && *p != ' ') p++;
                size_t tag_len = p - tag_start;

                lwc_string *tag = NULL;
                lwc_intern_string(tag_start, tag_len, &tag);
                printf("DEBUG: Hubbub interned tag '%.*s' (lwc_string=%p)\n", (int)tag_len, tag_start, (void*)tag);

                void *node = NULL;
                if (tag && parser->handler.create_element) {
                    parser->handler.create_element(parser->ctx, tag, &node);
                }

                if (node && parser->handler.append_child) {
                    void *parent = parser->current_node;
                    parser->handler.append_child(parser->ctx, parent, node);
                    parser->current_node = node;
                }

                /* Skip attributes until closing '>' */
                while (p < end && *p != '>') p++;
            }
        } else {
            /* Text content */
            const char *text_start = p;
            while (p < end && *p != '<') p++;
            if (parser->handler.insert_text) {
                parser->handler.insert_text(parser->ctx, parser->current_node, text_start, p - text_start);
            }
        }
        if (p < end && *p == '>') p++;
    }

    return HUBBUB_OK;
}

hubbub_error hubbub_parser_completed(hubbub_parser *parser) {
    return HUBBUB_OK;
}

hubbub_error hubbub_parser_destroy(hubbub_parser *parser) {
    if (!parser) return HUBBUB_BADPARM;
    free(parser);
    return HUBBUB_OK;
}
