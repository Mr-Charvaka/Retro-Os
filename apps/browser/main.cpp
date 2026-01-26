#include "../include/os/ipc.hpp"
#include "../include/os/syscalls.hpp"
#include "BrowserContainer.h"
#include "NetworkClient.h"
#include "FontUtils.h"
#include <string.h>

class BrowserApp {
    OS::IPCClient* ipc;
    BrowserContainer* container;
    litehtml::context html_context;
    std::shared_ptr<litehtml::document> doc;
    int width, height;
    char url_buffer[256];
    bool needs_redraw;

public:
    BrowserApp() : ipc(nullptr), container(nullptr), width(800), height(600), needs_redraw(true) {
        url_buffer[0] = 0;
    }

    ~BrowserApp() {
        if (container) delete container;
        if (ipc) delete ipc;
    }

    bool init() {
        ipc = new OS::IPCClient();
        if (!ipc->connect()) return false;
        if (!ipc->create_window("Retro Web Browser", width, height)) return false;
        
        container = new BrowserContainer(ipc, width, height);
        html_context.load_master_stylesheet("body { display: block; margin: 8px; font-family: Arial; font-size: 16px; } div { display: block; } a { color: blue; text-decoration: underline; }");
        
        load_html("<html><body><h1>Welcome to Retro OS Browser</h1><p>Rendering with litehtml.</p></body></html>");
        
        return true;
    }

    void load_html(const char* html) {
        doc = litehtml::document::createFromString(html, container, &html_context);
        doc->render(width);
        needs_redraw = true;
    }

    void run() {
        while (true) {
            if (needs_redraw) {
                // Clear white
                ipc->fill_rect(0, 0, width, height, 0xFFFFFFFF);
                
                // Draw HTML
                if (doc) {
                   litehtml::position clip(0, 0, width, height);
                   doc->draw((litehtml::uint_ptr)0, 0, 0, &clip);
                }
                
                ipc->flush();
                needs_redraw = false;
            }

            OS::gfx_msg_t msg;
            if (ipc->recv_msg(&msg)) {
                if (msg.type == OS::MSG_GFX_MOUSE_EVENT) {
                   // Pass mouse events to litehtml if needed (doc->on_mouse_over etc)
                   // functionality skipped for brevity in this step
                } else if (msg.type == OS::MSG_GFX_KEY_EVENT) {
                   if (msg.data.key.key == 'q') break;
                }
            }
        }
    }
};

extern "C" void _start() {
    // Initialize statics if needed
    FontUtils::init();

    BrowserApp app;
    if (app.init()) {
        app.run();
    }
    OS::Syscall::exit(0);
}
