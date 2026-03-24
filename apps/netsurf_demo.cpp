#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "netsurf.h"
#include "os/ipc.hpp"
#include "os/syscalls.hpp"

// -------------------------------------------------------------------------
// 1. DATA STRUCTURES
// -------------------------------------------------------------------------

struct BrowserCtx {
    dom_document *doc;
    OS::IPCClient *gui;
    int current_y;
    char url_input[256];
    int url_len;
};

// -------------------------------------------------------------------------
// 2. FONT & RENDERING ENGINE
// -------------------------------------------------------------------------

static uint8_t font8x8[128][8];

void init_font8x8() {
    memset(font8x8, 0, sizeof(font8x8));
    auto set_char = [](int c, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
        font8x8[(uint8_t)c][0] = d0; font8x8[(uint8_t)c][1] = d1; font8x8[(uint8_t)c][2] = d2; font8x8[(uint8_t)c][3] = d3;
        font8x8[(uint8_t)c][4] = d4; font8x8[(uint8_t)c][5] = d5; font8x8[(uint8_t)c][6] = d6; font8x8[(uint8_t)c][7] = d7;
    };
    
    // Default faint dot
    for(int i=0; i<128; i++) set_char(i, 0,0,0,0, 0x18, 0,0,0);
    
    set_char(32, 0,0,0,0,0,0,0,0); // Space
    set_char(46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60, 0x00); // .
    set_char(58, 0x00, 0x60, 0x60, 0x00, 0x00, 0x60, 0x60, 0x00); // :
    set_char(104, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x00); // h
    set_char(116, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x14, 0x18, 0x00); // t
    set_char(112, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60); // p
    set_char(115, 0x00, 0x3C, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00); // s
    set_char(110, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00); // n
    set_char(101, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x66, 0x3C, 0x00); // e
    set_char(99, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x66, 0x3C, 0x00);  // c
    set_char(103, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C); // g
    set_char(111, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00); // o
    set_char(108, 0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00); // l
    set_char(105, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00); // i
    set_char(114, 0x00, 0x5C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x00); // r
    set_char(119, 0x00, 0x66, 0x66, 0x66, 0x7E, 0x7E, 0x66, 0x00); // w
    set_char(100, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00); // d
    set_char(109, 0x00, 0x00, 0xDE, 0x92, 0x92, 0x92, 0x92, 0x00); // m
    set_char(67, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x66, 0x66, 0x3C); // C
    set_char(76, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E); // L
    set_char(87, 0x00, 0x66, 0x66, 0x66, 0x7E, 0x7E, 0x66, 0x66); // W
    set_char(84, 0x00, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18); // T
    set_char(68, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C); // D
}

void draw_char(OS::IPCClient *gui, int x, int y, char c, uint32_t color) {
    if ((uint8_t)c >= 128) return;
    const uint8_t *glyph = font8x8[(uint8_t)c];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (glyph[i] & (1 << (7 - j))) {
                gui->put_pixel(x + j, y + i, color);
            }
        }
    }
}

void draw_string(OS::IPCClient *gui, int x, int y, const char *str, uint32_t color) {
    while (*str) {
        draw_char(gui, x, y, *str++, color);
        x += 8;
    }
}

void draw_chrome(OS::IPCClient *gui) {
    gui->fill_rect(0, 0, 640, 40, 0xFFE0E0E0);
    gui->fill_rect(80, 8, 500, 24, 0xFFFFFFFF);
    draw_string(gui, 10, 15, "Back", 0xFF000000);
    gui->fill_rect(0, 39, 640, 1, 0xFFA0A0A0);
}

// -------------------------------------------------------------------------
// 3. NETWORK & PARSING INTERFACE
// -------------------------------------------------------------------------

static size_t my_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    hubbub_parser *parser = (hubbub_parser *)userdata;
    hubbub_parser_parse_chunk(parser, (const uint8_t *)ptr, size * nmemb);
    return size * nmemb;
}

void Navigate(BrowserCtx *bctx, hubbub_parser *parser, const char *url) {
    if (bctx->gui) {
        bctx->gui->fill_rect(0, 42, 640, 438, 0xFFFFFFFF);
        bctx->current_y = 60;
        bctx->gui->flush();
    }
    printf("NAV: Requesting %s\n", url);
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (void*)my_curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, parser);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}

// -------------------------------------------------------------------------
// 4. HUBBUB CALLBACKS
// -------------------------------------------------------------------------

static void my_create_element(void *ctx, lwc_string *tag, void **node) {
    if (!ctx || !tag || !node) return;
    BrowserCtx *bctx = (BrowserCtx *)ctx;
    dom_document_create_element(bctx->doc, tag, (dom_node **)node);
}

static void my_append_child(void *ctx, void *parent, void *child) {
    if (!ctx || !child) return;
    BrowserCtx *bctx = (BrowserCtx *)ctx;
    dom_node *p = (dom_node *)parent;
    if (!p) dom_document_get_root(bctx->doc, &p);
    if (p) dom_node_append_child(p, (dom_node *)child);
}

static void my_insert_text(void *ctx, void *parent, const char *data, size_t len) {
    if (!ctx || !data || len == 0) return;
    BrowserCtx *bctx = (BrowserCtx *)ctx;
    if (bctx->gui) {
        char buf[512];
        if (len > 511) len = 511;
        memcpy(buf, data, len);
        buf[len] = '\0';
        draw_string(bctx->gui, 20, bctx->current_y, buf, 0xFF000000);
        bctx->current_y += 20;
        bctx->gui->flush();
    }
}

// -------------------------------------------------------------------------
// 5. MAIN ENTRY POINT
// -------------------------------------------------------------------------

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    init_font8x8();
    
    OS::IPCClient gui;
    bool has_gui = false;
    if (gui.connect()) {
        if (gui.create_window("NetSurf Live", 640, 480)) {
            has_gui = true;
            gui.fill_rect(0, 0, 640, 480, 0xFFFFFFFF);
            draw_chrome(&gui);
            gui.flush();
        }
    }

    dom_document *doc = NULL;
    dom_document_create(&doc);

    BrowserCtx bctx;
    bctx.doc = doc;
    bctx.gui = has_gui ? &gui : NULL;
    bctx.current_y = 60;
    strcpy(bctx.url_input, "http://neverssl.com");
    bctx.url_len = strlen(bctx.url_input);

    hubbub_parser *parser = NULL;
    hubbub_parser_create("UTF-8", true, &parser);
    hubbub_tree_handler handler;
    memset(&handler, 0, sizeof(handler));
    handler.create_element = my_create_element;
    handler.append_child = my_append_child;
    handler.insert_text = my_insert_text;
    hubbub_parser_set_tree_handler(parser, &handler, &bctx);

    // Initial Load setup
    bool first_load = true;

    if (has_gui) {
        printf("Browser READY. Starting event loop.\n");
        while (true) {
            if (first_load) {
                // Draw a status message so user knows we are working
                draw_string(&gui, 20, 60, "Connecting to the world wide web...", 0xFF0000FF);
                gui.flush();
                OS::Syscall::sleep(50); // Give GUI 500ms to breathe
                
                Navigate(&bctx, parser, bctx.url_input);
                first_load = false;
            }

            OS::gfx_msg_t msg;
            while (gui.recv_msg(&msg)) {
                if (msg.type == OS::MSG_GFX_KEY_EVENT) {
                    char c = msg.data.key.key;
                    if (c == 0x0D) { // ENTER
                        bctx.url_input[bctx.url_len] = '\0';
                        Navigate(&bctx, parser, bctx.url_input);
                    } else if (c == 0x08) { // BACKSPACE
                        if (bctx.url_len > 0) bctx.url_len--;
                    } else if (bctx.url_len < 254 && c >= 32) {
                        bctx.url_input[bctx.url_len++] = c;
                    }
                    draw_chrome(&gui);
                    gui.fill_rect(82, 10, 496, 20, 0xFFFFFFFF);
                    bctx.url_input[bctx.url_len] = '\0';
                    draw_string(&gui, 85, 15, bctx.url_input, 0xFF000000);
                    gui.flush();
                }
            }
            OS::Syscall::sleep(1);
        }
    }

    return 0;
}
