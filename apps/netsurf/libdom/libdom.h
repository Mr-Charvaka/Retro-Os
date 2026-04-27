#ifndef LIBDOM_H
#define LIBDOM_H

#include <stddef.h>
#include <stdint.h>
#include "../libwapcaplet/wapcaplet.h"

/* DOM Error codes */
typedef enum {
    DOM_OK = 0,
    DOM_NOMEM = 1,
    DOM_BADPARM = 2,
    DOM_INVALID = 3,
    DOM_HIERARCHY_REQUEST_ERR = 4
} dom_error;

/* DOM Node types */
typedef enum {
    DOM_ELEMENT_NODE = 1,
    DOM_ATTRIBUTE_NODE = 2,
    DOM_TEXT_NODE = 3,
    DOM_CDATA_SECTION_NODE = 4,
    DOM_COMMENT_NODE = 8,
    DOM_DOCUMENT_NODE = 9
} dom_node_type;

#ifdef __cplusplus
extern "C" {
#endif
 
/* DOM Node type */
struct dom_node;
 
/* DOM Attribute type */
struct dom_attribute;
 
/* DOM Document type */
struct dom_document;

dom_error dom_document_create(dom_document **doc);
dom_error dom_document_create_element(dom_document *doc, lwc_string *tag, dom_node **node);
dom_error dom_document_create_text_node(dom_document *doc, const char *data, size_t len, dom_node **node);
dom_error dom_document_get_root(dom_document *doc, dom_node **root);

dom_error dom_node_append_child(dom_node *parent, dom_node *child);
dom_error dom_node_get_node_type(dom_node *node, dom_node_type *type);
dom_error dom_node_get_tag_name(dom_node *node, lwc_string **tag);

#ifdef __cplusplus
}
#endif
 
#endif /* LIBDOM_H */
