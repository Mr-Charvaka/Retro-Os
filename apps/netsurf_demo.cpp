#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "netsurf.h"

/* Tree handler for Hubbub to build our DOM */
static void my_create_element(void *ctx, lwc_string *tag, void **node) {
    if (!ctx || !tag || !node) return;
    dom_document *doc = (dom_document *)ctx;
    dom_document_create_element(doc, tag, (dom_node **)node);
    const char *data = lwc_string_data(tag);
    printf("  [HUBBUB] Created element: %s\n", data ? data : "(null)");
}

static void my_append_child(void *ctx, void *parent, void *child) {
    if (!ctx || !child) return;
    dom_document *doc = (dom_document *)ctx;
    dom_node *p = (dom_node *)parent;
    if (!p) {
        dom_document_get_root(doc, &p);
    }
    if (p) {
        dom_node_append_child(p, (dom_node *)child);
    }
}

static void my_insert_text(void *ctx, void *parent, const char *data, size_t len) {
    if (!parent || !data) return;
    printf("  [HUBBUB] Text: %.*s\n", (int)len, data);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("--- Retro-OS NetSurf Initializer ---\n");

    /* 1. Initialize Foundation (Wapcaplet) */
    lwc_string *url_str = NULL;
    lwc_intern_string("https://google.com", 18, &url_str);
    if (url_str) {
        printf("Interned URL: %s\n", lwc_string_data(url_str));
    } else {
        printf("ERROR: Failed to intern URL string.\n");
    }

    /* 2. Setup Networking (LibCURL) */
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("ERROR: LibCURL initialization failed.\n");
        return 1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, "https://retro-os.local");

    /* 3. Setup Parser (Hubbub) */
    hubbub_parser *parser = NULL;
    if (hubbub_parser_create("UTF-8", true, &parser) != HUBBUB_OK) {
        printf("ERROR: Hubbub parser creation failed.\n");
        return 1;
    }

    dom_document *doc = NULL;
    if (dom_document_create(&doc) != DOM_OK || !doc) {
        printf("ERROR: DOM document creation failed.\n");
        return 1;
    }

    hubbub_tree_handler handler;
    memset(&handler, 0, sizeof(handler));
    handler.create_element = my_create_element;
    handler.append_child = my_append_child;
    handler.insert_text = my_insert_text;
    
    hubbub_parser_set_tree_handler(parser, &handler, doc);

    /* 4. Perform Download and Parse */
    printf("Fetching page via LibCURL...\n");
    curl_easy_perform(curl);
    
    /* In a real scenario, curl callback would feed the parser: */
    const char *html_chunk = "<html><body><h1>Hello from NetSurf!</h1></body></html>";
    printf("Parsing HTML via Hubbub...\n");
    hubbub_parser_parse_chunk(parser, (const uint8_t *)html_chunk, strlen(html_chunk));

    /* 5. Cleanup */
    hubbub_parser_destroy(parser);
    curl_easy_cleanup(curl);
    printf("NetSurf shutdown successfully.\n");

    return 0;
}
