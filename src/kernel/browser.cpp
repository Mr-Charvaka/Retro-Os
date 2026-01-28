#include "../include/browser.h"
#include "../drivers/serial.h"
#include "../include/css_engine.h"
#include "../include/css_parser.h"
#include "../include/html_parser.h"
#include "../include/string.h"
#include "net_advanced.h"

namespace Browser {

// ----------------------------------------------------------------------------
// DATA STRUCTURES
// ----------------------------------------------------------------------------

struct BrowserState {
  char url[256];
  char content[65536]; // 64KB buffer for raw HTML
  int content_len;
  int scroll_y;
  bool loading;
  char status[64];
  struct http_response response;
  bool url_focused;
  int url_cursor;
  CSSStyleSheet *stylesheet;
};

static BrowserState g_browser;

// Tiny DOM / Layout Tree
struct Style {
  uint32_t color;
  uint32_t background_color;
  int font_size;
  bool is_block;
  bool is_bold;
  int margin_top;
  int margin_bottom;
  int packet_id; // For debugging
};

struct LayoutNode {
  char text[256]; // Text content or Tag name
  char tag[16];   // "p", "h1", "div", etc.
  Style style;
  int x, y, w, h;
  bool hidden;
};

#define MAX_NODES 2048
static LayoutNode nodes[MAX_NODES];
static int node_count = 0;
static int content_height = 0;

// ----------------------------------------------------------------------------
// HELPER FUNCTIONS
// ----------------------------------------------------------------------------

static int tolower(int c) {
  if (c >= 'A' && c <= 'Z')
    return c + 32;
  return c;
}

static int strncasecmp_custom(const char *s1, const char *s2, int n) {
  if (n == 0)
    return 0;
  while (n-- != 0 && tolower(*s1) == tolower(*s2)) {
    if (n == 0 || *s1 == '\0' || *s2 == '\0')
      break;
    s1++;
    s2++;
  }
  return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

// ----------------------------------------------------------------------------
// CSS / STYLE ENGINE
// ----------------------------------------------------------------------------

Style get_default_style(const char *tag) {
  Style s;
  s.color = 0xFF000000;            // Special value for INHERIT
  s.background_color = 0x00000000; // Transparent
  s.font_size = 8;
  s.is_block = false;
  s.is_bold = false;
  s.margin_top = 0;
  s.margin_bottom = 0;

  if (strncasecmp_custom(tag, "h1", 2) == 0) {
    s.font_size = 18;
    s.is_bold = true;
    s.is_block = true;
    s.margin_top = 16;
    s.margin_bottom = 12;
    s.color = 0x000000;
  } else if (strncasecmp_custom(tag, "h2", 2) == 0) {
    s.font_size = 15;
    s.is_bold = true;
    s.is_block = true;
    s.margin_top = 12;
    s.margin_bottom = 8;
    s.color = 0x111111;
  } else if (strncasecmp_custom(tag, "p", 1) == 0) {
    s.is_block = true;
    s.margin_bottom = 12;
    s.margin_top = 4;
  } else if (strncasecmp_custom(tag, "body", 4) == 0) {
    s.is_block = true;
    s.margin_top = 8;
    s.margin_bottom = 8;
    s.color = 0x333333;
  } else if (strncasecmp_custom(tag, "div", 3) == 0) {
    s.is_block = true;
  } else if (strncasecmp_custom(tag, "li", 2) == 0) {
    s.is_block = true;
    s.margin_bottom = 6;
  } else if (strncasecmp_custom(tag, "a", 1) == 0) {
    s.color = 0x0000EE; // Standard link blue
    s.is_bold = false;
  } else if (strncasecmp_custom(tag, "b", 1) == 0 ||
             strncasecmp_custom(tag, "strong", 6) == 0) {
    s.is_bold = true;
  } else if (strncasecmp_custom(tag, "br", 2) == 0) {
    s.is_block = true;
  }
  return s;
}

// ----------------------------------------------------------------------------
// CSS INTEGRATION
// ----------------------------------------------------------------------------

void collect_styles(Node *n) {
  if (!n)
    return;
  if (n->type == NodeType::ELEMENT_NODE) {
    ElementNode *el = (ElementNode *)n;
    if (strcmp(el->tag_name, "style") == 0) {
      if (el->first_child && el->first_child->type == NodeType::TEXT_NODE) {
        TextNode *tn = (TextNode *)el->first_child;
        CSSParser parser;
        CSSStyleSheet *new_sheet = parser.parse(tn->text);
        if (new_sheet) {
          // Merge rules into current sheet
          if (!g_browser.stylesheet) {
            g_browser.stylesheet = new_sheet;
          } else {
            CSSRule *r = g_browser.stylesheet->rules;
            if (!r) {
              g_browser.stylesheet->rules = new_sheet->rules;
            } else {
              while (r->next)
                r = r->next;
              r->next = new_sheet->rules;
            }
            new_sheet->rules = nullptr; // Avoid deleting moved rules
            delete new_sheet;
          }
        }
      }
    }
  }
  for (Node *child = n->first_child; child; child = child->next_sibling) {
    collect_styles(child);
  }
}

uint32_t parse_css_color(const char *data) {
  if (data[0] == '#') {
    int len = strlen(data);
    if (len == 7) {
      auto hex_to_int = [](char c) -> int {
        if (c >= '0' && c <= '9')
          return (int)(c - '0');
        if (c >= 'a' && c <= 'f')
          return (int)(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
          return (int)(c - 'A' + 10);
        return 0;
      };
      uint32_t r = (hex_to_int(data[1]) << 4) | hex_to_int(data[2]);
      uint32_t g = (hex_to_int(data[3]) << 4) | hex_to_int(data[4]);
      uint32_t b = (hex_to_int(data[5]) << 4) | hex_to_int(data[6]);
      return (r << 16) | (g << 8) | b;
    }
  }
  if (strcmp(data, "red") == 0)
    return 0xFF0000;
  if (strcmp(data, "green") == 0)
    return 0x00FF00;
  if (strcmp(data, "blue") == 0)
    return 0x0000FF;
  if (strcmp(data, "black") == 0)
    return 0x000000;
  if (strcmp(data, "white") == 0)
    return 0xFFFFFF;
  if (strcmp(data, "gray") == 0)
    return 0x808080;
  if (strcmp(data, "transparent") == 0)
    return 0x00000000;
  if (strcmp(data, "yellow") == 0)
    return 0xFFFF00;
  if (strcmp(data, "orange") == 0)
    return 0xFFA500;
  return 0x000000;
}

void apply_css_to_style(Node *n, Style &s) {
  if (!g_browser.stylesheet || !n)
    return;

  for (CSSRule *r = g_browser.stylesheet->rules; r; r = r->next) {
    if (r->type == CSSRule::STYLE) {
      CSSStyleRule *sr = (CSSStyleRule *)r;
      if (CSSEngine::matches(sr->selectors, n)) {
        for (CSSDeclaration *d = sr->declarations; d; d = d->next) {
          if (strcmp(d->property, "color") == 0) {
            if (d->value->type == CSSValueType::COLOR ||
                d->value->type == CSSValueType::KEYWORD) {
              s.color = parse_css_color(d->value->data);
            }
          } else if (strcmp(d->property, "background-color") == 0) {
            if (d->value->type == CSSValueType::COLOR ||
                d->value->type == CSSValueType::KEYWORD) {
              s.background_color = parse_css_color(d->value->data);
            }
          } else if (strcmp(d->property, "font-size") == 0) {
            if (d->value->type == CSSValueType::DIMENSION) {
              s.font_size = (int)d->value->numeric_value;
            }
          } else if (strcmp(d->property, "display") == 0) {
            if (d->value->type == CSSValueType::KEYWORD) {
              if (strcmp(d->value->data, "block") == 0)
                s.is_block = true;
              else if (strcmp(d->value->data, "inline") == 0)
                s.is_block = false;
              else if (strcmp(d->value->data, "none") == 0) {
                // Should hide the node, but Style doesn't have hidden yet.
              }
            }
          }
        }
      }
    }
  }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ENTITY DECODER
// ----------------------------------------------------------------------------

struct entity_map {
  const char *name;
  const char *val;
};
static const entity_map ENTITIES[] = {
    {"nbsp;", " "},   {"amp;", "&"},     {"lt;", "<"},     {"gt;", ">"},
    {"quot;", "\""},  {"copy;", "(c)"},  {"reg;", "(r)"},  {"raquo;", ">>"},
    {"laquo;", "<<"}, {"bull;", "*"},    {"middot;", "."}, {"ndash;", "-"},
    {"mdash;", "--"}, {nullptr, nullptr}};

void decode_entities(char *text) {
  char *src = text;
  char *dst = text;
  while (*src) {
    if (*src == '&') {
      bool found = false;
      for (int i = 0; ENTITIES[i].name; i++) {
        int len = strlen(ENTITIES[i].name);
        if (strncmp(src + 1, ENTITIES[i].name, len) == 0) {
          strcpy(dst, ENTITIES[i].val);
          dst += strlen(ENTITIES[i].val);
          src += len + 1;
          found = true;
          break;
        }
      }
      if (found)
        continue;
      // Decimal entities &#123;
      if (*(src + 1) == '#') {
        int val = 0;
        char *p = src + 2;
        while (*p >= '0' && *p <= '9') {
          val = val * 10 + (*p - '0');
          p++;
        }
        if (*p == ';') {
          *dst++ = (char)val;
          src = p + 1;
          continue;
        }
      }
    }
    *dst++ = *src++;
  }
  *dst = 0;
}

// HTML PARSER INTEGRATION
// ----------------------------------------------------------------------------

void flatten_tree(Node *n, Style current_style, bool hide_content) {
  if (!n || node_count >= MAX_NODES)
    return;

  bool current_hide = hide_content;
  Style next_style = current_style;

  if (n->type == NodeType::ELEMENT_NODE) {
    ElementNode *el = (ElementNode *)n;
    next_style = get_default_style(el->tag_name);

    if (next_style.color == 0xFF000000) {
      next_style.color = current_style.color;
    }

    apply_css_to_style(n, next_style);

    // Filter non-visible elements
    if (strcmp(el->tag_name, "script") == 0 ||
        strcmp(el->tag_name, "style") == 0 ||
        strcmp(el->tag_name, "title") == 0 ||
        strcmp(el->tag_name, "head") == 0) {
      current_hide = true;
    }

    if (!current_hide && next_style.is_block) {
      LayoutNode *ln = &nodes[node_count++];
      ln->text[0] = 0;
      strcpy(ln->tag, el->tag_name);
      ln->style = next_style;
      ln->hidden = false;
    }
  } else if (n->type == NodeType::TEXT_NODE) {
    if (!current_hide) {
      TextNode *tn = (TextNode *)n;
      LayoutNode *ln = &nodes[node_count++];
      strncpy(ln->text, tn->text, 255);
      ln->text[255] = 0;

      // Decode HTML entities (Google uses a lot of &nbsp; and &raquo;)
      decode_entities(ln->text);

      strcpy(ln->tag, "text");
      ln->style = current_style; // Inherit parent element style
      ln->style.is_block = false;
      ln->hidden = false;
    }
    return; // Leaf node
  }

  // Always recurse into children for container nodes (Document, Element, etc.)
  for (Node *child = n->first_child; child; child = child->next_sibling) {
    flatten_tree(child, next_style, current_hide);
  }
}

void parse_html() {
  node_count = 0;
  const char *p = g_browser.content;

  // Skip Headers
  const char *body_start = strstr(p, "\r\n\r\n");
  if (body_start)
    p = body_start + 4;

  if (g_browser.response.body && g_browser.response.body_length > 0) {
    p = (const char *)g_browser.response.body;
  }

  HTML5Parser parser;
  DocumentNode *doc = parser.parse(p);

  if (doc) {
    if (g_browser.stylesheet) {
      delete g_browser.stylesheet;
      g_browser.stylesheet = nullptr;
    }
    collect_styles(doc);
    flatten_tree(doc, get_default_style("body"), false);
    delete doc;
  }
}

// ----------------------------------------------------------------------------
// LAYOUT ENGINE
// ----------------------------------------------------------------------------

void layout_content(int width) {
  int cx = 10;
  int cy = 0;
  int max_w = width - 20;

  int line_h = 0;

  // Caches from FontSystem
  auto font_reg = FontSystem::font_load("default", 8);
  auto font_bold = FontSystem::font_load("bold", 16);

  for (int i = 0; i < node_count; i++) {
    LayoutNode *n = &nodes[i];
    if (n->hidden)
      continue;

    FontSystem::FontHandle font = n->style.is_bold ? font_bold : font_reg;
    int glyph_h = (font.size > 8) ? 16 : 8; // Simplified height map

    if (n->style.is_block) {
      if (cx > 10) {
        cy += line_h;
        cx = 10;
        line_h = 0;
      }
      cy += n->style.margin_top;
    }

    if (strlen(n->text) > 0) {
      char *word_start = n->text;
      char *c = n->text;

      while (*c) {
        if (*c == ' ' || *(c + 1) == 0) {
          int len = c - word_start + 1;
          if (*(c + 1) == 0 && *c != ' ')
            len++;

          char word[64];
          int copy_len = len < 63 ? len : 63;
          strncpy(word, word_start, copy_len);
          word[copy_len] = 0;
          if (word[copy_len - 1] == ' ')
            word[copy_len - 1] = 0;

          int w_px = FontSystem::text_width(font, word);

          if (cx + w_px > max_w) {
            cy += (line_h > 0 ? line_h : glyph_h);
            cx = 10;
            line_h = 0;
          }
          cx += w_px + 8; // space width approx
        }
        if (*c == ' ')
          word_start = c + 1;
        c++;
      }
      if (line_h < glyph_h)
        line_h = glyph_h;
    }

    if (n->style.is_block) {
      cy += line_h;
      cx = 10;
      line_h = 0;
      cy += n->style.margin_bottom;
    }
  }
  content_height = cy + 50;
}

// ----------------------------------------------------------------------------
// MAIN INTERFACE
// ----------------------------------------------------------------------------

void init() {
  memset(&g_browser, 0, sizeof(BrowserState));
  strcpy(g_browser.url, "http://info.cern.ch");
  strcpy(g_browser.status, "Ready");
  g_browser.content_len = 0;
  g_browser.url_focused = false;
  g_browser.url_cursor = strlen(g_browser.url);
}

static void navigate_internal(const char *url, int depth) {
  if (depth > 5) {
    serial_log("BROWSER: Redirect limit reached");
    return;
  }
  if (g_browser.loading && depth == 0)
    return;
  if (strlen(url) == 0)
    return;

  g_browser.loading = true;
  strcpy(g_browser.status, "Loading...");

  char safe_url[256];
  if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
    strcpy(safe_url, url);
  } else {
    strcpy(safe_url, "http://");
    strcat(safe_url, url);
  }

  // Update display URL
  strcpy(g_browser.url, safe_url);
  g_browser.url_cursor = strlen(g_browser.url);

  serial_log("BROWSER: Navigating to...");
  serial_log(safe_url);

  memset(g_browser.content, 0, sizeof(g_browser.content));
  g_browser.content_len = 0;
  g_browser.scroll_y = 0;

  int ret = http_get(safe_url, (uint8_t *)g_browser.content,
                     sizeof(g_browser.content) - 1, &g_browser.response);

  if (ret > 0) {
    // Redirect Support
    if (g_browser.response.status_code == 301 ||
        g_browser.response.status_code == 302) {
      const char *loc = http_get_header(&g_browser.response, "Location");
      if (loc) {
        serial_log("BROWSER: Redirecting...");
        navigate_internal(loc, depth + 1);
        return;
      }
    }
    g_browser.content_len = ret;
    g_browser.content[ret] = 0;
    strcpy(g_browser.status, "Done");
    parse_html();
    layout_content(800);
  } else {
    strcpy(g_browser.content,
           "<html><body><h1>Error</h1><p>Failed.</p></body></html>");
    g_browser.content_len = strlen(g_browser.content);
    strcpy(g_browser.status, "Failed");
    parse_html();
  }

  g_browser.loading = false;
}

void navigate(const char *url) { navigate_internal(url, 0); }

void draw(Window *w) {
  // 1. CHROME (Now inside window boundaries)
  int tab_h = 32;
  int toolbar_h = 38;

  // Tab Bar Background
  FB::rect(w->x, w->y, w->w, tab_h, 0xDDE1E6);

  // Buttons (Top Right)
  int btn_w = 12;
  int btn_spacing = 8;
  int btn_y = w->y + 10;
  int close_x = w->x + w->w - 20;
  int max_x = close_x - btn_w - btn_spacing;
  int min_x = max_x - btn_w - btn_spacing;

  // Close (Red)
  FB::rect(close_x, btn_y, btn_w, btn_w, 0xFF5F57);
  // Maximize (Green)
  FB::rect(max_x, btn_y, btn_w, btn_w, 0x28C840);
  // Minimize (Yellow)
  FB::rect(min_x, btn_y, btn_w, btn_w, 0xFEBC2E);

  // Active Tab
  FB::rect(w->x + 8, w->y + 4, 180, tab_h - 4, 0xFFFFFF);

  auto font_ui = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font_ui, w->x + 20, w->y + 12, "New Tab", 0x333333);

  // Toolbar
  int toolbar_y = w->y + tab_h;
  FB::rect(w->x, toolbar_y, w->w, toolbar_h, 0xFFFFFF);
  FB::rect(w->x, toolbar_y + toolbar_h, w->w, 1, 0xE0E0E0);

  // URL Bar
  int url_x = w->x + 100;
  int url_w = w->w - 200; // Adjusted for buttons
  int url_h = 26;
  int url_y = toolbar_y + 6;
  uint32_t url_bg = g_browser.url_focused ? 0xFFFFFF : 0xF1F3F4;
  uint32_t url_border = g_browser.url_focused ? 0x1A73E8 : 0xF1F3F4;

  FB::rect(url_x, url_y, url_w, url_h, url_bg);

  if (g_browser.url_focused) {
    FB::rect(url_x, url_y, url_w, 1, url_border);
    FB::rect(url_x, url_y + url_h - 1, url_w, 1, url_border);
    FB::rect(url_x, url_y, 1, url_h, url_border);
    FB::rect(url_x + url_w - 1, url_y, 1, url_h, url_border);
  }

  if (strlen(g_browser.url) > 0)
    FontSystem::draw_text(font_ui, url_x + 12, url_y + 8, g_browser.url,
                          0x000000);
  else
    FontSystem::draw_text(font_ui, url_x + 12, url_y + 8, "Type URL...",
                          0xAAAAAA);

  // 2. CONTENT RENDERING
  int content_y = toolbar_y + toolbar_h;
  int content_h = w->h - (tab_h + toolbar_h);
  FB::rect(w->x, content_y, w->w, content_h, 0xFFFFFF);

  if (g_browser.loading) {

    FontSystem::draw_text(font_ui, w->x + w->w / 2 - 20, content_y + 40,
                          "Loading...", 0x000000);
    return;
  }

  // Draw DOM Nodes
  int cx = w->x + 10;
  int cy = content_y + 10 - g_browser.scroll_y;
  int max_w = w->w - 20;
  int line_h = 0;

  auto font_reg = FontSystem::font_load("default", 8);
  auto font_bold = FontSystem::font_load("bold", 16);

  for (int i = 0; i < node_count; i++) {
    LayoutNode *n = &nodes[i];
    if (n->hidden)
      continue;

    FontSystem::FontHandle font = n->style.is_bold ? font_bold : font_reg;
    int glyph_h = (font.size > 8) ? 16 : 8;

    if (n->style.is_block) {
      if (cx > w->x + 10) {
        cy += line_h;
        cx = w->x + 10;
        line_h = 0;
      }
      cy += n->style.margin_top;
    }

    if (strlen(n->text) > 0) {
      char *word_start = n->text;
      char *c = n->text;
      while (*c) {
        if (*c == ' ' || *(c + 1) == 0) {
          int len = c - word_start + 1;
          if (*(c + 1) == 0 && *c != ' ')
            len++;

          char word[64];
          int copy_len = len < 63 ? len : 63;
          strncpy(word, word_start, copy_len);
          word[copy_len] = 0;
          if (word[copy_len - 1] == ' ')
            word[copy_len - 1] = 0;

          int w_px = FontSystem::text_width(font, word);

          if (cx + w_px > w->x + max_w) {
            cy += (line_h > 0 ? line_h : glyph_h);
            cx = w->x + 10;
            line_h = 0;
          }

          if (cy + glyph_h > content_y && cy < content_y + content_h) {
            if (n->style.background_color != 0) {
              // Approximate background for the word
              FB::rect(cx, cy, w_px, glyph_h, n->style.background_color);
            }
            FontSystem::draw_text(font, cx, cy, word, n->style.color);
          }

          cx += w_px + 8;
        }
        if (*c == ' ')
          word_start = c + 1;
        c++;
      }
      if (line_h < glyph_h)
        line_h = glyph_h;
    }

    if (n->style.is_block) {
      cy += line_h;
      cx = w->x + 10;
      line_h = 0;
      cy += n->style.margin_bottom;
    }
  }
}

void key(Window *w, int k, int state) {
  if (state == 0)
    return;

  if (g_browser.url_focused) {
    int len = strlen(g_browser.url);
    if (k == 8) {
      if (len > 0) {
        g_browser.url[len - 1] = 0;
        g_browser.url_cursor--;
      }
    } else if (k == 13) {
      g_browser.url_focused = false;
      navigate(g_browser.url);
    } else if (k >= 32 && k <= 126) {
      if (len < 255) {
        g_browser.url[len] = (char)k;
        g_browser.url[len + 1] = 0;
        g_browser.url_cursor++;
      }
    }
  }
}

bool click(Window *w, int mx, int my) {
  int tab_h = 32;
  int toolbar_h = 38;

  if (my < tab_h) {
    // Buttons check
    int btn_y = 10;
    int close_x = w->w - 20;
    int max_x = close_x - 20;
    int min_x = max_x - 20;

    if (my >= btn_y && my <= btn_y + 12) {
      if (mx >= close_x && mx <= close_x + 12) {
        w->alive = false;
        return true;
      }
      if (mx >= max_x && mx <= max_x + 12) {
        // Maximize placeholder
        serial_log("BROWSER: Maximize clicked");
        return true;
      }
      if (mx >= min_x && mx <= min_x + 12) {
        // Minimize placeholder
        serial_log("BROWSER: Minimize clicked");
        return true;
      }
    }
    g_browser.url_focused = false;
  } else if (my < tab_h + toolbar_h) {
    // Toolbar area
    int url_y = tab_h + 6;
    int url_x = 100;
    int url_w = w->w - 200;

    if (mx >= url_x && mx <= url_x + url_w && my >= url_y && my <= url_y + 26) {
      g_browser.url_focused = true;
    } else if (mx > 60 && mx < 100) {
      navigate(g_browser.url);
    } else {
      g_browser.url_focused = false;
    }
  } else {
    g_browser.url_focused = false;
    g_browser.scroll_y += 20;
  }
  return true;
}

} // namespace Browser
