#ifndef GUI_COMMON_H
#define GUI_COMMON_H

#include <stdint.h>

// Theme
namespace ThemeEngine {
struct Theme {
  uint32_t bg;
  uint32_t fg;
  uint32_t accent;
  uint32_t border;
};
} // namespace ThemeEngine

struct Window;
typedef void (*DrawFn)(Window *);
typedef bool (*ClickFn)(Window *, int, int);
typedef void (*KeyFn)(Window *, int, int);

// Context
enum ContextTarget { TARGET_DESKTOP, TARGET_FILE, TARGET_FOLDER, TARGET_EMPTY };
enum ContextAction {
  ACT_REFRESH,
  ACT_OPEN,
  ACT_NEW_FOLDER,
  ACT_NEW_FILE,
  ACT_DELETE,
  ACT_RENAME,
  ACT_OPEN_TERMINAL,
  ACT_DISPLAY_SETTINGS,
  ACT_PERSONALIZE,
  ACT_SORT,
  ACT_UNDO,
  ACT_REDO,
  ACT_RESTORE,
  ACT_PURGE,
  ACT_SELECT,
  ACT_SELECT_ALL
};
struct ContextIntent {
  ContextTarget target;
  ContextAction action;
  char path[256];
  char new_path[256];
};

struct ExtendedAction {
  const char *label;
  void (*handler)(const char *path, void *ctx);
};

struct ContextProvider {
  void *app_ctx;
  int (*populate)(ExtendedAction *out, int max, const char *path,
                  void *app_ctx);
};

namespace AnimationEngine {
struct Animation {
  float value;
  float target;
  float speed;
};
} // namespace AnimationEngine

struct Window {
  int x, y, w, h;
  char title[64];
  ThemeEngine::Theme *theme;
  bool alive;
  bool focused;
  bool wants_keyboard;
  AnimationEngine::Animation fade;
  DrawFn draw;
  ClickFn click;
  KeyFn key;
  uint32_t *backbuffer;
  int id;
  int client_fd;
  ContextProvider context_provider;
};

// --- API Declarations ---

namespace FB {
void rect(int x, int y, int w, int h, uint32_t c);
void put(int x, int y, uint32_t c);
void blend_rect(int x, int y, int w, int h, uint32_t c, uint8_t alpha);
} // namespace FB

namespace FontSystem {
struct FontHandle {
  int size;
  bool valid;
};
FontHandle font_load(const char *name, int size);
void draw_text(FontHandle f, int x, int y, const char *s, uint32_t c);
int text_width(FontHandle f, const char *s);
} // namespace FontSystem

namespace IconSystem {
enum IconID {
  ICON_FOLDER,
  ICON_FILE,
  ICON_CLOSE,
  ICON_BACK,
  ICON_HOME,
  ICON_TERMINAL,
  ICON_MONITOR,
  ICON_CALC,
  ICON_NOTEPAD,
  ICON_BROWSER
};
void draw_icon(IconID id, int x, int y, int size, uint32_t accent = 0);
} // namespace IconSystem

namespace UI {
void button(int x, int y, int w, int h, const char *t, bool hover);
void list_item(int x, int y, int w, int h, const char *t, bool sel, bool hover);
} // namespace UI

#endif
