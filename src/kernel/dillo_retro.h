#ifndef DILLO_RETRO_H
#define DILLO_RETRO_H

#include "../lib/dillo/dw/core.hh"
#include "../lib/dillo/dw/view.hh"
#include "gui_system.h" // For FB access

namespace dw {
namespace core {

class RetroPlatform : public Platform {
public:
  RetroPlatform();
  void setLayout(Layout *layout) override;
  void attachView(View *view) override;
  void detachView(View *view) override;

  int textWidth(style::Font *font, const char *text, int len) override;
  char *textToUpper(const char *text, int len) override;
  char *textToLower(const char *text, int len) override;
  int nextGlyph(const char *text, int idx) override;
  int prevGlyph(const char *text, int idx) override;

  float dpiX() override { return 96.0f; }
  float dpiY() override { return 96.0f; }

  int addIdle(void (Layout::*func)()) override { return 0; }
  void removeIdle(int idleId) override {}

  style::Font *createFont(style::FontAttrs *attrs, bool tryEverything) override;
  bool fontExists(const char *name) override { return true; }
  style::Color *createColor(int color) override;
  style::Tooltip *createTooltip(const char *text) override { return nullptr; }
  void cancelTooltip() override {}

  Imgbuf *createImgbuf(Imgbuf::Type type, int width, int height,
                       double gamma) override;
  void copySelection(const char *text, int destination) override {}
  ui::ResourceFactory *getResourceFactory() override { return nullptr; }

private:
  Layout *layout;
};

class RetroView : public View {
public:
  RetroView(int x, int y, int w, int h);
  void draw_text(int x, int y, style::Font *font, const char *text, int len,
                 style::Color *color) override;
  void draw_rect(int x, int y, int w, int h, style::Color *color) override;
  // ... add more drawing overrides here ...

  void set_size(int w, int h) {
    this->w = w;
    this->h = h;
  }

private:
  int x, y, w, h;
};

} // namespace core
} // namespace dw

#endif
