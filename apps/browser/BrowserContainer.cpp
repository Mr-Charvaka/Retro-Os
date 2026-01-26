#include "BrowserContainer.h"
#include "FontUtils.h"
#include <iostream>

BrowserContainer::BrowserContainer(OS::IPCClient* ipc_client, int w, int h) 
    : ipc(ipc_client), client_width(w), client_height(h) {}

BrowserContainer::~BrowserContainer() {}

litehtml::uint_ptr BrowserContainer::create_font(const litehtml::tchar_t* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) {
    if(fm) {
        fm->ascent = size;
        fm->descent = size / 4; 
        fm->height = size + fm->descent;
        fm->x_height = size / 2;
        fm->draw_spaces = true;
    }
    return (litehtml::uint_ptr)size; // Using size as handle
}

void BrowserContainer::delete_font(litehtml::uint_ptr hFont) {}

int BrowserContainer::text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont) {
    return FontUtils::get_text_width(text, (int)hFont);
}

void BrowserContainer::draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) {
    uint32_t c = (0xFF << 24) | (color.red << 16) | (color.green << 8) | color.blue;
    FontUtils::draw_text(ipc, pos.x, pos.y, text, c, (int)hFont);
}

int BrowserContainer::pt_to_px(int pt) const { return pt; } // 1:1

int BrowserContainer::get_default_font_size() const { return 16; }

const litehtml::tchar_t* BrowserContainer::get_default_font_name() const { return "Arial"; }

void BrowserContainer::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) {
    // Simplified marker
    uint32_t c = (0xFF << 24) | (marker.color.red << 16) | (marker.color.green << 8) | marker.color.blue;
    ipc->fill_rect(marker.pos.x, marker.pos.y, marker.pos.width, marker.pos.height, c);
}

void BrowserContainer::load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, bool redraw_on_ready) {}

void BrowserContainer::get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, litehtml::size& sz) {
    sz.width = 0; sz.height = 0;
}

void BrowserContainer::draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg) {
    uint32_t c = (0xFF << 24) | (bg.color.red << 16) | (bg.color.green << 8) | bg.color.blue;
    ipc->fill_rect(bg.border_box.x, bg.border_box.y, bg.border_box.width, bg.border_box.height, c);
}

void BrowserContainer::draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) {
    // Simplified borders
    if(borders.left.width > 0) 
        ipc->fill_rect(draw_pos.x, draw_pos.y, borders.left.width, draw_pos.height, 0xFF000000);
    // ... complete others if needed
}

void BrowserContainer::set_caption(const litehtml::tchar_t* caption) {}

void BrowserContainer::set_base_url(const litehtml::tchar_t* base_url) {}

void BrowserContainer::link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) {}

void BrowserContainer::on_anchor_click(const litehtml::tchar_t* url, const litehtml::element::ptr& el) {}

void BrowserContainer::set_cursor(const litehtml::tchar_t* cursor) {}

void BrowserContainer::transform_text(litehtml::tstring& text, litehtml::text_transform tt) {}

void BrowserContainer::import_css(litehtml::tstring& text, const litehtml::tstring& url, litehtml::tstring& baseurl) {}

void BrowserContainer::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius, bool valid_x, bool valid_y) {}

void BrowserContainer::del_clip() {}

void BrowserContainer::get_client_rect(litehtml::position& client) const {
    client.x = 0;
    client.y = 0;
    client.width = client_width;
    client.height = client_height;
}

std::shared_ptr<litehtml::element> BrowserContainer::create_element(const litehtml::tchar_t* tag_name, const litehtml::string_map& attributes, const std::shared_ptr<litehtml::document>& doc) {
    return 0;
}

void BrowserContainer::get_media_features(litehtml::media_features& media) const {
    media.type = litehtml::media_type_screen;
    media.width = client_width;
    media.height = client_height;
}

void BrowserContainer::get_language(litehtml::tstring& language, litehtml::tstring& culture) const {
    language = "en";
    culture = "";
}
