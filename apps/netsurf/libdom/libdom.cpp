#include "libdom.h"
#include "../libwapcaplet/wapcaplet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct dom_node {
    dom_node_type type;
    lwc_string *tag;
    char *text;
    size_t text_len;
    struct dom_node *parent;
    struct dom_node *first_child;
    struct dom_node *last_child;
    struct dom_node *next_sibling;
    struct dom_node *prev_sibling;
};

struct dom_document {
    dom_node root;
};

dom_error dom_document_create(dom_document **doc) {
    if (!doc) return DOM_BADPARM;
    dom_document *new_doc = (dom_document *)malloc(sizeof(dom_document));
    if (!new_doc) return DOM_NOMEM;
    memset(new_doc, 0, sizeof(dom_document));
    new_doc->root.type = DOM_DOCUMENT_NODE;
    *doc = new_doc;
    return DOM_OK;
}

dom_error dom_document_create_element(dom_document *doc, lwc_string *tag, dom_node **node) {
    if (!doc || !tag || !node) return DOM_BADPARM;
    dom_node *new_node = (dom_node *)malloc(sizeof(dom_node));
    if (!new_node) return DOM_NOMEM;
    memset(new_node, 0, sizeof(dom_node));
    new_node->type = DOM_ELEMENT_NODE;
    new_node->tag = tag;
    lwc_string_ref(tag);
    *node = new_node;
    return DOM_OK;
}

dom_error dom_document_create_text_node(dom_document *doc, const char *data, size_t len, dom_node **node) {
    if (!doc || !node) return DOM_BADPARM;
    dom_node *new_node = (dom_node *)malloc(sizeof(dom_node));
    if (!new_node) return DOM_NOMEM;
    memset(new_node, 0, sizeof(dom_node));
    new_node->type = DOM_TEXT_NODE;
    new_node->text = (char *)malloc(len + 1);
    memcpy(new_node->text, data, len);
    new_node->text[len] = '\0';
    new_node->text_len = len;
    *node = new_node;
    return DOM_OK;
}

dom_error dom_node_append_child(dom_node *parent, dom_node *child) {
    if (!parent || !child) return DOM_BADPARM;
    
    /* Offset 16: parent */
    child->parent = parent;
    
    /* Offset 24: last_child, Offset 20: first_child */
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    } else {
        parent->first_child = parent->last_child = child;
    }
    child->next_sibling = NULL;
    return DOM_OK;
}

dom_error dom_document_get_root(dom_document *doc, dom_node **root) {
    if (!doc || !root) {
        printf("DEBUG: dom_document_get_root FAILED: doc=%p, root=%p\n", doc, root);
        return DOM_BADPARM;
    }
    *root = &doc->root;
    printf("DEBUG: dom_document_get_root SUCCESS: node=%p\n", *root);
    return DOM_OK;
}
 
dom_error dom_node_get_node_type(dom_node *node, dom_node_type *type) {
    if (!node || !type) return DOM_BADPARM;
    *type = node->type;
    return DOM_OK;
}
