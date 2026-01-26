/* =========================================================
   GUI SUBSYSTEM MASTER (Asli GUI yahan hai)
   Baaki sab toh bass kernel ke nakhre hain

   Warning: Isme panga mat lena varna boot nahi hoga.
   ========================================================= */

#include "../drivers/serial.h"
#include "../include/font.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "pmm.h"
#include "process.h"
#include <stdint.h>

/* 🧱 SYSTEM INTERFACES (Kernel ke saath baat-cheet ka jariya) */
extern "C" {
void *sys_get_framebuffer();
int sys_fb_width();
int sys_fb_height();
void sys_fb_swap();
void sys_get_mouse(int *, int *, int *);
uint32_t sys_time_ms();
int sys_spawn(const char *path, char **argv);
int sys_read_key(int *key, int *state);
int sys_open(const char *, int);
int sys_readdir(int, uint32_t, void *);
int sys_close(int);
void bga_set_video_mode(uint32_t, uint32_t, uint32_t);
int sys_disk_stats(uint64_t *total, uint64_t *free);
int sys_shmget(uint32_t key, uint32_t size, int flags);
void *sys_shmat(int id);
int sys_read(int fd, void *buf, int size);
int sys_write(int fd, void *buf, int size);

/* 🧾 CONTEXT INTENT (Menu pe click karne se kya hoga) */
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

int sys_context_execute(ContextIntent *intent);
int sys_is_dir(const char *path);
int sys_move(const char *src, const char *dst);
int sys_copy(const char *src, const char *dst);

struct DirEntry {
  char name[64];
  uint32_t type;
};

/* 🧾 FS TRANSACTION (File system ki dhandli) */
#define MAX_PATH 256
enum FsOpType { FS_OPEN, FS_CREATE, FS_DELETE, FS_MOVE, FS_COPY, FS_RENAME };
struct FsTransaction {
  FsOpType type;
  char src[MAX_PATH];
  char dst[MAX_PATH];
  uint32_t flags;
};
int sys_fs_transaction(const FsTransaction *tx);

/* 🧩 EXPLORER KERNEL (Dono ke beech ka mediator) */
#define MAX_NAME 64
enum SyscallID {
  SYS_CREATE_FILE,
  SYS_DELETE_FILE,
  SYS_RENAME_FILE,
  SYS_COPY_FILE,
  SYS_PASTE_FILE,
  SYS_GET_FILES
};
struct ExplorerItem {
  char name[64];
  char full_path[MAX_PATH];
  uint32_t type;
};
extern "C" void explorer_init();
extern "C" void explorer_load_directory(const char *path);
extern "C" void explorer_open_item(int index);
extern "C" void explorer_double_click(int index);
extern "C" void explorer_create_folder(const char *name);
extern "C" void explorer_context_action(int index, int act);
}

struct Rect {
  int x, y, w, h;
};

/* 🚀 GPU ACCELERATION LAYER (Taaki smooth chale) */
enum GpuCmdType { GPU_RECT, GPU_BLEND_RECT, GPU_TEXT, GPU_ICON };
struct GpuCmd {
  GpuCmdType type;
  Rect r;
  uint32_t color;
  uint8_t alpha;
  int icon_id;
  char text[64];
};

#define GPU_CMD_MAX 2048
static GpuCmd gpu_cmds[GPU_CMD_MAX];
static int gpu_count = 0;

/* 📋 CLIPBOARD & DRAG-DROP (Idhar se udhar karne ke liye) */
struct Clipboard {
  char path[256];
  bool cut;
};
static Clipboard g_clipboard = {{0}, false};

struct RenameState {
  bool active;
  char old_path[256];
  char new_name[64];
  int cursor;
  ContextTarget target;
};
static RenameState g_rename = {false, {0}, {0}, 0, TARGET_EMPTY};

struct DragState {
  bool active;
  char path[256];
};
static DragState g_drag = {false, {0}};

namespace DesktopSystem {
void refresh();
void select_all();
void select_path(const char *path);
} // namespace DesktopSystem

void launch_explorer();
void launch_terminal();
void launch_sysmonitor();
void launch_calculator();
void launch_notepad();
void launch_browser();
void load_dir();
void open_item(const char *path);

bool name_match(const char *a, const char *b) {
  int i = 0;
  while (a[i] || b[i]) {
    char c1 = a[i];
    char c2 = b[i];
    if (c1 >= 'a' && c1 <= 'z')
      c1 -= 32;
    if (c2 >= 'a' && c2 <= 'z')
      c2 -= 32;
    if (c1 != c2)
      return false;
    if (!a[i] || !b[i])
      break;
    i++;
  }
  return true;
}

void clipboard_copy(const char *path) {
  int i = 0;
  while (path[i] && i < 255) {
    g_clipboard.path[i] = path[i];
    i++;
  }
  g_clipboard.path[i] = 0;
  g_clipboard.cut = false;
}

void clipboard_cut(const char *path) {
  int i = 0;
  while (path[i] && i < 255) {
    g_clipboard.path[i] = path[i];
    i++;
  }
  g_clipboard.path[i] = 0;
  g_clipboard.cut = true;
}

void build_full_path(char *out, const char *dir, const char *file) {
  int p = 0;
  while (dir[p] && p < 255) {
    out[p] = dir[p];
    p++;
  }
  if (p > 1 && out[p - 1] != '/' && p < 255)
    out[p++] = '/';
  int q = 0;
  while (file[q] && p < 255) {
    out[p++] = file[q++];
  }
  out[p] = 0;
}

void clipboard_paste(const char *dst_dir) {
  if (!g_clipboard.path[0])
    return;
  char dst[256];
  const char *name = g_clipboard.path;
  for (int i = 0; g_clipboard.path[i]; i++)
    if (g_clipboard.path[i] == '/')
      name = &g_clipboard.path[i + 1];

  build_full_path(dst, dst_dir, name);

  FsTransaction tx;
  uint8_t *p = (uint8_t *)&tx;
  for (int j = 0; j < (int)sizeof(tx); j++)
    p[j] = 0;

  tx.type = g_clipboard.cut ? FS_MOVE : FS_COPY;
  {
    int j = 0;
    while (g_clipboard.path[j] && j < 255) {
      tx.src[j] = g_clipboard.path[j];
      j++;
    }
    tx.src[j] = 0;
  }
  {
    int j = 0;
    while (dst[j] && j < 255) {
      tx.dst[j] = dst[j];
      j++;
    }
    tx.dst[j] = 0;
  }

  sys_fs_transaction(&tx);

  if (g_clipboard.cut)
    g_clipboard.path[0] = 0;
}

void drag_start(const char *path) {
  g_drag.active = true;
  int i = 0;
  while (path[i] && i < 255) {
    g_drag.path[i] = path[i];
    i++;
  }
  g_drag.path[i] = 0;
}

void drag_drop(const char *target_dir) {
  if (!g_drag.active)
    return;
  char dst[256];
  const char *name = g_drag.path;
  for (int i = 0; g_drag.path[i]; i++)
    if (g_drag.path[i] == '/')
      name = &g_drag.path[i + 1];
  build_full_path(dst, target_dir, name);

  FsTransaction tx;
  uint8_t *p = (uint8_t *)&tx;
  for (int j = 0; j < (int)sizeof(tx); j++)
    p[j] = 0;

  tx.type = FS_MOVE;
  {
    int j = 0;
    while (g_drag.path[j] && j < 255) {
      tx.src[j] = g_drag.path[j];
      j++;
    }
    tx.src[j] = 0;
  }
  {
    int j = 0;
    while (dst[j] && j < 255) {
      tx.dst[j] = dst[j];
      j++;
    }
    tx.dst[j] = 0;
  }

  sys_fs_transaction(&tx);
  g_drag.active = false;
}

static inline bool inside(Rect r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* =========================================================
   LAYER 1: FRAMEBUFFER + INPUT (KERNEL BOUNDARY)
   Yahan seedha pixels ke saath khelte hain
   ========================================================= */
namespace FB {
static uint32_t *buf;
static int W, H;

void init() {
  buf = (uint32_t *)sys_get_framebuffer();
  W = sys_fb_width();
  H = sys_fb_height();
}

inline void put(int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= W || y >= H)
    return;
  buf[y * W + x] = c;
}

void rect(int x, int y, int w, int h, uint32_t c) {
  for (int i = 0; i < h; i++)
    for (int j = 0; j < w; j++)
      put(x + j, y + i, c);
}

void clear(uint32_t c) {
  for (int i = 0; i < W * H; i++)
    buf[i] = c;
}

void blend_rect(int x, int y, int w, int h, uint32_t c, uint8_t a) {
  uint8_t r1 = (c >> 16) & 255, g1 = (c >> 8) & 255, b1 = c & 255;
  for (int i = 0; i < h; i++) {
    for (int j = 0; j < w; j++) {
      int px = x + j, py = y + i;
      if (px < 0 || py < 0 || px >= W || py >= H)
        continue;
      uint32_t bg = buf[py * W + px];
      uint8_t r2 = (bg >> 16) & 255, g2 = (bg >> 8) & 255, b2 = bg & 255;
      uint8_t r = (r1 * a + r2 * (255 - a)) / 255;
      uint8_t g = (g1 * a + g2 * (255 - a)) / 255;
      uint8_t b = (b1 * a + b2 * (255 - a)) / 255;
      buf[py * W + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
  }
}

inline void swap() { sys_fb_swap(); }
} // namespace FB

namespace Input {
static int mx, my, mb, pmb;
static uint32_t last_click_time = 0;
static int last_click_x, last_click_y;
static bool g_has_clicked = false;
static bool g_is_double_click = false;
#define DBLCLICK_MS 800

void poll() {
  pmb = mb;
  sys_get_mouse(&mx, &my, &mb);

  if ((mb & 1) && !(pmb & 1)) {
    uint32_t now = sys_time_ms();
    int dx = mx - last_click_x;
    int dy = my - last_click_y;
    if (dx < 0)
      dx = -dx;
    if (dy < 0)
      dy = -dy;

    g_is_double_click = g_has_clicked &&
                        (now - last_click_time < DBLCLICK_MS) &&
                        (dx < 30 && dy < 30);

    if (g_is_double_click) {
      serial_log("INPUT: DOUBLE-CLICK detected");
    } else {
      serial_log_hex("INPUT: click dt=", now - last_click_time);
    }

    last_click_time = now;
    last_click_x = mx;
    last_click_y = my;
    g_has_clicked = true;
  } else {
    g_is_double_click = false;
  }
}

inline bool clicked() { return (mb & 1) && !(pmb & 1); }
inline bool right_clicked() { return (mb & 2) && !(pmb & 2); }
inline bool held() { return mb & 1; }
inline int x() { return mx; }
inline int y() { return my; }
inline bool inside_rect(Rect r) {
  return mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h;
}

inline bool is_double_click() { return g_is_double_click; }
} // namespace Input

/* =========================================================
   1️⃣ FRAMEBUFFER UTILS (Screen pe rang bharna)
   ========================================================= */
namespace FontSystem {

struct FontHandle {
  int size;
  bool valid;
};

// For now, we support 8x8 and 16x16 (scaled)
FontHandle font_load(const char *name, int size) {
  (void)name;
  return {size, true};
}

void draw_text(FontHandle f, int x, int y, const char *s, uint32_t c) {
  int scale = (f.size > 8) ? 2 : 1;

  while (*s) {
    const uint8_t *g = font8x8_basic[(int)*s++];
    for (int r = 0; r < 8; r++) {
      for (int b = 0; b < 8; b++) {
        if (g[r] & (1 << (7 - b))) {
          for (int sy = 0; sy < scale; sy++)
            for (int sx = 0; sx < scale; sx++)
              FB::put(x + b * scale + sx, y + r * scale + sy, c);
        }
      }
    }
    x += 8 * scale;
  }
}

int text_width(FontHandle f, const char *s) {
  int len = 0;
  while (*s++)
    len++;
  return len * 8 * ((f.size > 8) ? 2 : 1);
}

} // namespace FontSystem

/* =========================================================
   2️⃣ ICON SYSTEM (MODERN LOOK)
   ========================================================= */
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

void draw_icon(IconID id, int x, int y, int size, uint32_t accent) {
  switch (id) {
  case ICON_FOLDER:
    // Folder tab
    FB::rect(x, y, size / 2, size / 6, accent);
    // Folder body
    FB::rect(x, y + size / 6, size, size * 2 / 3, accent);
    break;

  case ICON_FILE:
    // Document shape
    FB::rect(x + size / 6, y, size * 2 / 3, size, 0xEEEEEE);
    FB::rect(x + size / 6, y, size * 2 / 3, 2, accent);
    FB::rect(x + size / 6, y + size - 2, size * 2 / 3, 2, accent);
    break;

  case ICON_HOME:
    // Simple house shape
    FB::rect(x, y + size / 2, size, size / 2, accent);
    FB::rect(x + size / 4, y + size / 4, size / 2, size / 4, accent);
    break;

  case ICON_CLOSE:
    FB::rect(x, y, size, size, 0xFF4444);
    FB::rect(x + 2, y + size / 2 - 1, size - 4, 2, 0xFFFFFF);
    break;

  case ICON_BACK: {
    // Arrow pointing left
    int mid = size / 2;
    FB::rect(x + 2, y + mid - 1, size - 4, 2, accent); // horizontal line
    for (int i = 0; i < size / 3; i++) {
      FB::put(x + 2 + i, y + mid - i, accent);
      FB::put(x + 2 + i, y + mid + i, accent);
    }
  } break;
  case ICON_TERMINAL:
    // Terminal window shape
    FB::rect(x, y, size, size, 0x222222);
    FB::rect(x, y, size, size / 6, 0x444444); // Title bar
    // Simple prompt symbol ">"
    FB::rect(x + 4, y + size / 3, 2, 6, 0x00FF00);
    FB::rect(x + 6, y + size / 3 + 2, 2, 2, 0x00FF00);
    break;
  case ICON_MONITOR:
    // Monitor with graph
    FB::rect(x, y, size, size, 0x1A2A3A);
    FB::rect(x, y, size, size / 6, 0x4FC3F7); // Header
    // Graph bars
    FB::rect(x + 4, y + size / 2, 4, size / 3, 0x81C784);
    FB::rect(x + 10, y + size / 3, 4, size / 2, 0xFFB74D);
    FB::rect(x + 16, y + size / 2 + 4, 4, size / 4, 0xFF8A65);
    break;
  case ICON_CALC:
    // Calculator icon
    FB::rect(x, y, size, size, 0x444444);
    FB::rect(x + 2, y + 2, size - 4, size / 3, 0xDDDDDD); // display area
    // buttons
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        FB::rect(x + 2 + j * (size / 3), y + size / 2 + i * (size / 6),
                 size / 4, size / 8, accent);
      }
    }
    break;
  case ICON_NOTEPAD:
    // Notepad icon
    FB::rect(x + 5, y + 2, size - 10, size - 4, 0xF5F5F5); // Paper
    FB::rect(x + 8, y + 8, size - 16, 2, 0xCCCCCC);        // Lines
    FB::rect(x + 8, y + 14, size - 16, 2, 0xCCCCCC);
    FB::rect(x + 8, y + 20, size - 16, 2, 0xCCCCCC);
    FB::rect(x + 5, y + 2, size - 10, size / 6, 0x448AFF); // Header
    break;
  case ICON_BROWSER:
    // Browser icon (Globe-ish)
    FB::rect(x, y, size, size, 0xEEEEEE);
    FB::rect(x + 2, y + 2, size - 4, size / 6, 0x3366FF); // URL bar area
    // Globe circle mockup
    int cx = x + size/2;
    int cy = y + size/2 + 2;
    int r = size/3;
    FB::rect(cx - r, cy - r, r*2, r*2, 0x448AFF);
    FB::rect(cx - r + 4, cy - 2, r*2 - 8, 4, 0xFFFFFF); // Equator
    FB::rect(cx - 2, cy - r + 4, 4, r*2 - 8, 0xFFFFFF); // Meridian
    break;
  }
}

} // namespace IconSystem

/* =========================================================
   UI TOOLKIT (Phase 4)
   ========================================================= */
namespace UI {

void button(int x, int y, int w, int h, const char *t, bool hover) {
  uint32_t bg = hover ? 0xDDE8FF : 0xEEEEEE;
  FB::rect(x, y, w, h, bg);
  FB::rect(x, y, w, 1, 0xAAAAAA);
  FB::rect(x, y + h - 1, w, 1, 0x888888);
  auto font = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font, x + 8, y + 8, t, 0x333333);
}

void list_item(int x, int y, int w, int h, const char *t, bool sel,
               bool hover) {
  if (sel)
    FB::blend_rect(x, y, w, h, 0x3366FF, 80);
  else if (hover)
    FB::blend_rect(x, y, w, h, 0x000000, 20);
  auto font = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font, x + 10, y + 6, t, 0x222222);
}

} // namespace UI

/* 🖱️ CONTEXT MENU EXTENSIONS */
struct ExtendedAction {
  const char *label;
  void (*handler)(const char *path, void *ctx);
};

struct ContextProvider {
  void *app_ctx;
  int (*populate)(ExtendedAction *out, int max, const char *path,
                  void *app_ctx);
};
struct Scroll {
  int offset;
  int max;
};

void draw_scrollbar(Rect r, Scroll *s) {
  FB::rect(r.x, r.y, r.w, r.h, 0xEEEEEE);
  int content_h = s->max > r.h ? s->max : r.h;
  int bar_h = (r.h * r.h) / content_h;
  if (bar_h < 10)
    bar_h = 10;
  int bar_y = r.y + (s->offset * (r.h - bar_h)) / (content_h - r.h + 1);
  FB::rect(r.x, bar_y, r.w, bar_h, 0x999999);
}

/* =========================================================
   CONTEXT MENU
   ========================================================= */
struct MenuItem {
  const char *label;
  ContextAction action;
  void (*handler)(const char *path, void *ctx);
};

struct ContextMenu {
  Rect area;
  MenuItem items[12];
  int count;
  bool visible;
  ContextTarget target;
  char path[256];
};

static ContextMenu g_ctx_menu;

void add_context_item(const char *label, ContextAction action) {
  if (g_ctx_menu.count >= 12)
    return;
  g_ctx_menu.items[g_ctx_menu.count++] = {label, action, nullptr};
}

void add_extended_item(const char *label,
                       void (*handler)(const char *, void *)) {
  if (g_ctx_menu.count >= 12)
    return;
  g_ctx_menu.items[g_ctx_menu.count++] = {label, ACT_UNDO, handler};
}

void show_context_menu(int x, int y, ContextTarget target, const char *path,
                       ContextProvider *provider = nullptr) {
  g_ctx_menu.area.x = x;
  g_ctx_menu.area.y = y;
  g_ctx_menu.area.w = 180;
  g_ctx_menu.visible = true;
  g_ctx_menu.target = target;
  g_ctx_menu.count = 0;

  int i = 0;
  if (path) {
    while (path[i] && i < 255) {
      g_ctx_menu.path[i] = path[i];
      i++;
    }
  }
  g_ctx_menu.path[i] = 0;

  if (target == TARGET_DESKTOP) {
    add_context_item("View", ACT_SORT);
    add_context_item("Refresh", ACT_REFRESH);
    add_context_item("Select All", ACT_SELECT_ALL);
    add_context_item("Paste", ACT_UNDO); // Mock paste
    add_context_item("Undo", ACT_UNDO);
    add_context_item("Redo", ACT_REDO);
    add_context_item("New Folder", ACT_NEW_FOLDER);
    add_context_item("Open Terminal", ACT_OPEN_TERMINAL);
  } else {
    add_context_item("Open", ACT_OPEN);
    add_context_item("Select", ACT_SELECT);
    add_context_item("Copy", ACT_UNDO);
    add_context_item("Cut", ACT_UNDO);
    add_context_item("Rename", ACT_RENAME);
    add_context_item("Delete", ACT_DELETE);
    add_context_item("Restore", ACT_RESTORE);
    add_context_item("Purge", ACT_PURGE);
  }

  if (provider && provider->populate) {
    ExtendedAction extra[4];
    int n = provider->populate(extra, 4, path, provider->app_ctx);
    for (int i = 0; i < n; i++)
      add_extended_item(extra[i].label, extra[i].handler);
  }

  g_ctx_menu.area.h = g_ctx_menu.count * 28;
}

void draw_context_menu() {
  if (!g_ctx_menu.visible)
    return;

  // Shadow
  FB::blend_rect(g_ctx_menu.area.x + 4, g_ctx_menu.area.y + 4,
                 g_ctx_menu.area.w, g_ctx_menu.area.h, 0x000000, 80);

  // Background
  FB::rect(g_ctx_menu.area.x, g_ctx_menu.area.y, g_ctx_menu.area.w,
           g_ctx_menu.area.h, 0xFFFFFF);
  FB::rect(g_ctx_menu.area.x, g_ctx_menu.area.y, g_ctx_menu.area.w, 1,
           0xCCCCCC); // Border top

  auto font = FontSystem::font_load("default", 8);
  for (int i = 0; i < g_ctx_menu.count; i++) {
    Rect r = {g_ctx_menu.area.x, g_ctx_menu.area.y + i * 28, g_ctx_menu.area.w,
              28};
    bool hover = Input::inside_rect(r);

    if (hover) {
      FB::rect(r.x, r.y, r.w, r.h, 0x3366FF);
      FontSystem::draw_text(font, r.x + 12, r.y + 10, g_ctx_menu.items[i].label,
                            0xFFFFFF);
    } else {
      FontSystem::draw_text(font, r.x + 12, r.y + 10, g_ctx_menu.items[i].label,
                            0x333333);
    }
  }
}

void context_click(int index) {
  if (index < 0 || index >= g_ctx_menu.count)
    return;

  MenuItem &m = g_ctx_menu.items[index];
  if (m.handler) {
    m.handler(g_ctx_menu.path, nullptr);
  } else {
    // Handle GUI local actions first
    if (m.action == ACT_SELECT_ALL) {
      DesktopSystem::select_all();
    } else if (m.action == ACT_SELECT) {
      DesktopSystem::select_path(g_ctx_menu.path);
    } else if (m.action == ACT_RENAME) {
      g_rename.active = true;
      strcpy(g_rename.old_path, g_ctx_menu.path);
      g_rename.target = g_ctx_menu.target;
    } else if (m.action == ACT_OPEN) {
      serial_log("GUI: Context Menu -> OPEN: ");
      serial_log(g_ctx_menu.path);
      open_item(g_ctx_menu.path);
    } else {
      static ContextIntent intent; // Static to save stack
      intent.target = g_ctx_menu.target;
      intent.action = m.action;
      for (int i = 0; i < 256; i++)
        intent.path[i] = g_ctx_menu.path[i];
      intent.new_path[0] = 0;
      serial_log("GUI: Executing context action on path: ");
      serial_log(intent.path);
      sys_context_execute(&intent);
      DesktopSystem::refresh();
    }
  }
  g_ctx_menu.visible = false;
}

void handle_context_input() {
  if (!g_ctx_menu.visible)
    return;

  if (Input::clicked()) {
    if (Input::inside_rect(g_ctx_menu.area)) {
      int idx = (Input::y() - g_ctx_menu.area.y) / 28;
      context_click(idx);
    } else {
      g_ctx_menu.visible = false;
    }
  }
}

void draw_rename_dialog() {
  if (!g_rename.active)
    return;

  int w = 300, h = 100;
  int x = (FB::W - w) / 2;
  int y = (FB::H - h) / 2;

  // Background
  FB::rect(x, y, w, h, 0xFFFFFF);
  FB::rect(x, y, w, 2, 0x3366FF); // Header
  FB::rect(x, y, 1, h, 0xAAAAAA);
  FB::rect(x + w - 1, y, 1, h, 0xAAAAAA);
  FB::rect(x, y + h - 1, w, 1, 0xAAAAAA);

  auto font = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font, x + 10, y + 10, "Rename Item", 0x333333);
  FontSystem::draw_text(font, x + 10, y + 35, "Enter new name:", 0x666666);

  // Input area
  FB::rect(x + 10, y + 55, w - 20, 25, 0xF5F5F5);
  FB::rect(x + 10, y + 55, w - 20, 1, 0xCCCCCC);
  FontSystem::draw_text(font, x + 15, y + 62, g_rename.new_name, 0x333333);

  // Cursor
  int cursor_x = x + 15 + strlen(g_rename.new_name) * 8;
  FB::rect(cursor_x, y + 60, 2, 15, 0x3366FF);

  FontSystem::draw_text(font, x + 10, y + 85,
                        "Press ENTER to confirm, ESC to cancel", 0x999999);
}

void handle_rename_key(int k) {
  if (k == 27) { // ESC
    g_rename.active = false;
    return;
  }
  if (k == 13) { // ENTER
    if (strlen(g_rename.new_name) > 0) {
      static ContextIntent intent;
      intent.target = g_rename.target;
      intent.action = ACT_RENAME;
      strcpy(intent.path, g_rename.old_path);

      // Build new path
      char new_path[256];
      strcpy(new_path, g_rename.old_path);
      // Remove last component
      int last_slash = -1;
      for (int i = 0; new_path[i]; i++)
        if (new_path[i] == '/')
          last_slash = i;

      if (last_slash != -1)
        new_path[last_slash + 1] = 0;
      else
        new_path[0] = 0;

      strcat(new_path, g_rename.new_name);
      strcpy(intent.new_path, new_path);

      sys_context_execute(&intent);
      DesktopSystem::refresh();
      load_dir(); // Refresh explorer too
    }
    g_rename.active = false;
    return;
  }
  if (k == 8) { // Backspace
    if (g_rename.cursor > 0) {
      g_rename.new_name[--g_rename.cursor] = 0;
    }
    return;
  }
  if (k >= 32 && k <= 126 && g_rename.cursor < 63) {
    g_rename.new_name[g_rename.cursor++] = (char)k;
    g_rename.new_name[g_rename.cursor] = 0;
  }
}

/* =========================================================
   3️⃣ ANIMATION ENGINE (LIGHTWEIGHT, SAFE)
   ========================================================= */
namespace AnimationEngine {

struct Animation {
  float value;
  float target;
  float speed;
};

void animate(Animation *a) {
  if (a->value < a->target) {
    a->value += a->speed;
    if (a->value > a->target)
      a->value = a->target;
  } else if (a->value > a->target) {
    a->value -= a->speed;
    if (a->value < a->target)
      a->value = a->target;
  }
}

} // namespace AnimationEngine

/* =========================================================
   5️⃣ THEME ENGINE
   ========================================================= */
namespace ThemeEngine {

struct Theme {
  uint32_t bg;
  uint32_t fg;
  uint32_t accent;
  uint32_t border;
};

Theme explorer_theme = {0x1E1E2E, 0xFFFFFF, 0xFF4FD8, 0x2A2A40};
Theme desktop_theme = {0x1E1E2F, 0xFFFFFF, 0x5E81AC, 0x000000};

} // namespace ThemeEngine

/* =========================================================
   LAYER 3: WINDOW MANAGER (Khidkiyon ka management)
   ========================================================= */
struct Window;
typedef void (*DrawFn)(Window *);
typedef bool (*ClickFn)(Window *, int, int);
typedef void (*KeyFn)(Window *, int, int);

#define MAX_DESKTOPS 4

enum ResizeEdge { EDGE_NONE, EDGE_LEFT, EDGE_RIGHT, EDGE_BOTTOM, EDGE_TOP };

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

void execute_ipc_draw(Window *w) {
  if (w->backbuffer) {
    for (int j = 0; j < w->h; j++) {
      for (int i = 0; i < w->w; i++) {
        FB::put(w->x + i, w->y + j, w->backbuffer[j * w->w + i]);
      }
    }
  }
}

static Window windows[MAX_DESKTOPS][16];
static int win_count[MAX_DESKTOPS] = {0};
static int current_desktop = 0;
static Window *dragging = nullptr;
static Window *resizing = nullptr;
static ResizeEdge res_edge = EDGE_NONE;
static int drag_dx, drag_dy;

ResizeEdge detect_resize(Window *w, int mx, int my) {
  if (mx >= w->x && mx < w->x + 5 && my >= w->y && my < w->y + w->h)
    return EDGE_LEFT;
  if (mx >= w->x + w->w - 5 && mx < w->x + w->w && my >= w->y &&
      my < w->y + w->h)
    return EDGE_RIGHT;
  if (my >= w->y + w->h - 5 && my < w->y + w->h && mx >= w->x &&
      mx < w->x + w->w)
    return EDGE_BOTTOM;
  return EDGE_NONE;
}

void focus_window(int index);

Window *create_window(int x, int y, int w, int h, const char *t,
                      ThemeEngine::Theme *theme, DrawFn d, ClickFn c) {
  static int next_win_id = 1;
  Window *win = nullptr;

  // Try to reuse a dead slot
  for (int i = 0; i < win_count[current_desktop]; i++) {
    if (!windows[current_desktop][i].alive) {
      win = &windows[current_desktop][i];
      break;
    }
  }

  // Use new slot if available
  if (!win && win_count[current_desktop] < 16) {
    win = &windows[current_desktop][win_count[current_desktop]++];
  }

  if (win) {
    *win = {x,     y,    w,       h,       {0},
            theme, true, true,    false,   {1.0f, 1.0f, 0.05f},
            d,     c,    nullptr, nullptr, next_win_id++};
    for (int i = 0; t && t[i] && i < 63; i++)
      win->title[i] = t[i];
    focus_window(win_count[current_desktop] - 1);
    return win;
  }
  return nullptr; // Too many windows
}

void focus_window(int index) {
  int count = win_count[current_desktop];
  if (index < 0 || index >= count)
    return;

  for (int i = 0; i < count; i++)
    windows[current_desktop][i].focused = false;

  Window tmp = windows[current_desktop][index];
  for (int i = index; i < count - 1; i++)
    windows[current_desktop][i] = windows[current_desktop][i + 1];
  windows[current_desktop][count - 1] = tmp;
  windows[current_desktop][count - 1].focused = true;
}

void switch_desktop(int d) {
  if (d >= 0 && d < MAX_DESKTOPS)
    current_desktop = d;
}

void draw_window(Window *w) {
  // Animate fade
  AnimationEngine::animate(&w->fade);

  if (w->fade.value < 0.1f)
    return; // Fully faded out

  // Shadow with blend
  FB::blend_rect(w->x + 6, w->y - 22, w->w, w->h + 28, 0x000000, 60);

  // Titlebar with blend
  uint32_t titlebar =
      w->focused ? w->theme->accent : (w->theme->accent & 0x7F7F7F);
  FB::blend_rect(w->x, w->y - 28, w->w, 28, titlebar, 230);
  auto font = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font, w->x + 8, w->y - 18, w->title, 0xFFFFFF);

  // Close button with icon
  FB::rect(w->x + w->w - 20, w->y - 20, 16, 16, 0xFF4444);
  IconSystem::draw_icon(IconSystem::ICON_CLOSE, w->x + w->w - 18, w->y - 18, 12,
                        0xFFFFFF);

  // Body
  FB::rect(w->x, w->y, w->w, w->h, w->theme->bg);

  if (w->draw)
    w->draw(w);
}

bool handle_window_input(Window *w, int index) {
  int mx = Input::x();
  int my = Input::y();

  // Titlebar area (including close button)
  if (mx >= w->x && mx < w->x + w->w && my >= w->y - 28 && my < w->y) {
    if (Input::clicked()) {
      if (mx >= w->x + w->w - 20 && mx < w->x + w->w - 4 && my >= w->y - 20 &&
          my < w->y - 4) {
        w->fade.target = 0.0f;
        w->alive = false;
        return true;
      }

      focus_window(index);
      dragging = w;
      drag_dx = mx - w->x;
      drag_dy = my - w->y;
      return true;
    }
    return true; // Hovering titlebar, consume
  }

  // Dragging / Resizing logic
  if (dragging == w && Input::held()) {
    w->x = mx - drag_dx;
    w->y = my - drag_dy;
    return true;
  }

  if (resizing == w && Input::held()) {
    if (res_edge == EDGE_RIGHT)
      w->w = mx - w->x;
    if (res_edge == EDGE_BOTTOM)
      w->h = my - w->y;
    if (res_edge == EDGE_LEFT) {
      int old_x = w->x;
      w->x = mx;
      w->w += (old_x - w->x);
    }
    if (w->w < 100)
      w->w = 100;
    if (w->h < 60)
      w->h = 60;
    return true;
  }

  if ((dragging == w || resizing == w) && !Input::held()) {
    dragging = nullptr;
    resizing = nullptr;
  }

  // Border detection for resizing
  ResizeEdge edge = detect_resize(w, mx, my);
  if (edge != EDGE_NONE && Input::clicked()) {
    resizing = w;
    res_edge = edge;
    focus_window(index);
    return true;
  }

  // Client area
  if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
    if (Input::clicked()) {
      focus_window(index);
      if (w->click)
        w->click(w, mx - w->x, my - w->y);
      return true;
    }
    // For hover effects in explorer_draw (which uses global 'hovered'),
    // we still need to let explorer_draw know the mouse is here.
    // However, we consume the event for hit-testing purposes.
    return true;
  }

  return false;
}

/* =========================================================
   FILE EXPLORER (FINAL CONTRACT)
   ========================================================= */
extern ExplorerItem items[];
extern int item_count;
static int selected = -1;
static int hovered = -1;
static int scroll_y = 0;
static const int ITEM_H = 80;
extern char cwd[];

/* =========================================================
   4️⃣ DESKTOP SYSTEM (REAL DESKTOP)
   ========================================================= */
namespace DesktopSystem {

struct DesktopIcon {
  char name[32];
  IconSystem::IconID icon;
  int x, y;
  void (*launch)();
  bool selected;
};

const int GRID_W = 96;
const int GRID_H = 110;
const int GRID_X_OFF = 32;
const int GRID_Y_OFF = 32;
const int MAX_ICONS = 64;

DesktopIcon icons[32]; // Increased capacity
static int icon_count = 0;
static int dragging_icon_index = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static int prev_x = 0;
static int prev_y = 0;

void add_icon(const char *name, IconSystem::IconID icon, int x, int y,
              void (*launch)()) {
  if (icon_count >= MAX_ICONS)
    return;
  auto &ic = icons[icon_count++];
  int i = 0;
  while (name[i] && i < 31) {
    ic.name[i] = name[i];
    i++;
  }
  ic.name[i] = 0;
  ic.icon = icon;
  ic.x = x;
  ic.y = y;
  ic.launch = launch;
  ic.selected = false;
}

// Helper to find next available grid slot
void find_free_slot(int &x, int &y) {
  for (int row = 0; row < 6; row++) {
    for (int col = 0; col < 10; col++) { // 10 cols, 6 rows
      int test_x = GRID_X_OFF + col * GRID_W;
      int test_y = GRID_Y_OFF + row * GRID_H;

      bool occupied = false;
      for (int i = 0; i < icon_count; i++) {
        // Simple proximity check
        int dx = icons[i].x - test_x;
        int dy = icons[i].y - test_y;
        if (dx * dx + dy * dy < 40 * 40) { // If within ~40px
          occupied = true;
          break;
        }
      }
      if (!occupied) {
        x = test_x;
        y = test_y;
        return;
      }
    }
  }
  // Fallback if full
  x = 32;
  y = 32;
}

void refresh() {
  DesktopIcon old_icons[MAX_ICONS];
  int old_count = icon_count;
  for (int i = 0; i < old_count; i++)
    old_icons[i] = icons[i];

  icon_count = 0;

  // Lambda to preserve position if exists
  auto add_or_update = [&](const char *name, IconSystem::IconID id,
                           void (*func)()) {
    int tx = -1, ty = -1;
    // Try find old pos
    for (int i = 0; i < old_count; i++) {
      if (name_match(old_icons[i].name, name)) {
        tx = old_icons[i].x;
        ty = old_icons[i].y;
        break;
      }
    }

    if (tx == -1) {
      find_free_slot(tx, ty);
    }
    add_icon(name, id, tx, ty, func);
  };

  // 1. Fixed Icons
  add_or_update("Explorer", IconSystem::ICON_FOLDER, launch_explorer);
  add_or_update("Terminal", IconSystem::ICON_TERMINAL, launch_terminal);
  add_or_update("Monitor", IconSystem::ICON_MONITOR, launch_sysmonitor);
  add_or_update("Notepad", IconSystem::ICON_NOTEPAD, launch_notepad);
  add_or_update("Calculator", IconSystem::ICON_CALC, launch_calculator);
  add_or_update("Browser", IconSystem::ICON_BROWSER, launch_browser);

  // 2. Load from REAL VFS (/home/user/Desktop)
  int fd = sys_open("/home/user/Desktop", 0);
  if (fd >= 1000) {
    for (int i = 0; i < MAX_ICONS && icon_count < MAX_ICONS; i++) {
      struct {
        char name[64];
        uint32_t type;
      } tmp;
      if (sys_readdir(fd, i, &tmp) != 0)
        break;
      if (tmp.name[0] == 0)
        break;
      if (strcmp(tmp.name, ".") == 0 || strcmp(tmp.name, "..") == 0)
        continue;
      if (name_match(tmp.name, "Home"))
        continue;

      // Avoid duplicating fixed icons if they exist as files (optional)
      bool duplicate = false;
      for (int k = 0; k < icon_count; k++) {
        if (name_match(icons[k].name, tmp.name)) {
          duplicate = true;
          break;
        }
      }
      if (duplicate)
        continue;

      IconSystem::IconID icon =
          (tmp.type == 2) ? IconSystem::ICON_FOLDER : IconSystem::ICON_FILE;
      void (*launch)() = (tmp.type == 2) ? launch_explorer : nullptr;

      add_or_update(tmp.name, icon, launch);
    }
    sys_close(fd);
  } else {
    serial_log("DESKTOP: Failed to open Desktop dir");
  }
}

void draw_desktop() {
  // Gradient background
  for (int y = 0; y < FB::H; y++) {
    uint32_t c = (y < FB::H / 2) ? 0x1E1E2F : 0x12121C;
    for (int x = 0; x < FB::W; x++)
      FB::put(x, y, c);
  }

  // Draw desktop icons
  auto font = FontSystem::font_load("default", 8);
  for (int i = 0; i < icon_count; i++) {
    if (icons[i].selected) {
      FB::blend_rect(icons[i].x - 4, icons[i].y - 4, 56, 70, 0x7E57C2, 100);
    }
    IconSystem::draw_icon(icons[i].icon, icons[i].x, icons[i].y, 48, 0xFFFFFF);
    FontSystem::draw_text(font, icons[i].x, icons[i].y + 52, icons[i].name,
                          0xFFFFFF);
  }
}

void select_all() {
  for (int i = 0; i < icon_count; i++)
    icons[i].selected = true;
}

void select_path(const char *path) {
  for (int i = 0; i < icon_count; i++) {
    char full[256];
    strcpy(full, "/home/user/Desktop/");
    strcat(full, icons[i].name);
    if (strcmp(full, path) == 0)
      icons[i].selected = true;
    else
      icons[i].selected = false;
  }
}

void handle_desktop_input() {
  if (Input::clicked()) {
    bool hit = false;
    for (int i = 0; i < icon_count; i++) {
      if (Input::x() >= icons[i].x && Input::x() < icons[i].x + 48 &&
          Input::y() >= icons[i].y && Input::y() < icons[i].y + 48) {

        serial_log("GUI: Desktop Icon Clicked: ");
        serial_log(icons[i].name);

        // Double click launches
        if (Input::is_double_click()) {
          serial_log("GUI: Desktop Icon Double-Click - Launching...");
          if (name_match(icons[i].name, "Explorer")) {
            explorer_load_directory("/");
            launch_explorer();
          } else if (name_match(icons[i].name, "Terminal")) {
            launch_terminal();
          } else if (name_match(icons[i].name, "Monitor")) {
            launch_sysmonitor();
          } else if (name_match(icons[i].name, "Calculator")) {
            launch_calculator();
          } else if (name_match(icons[i].name, "Notepad")) {
            launch_notepad();
          } else {
            char full[256];
            strcpy(full, "/home/user/Desktop/");
            strcat(full, icons[i].name);

            if (sys_is_dir(full)) {
              explorer_load_directory(full);
              launch_explorer();
            } else {
              sys_spawn(full, nullptr);
            }
          }
          dragging_icon_index = -1;
        } else {
          serial_log("GUI: -> Selection / Drag Start");
          for (int k = 0; k < icon_count; k++)
            icons[k].selected = false;
          icons[i].selected = true;

          dragging_icon_index = i;
          prev_x = icons[i].x;
          prev_y = icons[i].y;
          drag_offset_x = Input::x() - icons[i].x;
          drag_offset_y = Input::y() - icons[i].y;
        }
        hit = true;
        break;
      }
    }
    if (!hit) {
      // Clicked on desktop empty area, deselect all
      for (int i = 0; i < icon_count; i++)
        icons[i].selected = false;
      dragging_icon_index = -1;
    }
  }

  if (dragging_icon_index != -1) {
    if (Input::held()) {
      icons[dragging_icon_index].x = Input::x() - drag_offset_x;
      icons[dragging_icon_index].y = Input::y() - drag_offset_y;

      // Keep inside screen
      if (icons[dragging_icon_index].x < 10)
        icons[dragging_icon_index].x = 10;
      if (icons[dragging_icon_index].y < 10)
        icons[dragging_icon_index].y = 10;
      if (icons[dragging_icon_index].x > FB::W - 60)
        icons[dragging_icon_index].x = FB::W - 60;
      if (icons[dragging_icon_index].y > FB::H - 100)
        icons[dragging_icon_index].y = FB::H - 100;
    } else {
      // RELEASE: Snap to grid and check if occupied
      int gx = (icons[dragging_icon_index].x + 48) / 96;
      int gy = (icons[dragging_icon_index].y + 55) / 110;
      int nx = 32 + gx * 96;
      int ny = 32 + gy * 110;

      bool occupied = false;
      for (int i = 0; i < icon_count; i++) {
        if (i == dragging_icon_index)
          continue;
        if (icons[i].x == nx && icons[i].y == ny) {
          occupied = true;
          break;
        }
      }

      if (!occupied) {
        icons[dragging_icon_index].x = nx;
        icons[dragging_icon_index].y = ny;
      } else {
        // Collision! Move back
        icons[dragging_icon_index].x = prev_x;
        icons[dragging_icon_index].y = prev_y;
      }
      dragging_icon_index = -1;
    }
  }
}

} // namespace DesktopSystem

int explorer_hit_test(Window *w, int mx, int my) {
  int sidebar_w = 160;
  int header_h = 40;
  int start_y = w->y + header_h + 20; // header_h + padding

  int visible_w = w->w - sidebar_w;
  int icon_cols = (visible_w - 40) / 90;
  if (icon_cols < 1)
    icon_cols = 1;

  for (int i = 0; i < item_count; i++) {
    int col = i % icon_cols;
    int row = i / icon_cols;
    int x = w->x + sidebar_w + 20 + col * 90;
    int y = start_y + row * ITEM_H - scroll_y;

    if (mx >= x && mx < x + 72 && my >= y && my < y + 72)
      return i;
  }
  return -1;
}

void load_dir() { explorer_load_directory(cwd); }

void open_item(const char *path) {
  // Integrated into explorer_contract, but kept for legacy calls
  if (sys_is_dir(path)) {
    explorer_load_directory(path);
  } else {
    sys_spawn(path, nullptr);
  }
}

void explorer_draw(Window *w) {
  auto font = FontSystem::font_load("default", 8);
  int sidebar_w = 160;
  int header_h = 40; // Reduced header (no tabs)

  // 1. Window Background
  FB::rect(w->x, w->y, w->w, w->h, 0x1A1025);

  // 2. Toolbar (Nav + Actions)
  int toolbar_y = w->y; // Starts at top now
  FB::rect(w->x, toolbar_y, w->w, header_h, 0x201530);

  // Nav Buttons
  IconSystem::draw_icon(IconSystem::ICON_BACK, w->x + 12, toolbar_y + 12, 16,
                        0xFFFFFF);
  // Red Close button (matching image)
  FB::rect(w->x + 40, toolbar_y + 12, 16, 16, 0xFF4444);

  // Address Breadcrumb
  FB::rect(w->x + 70, toolbar_y + 8, w->w - 200, 24, 0x150E20);
  FontSystem::draw_text(font, w->x + 80, toolbar_y + 16, cwd, 0xE0E0E0);

  // Sidebar (Left)
  int content_y = w->y + header_h;
  int content_h = w->h - header_h; // No footer

  FB::rect(w->x, content_y, sidebar_w, content_h, 0x180E22);

  const char *sidebar_items[] = {"Home",      "Desktop",   "Pictures",
                                 "Documents", "Downloads", "Projects",
                                 "This PC",   "Network"};

  for (int i = 0; i < 8; i++) {
    int item_y = content_y + 10 + i * 32;
    bool active = false;
    if (strcmp(sidebar_items[i], "Home") == 0 && strcmp(cwd, "/home/user") == 0)
      active = true;
    if (strcmp(sidebar_items[i], "Desktop") == 0 &&
        strcmp(cwd, "/home/user/Desktop") == 0)
      active = true;

    if (active) {
      FB::rect(w->x + 8, item_y - 4, sidebar_w - 16, 28, 0x352050);
      FB::rect(w->x + 8, item_y + 4, 3, 12, 0xFF4FD8);
    }

    uint32_t icon_col = (i == 4) ? 0xFFA500 : 0xFF4FD8;
    IconSystem::draw_icon(IconSystem::ICON_FOLDER, w->x + 20, item_y, 16,
                          icon_col);

    FontSystem::draw_text(font, w->x + 44, item_y + 6, sidebar_items[i],
                          active ? 0xFFFFFF : 0xAAAAAA);
  }

  // 5. Main Content Area
  int main_x = w->x + sidebar_w;
  int main_w = w->w - sidebar_w;
  FB::rect(main_x, content_y, main_w, content_h, 0x150F20);

  hovered = explorer_hit_test(w, Input::x(), Input::y());

  int visible_w = w->w - sidebar_w;
  int visible_h = content_h;
  int content_x = w->x + sidebar_w;

  if (Input::x() >= main_x && Input::y() >= content_y &&
      Input::y() < content_y + content_h) {
    if (Input::held() && Input::y() < content_y + 50)
      scroll_y -= 5;
    if (Input::held() && Input::y() > content_y + visible_h - 50)
      scroll_y += 5;
  }
  if (scroll_y < 0)
    scroll_y = 0;

  // 5. Icons / Files
  int icon_cols = (visible_w - 40) / 90;
  if (icon_cols < 1)
    icon_cols = 1;
  int total_h = ((item_count + icon_cols - 1) / icon_cols) * ITEM_H + 40;

  for (int i = 0; i < item_count; i++) {
    int col = i % icon_cols;
    int row = i / icon_cols;
    int item_x = content_x + 20 + col * 90;
    int item_y = content_y + 20 + row * ITEM_H - scroll_y;

    if (item_y + ITEM_H < content_y || item_y > w->y + w->h)
      continue;

    if (i == hovered) {
      FB::blend_rect(item_x - 5, item_y - 5, 82, 82, 0xFFFFFF, 30);
    }
    if (i == selected) {
      FB::blend_rect(item_x - 5, item_y - 5, 82, 82, 0x3366FF, 60);
    }

    uint32_t accent = (items[i].type == 2) ? 0xFFD166 : 0x44AAFF;
    IconSystem::draw_icon(items[i].type == 2 ? IconSystem::ICON_FOLDER
                                             : IconSystem::ICON_FILE,
                          item_x + 16, item_y, 40, accent);

    FontSystem::draw_text(font, item_x, item_y + 50, items[i].name, 0xDDDDDD);
  }

  int max_scroll = total_h - visible_h + 20;
  if (max_scroll < 0)
    max_scroll = 0;
  if (scroll_y > max_scroll)
    scroll_y = max_scroll;

  if (total_h > visible_h) {
    Scroll s = {scroll_y, total_h};
    draw_scrollbar({w->x + w->w - 8, content_y, 8, content_h}, &s);
  }
}

bool explorer_click(Window *w, int rx, int ry) {
  int sidebar_w = 160;
  int header_h = 40;
  int footer_h = 0; // No footer

  // 1. Sidebar Click
  if (rx < sidebar_w && ry >= header_h && ry < w->h) {
    int item_idx = (ry - header_h - 10) / 32;
    const char *new_path = nullptr;

    if (item_idx == 0)
      new_path = "/home/user";
    else if (item_idx == 1)
      new_path = "/home/user/Desktop";
    else if (item_idx == 2)
      new_path = "/home/user/Pictures";
    else if (item_idx == 3)
      new_path = "/home/user/Documents";
    else if (item_idx == 4)
      new_path = "/home/user/Downloads";
    else if (item_idx == 5)
      new_path = "/home/user/Projects";
    else if (item_idx == 6)
      new_path = "/";
    else if (item_idx == 7)
      new_path = "/dev"; // Network placeholder -> dev

    if (new_path && item_idx >= 0 && item_idx <= 7) {
      strcpy(cwd, new_path);
      load_dir();
      selected = -1;
      return true;
    }
    return false;
  }

  // 2. Toolbar / Back Button
  if (ry > 0 && ry < header_h) {
    // Back button location: x=12, y=toolbar_y+8 (32+8=40).
    // Correcting hit test relative to window `rx` `ry`.
    // Toolbar starts at y=32. Icons are at y=40, size 16.
    if (rx >= 12 && rx <= 28) { // Back
      // Parent dir logic
      int len = 0;
      while (cwd[len])
        len++;
      for (int i = len - 1; i > 0; i--) {
        if (cwd[i] == '/') {
          cwd[i] = 0;
          break;
        }
      }
      if (cwd[0] == 0) {
        cwd[0] = '/';
        cwd[1] = 0;
      }
      load_dir();
      selected = -1;
      return true;
    }
  }

  // 3. Main Content Area
  if (rx >= sidebar_w && ry >= header_h && ry < w->h - footer_h) {
    if (hovered >= 0 && hovered < item_count) {
      bool dbl = Input::is_double_click();
      if (hovered == selected && dbl) {
        explorer_double_click(hovered);
        selected = -1;
      } else {
        selected = hovered;
        serial_log("GUI: Explorer SELECTED: ");
        serial_log(items[selected].name);
      }
      return true;
    }
    // Click on background -> Deselect
    selected = -1;
    return true;
  }

  return false;
}

void explorer_key(Window *w, int key, int state) {
  if (state == 0)
    return;

  if (key == 0x48) { // UP
    if (selected >= 5)
      selected -= 5;
  } else if (key == 0x50) { // DOWN
    if (selected < item_count - 5)
      selected += 5;
  } else if (key == 0x4B) { // LEFT
    if (selected > 0)
      selected--;
  } else if (key == 0x4D) { // RIGHT
    if (selected < item_count - 1)
      selected++;
  } else if (key == 0x0E) { // Backspace
    explorer_click(w, w->w - 32, 12);
  } else if (key == 0x01) { // ESC
    selected = -1;
  } else if (key == 0x1C) { // Enter
    if (selected >= 0 && selected < item_count) {
      explorer_double_click(selected);
      selected = -1;
    }
  }
}

void on_right_click(int x, int y) {
  ContextTarget target = TARGET_DESKTOP;
  static char full_path[256];
  full_path[0] = 0;

  // Check if we hit a window (top to bottom)
  for (int i = win_count[current_desktop] - 1; i >= 0; i--) {
    Window *w = &windows[current_desktop][i];
    if (!w->alive || w->fade.value < 0.5f)
      continue;

    Rect win_rect = {w->x, w->y, w->w, w->h};
    if (Input::inside_rect(win_rect)) {
      target = TARGET_EMPTY;
      const char *path = "/";

      if (w->draw == (DrawFn)explorer_draw) {
        int idx = explorer_hit_test(w, x, y);
        if (idx >= 0 && idx < item_count) {
          target = (items[idx].type == 2) ? TARGET_FOLDER : TARGET_FILE;
          path = items[idx].full_path;
        }
      }

      show_context_menu(x, y, target, path, &w->context_provider);
      return;
    }
  }

  // Check desktop icons
  for (int i = 0; i < DesktopSystem::icon_count; i++) {
    Rect r = {DesktopSystem::icons[i].x, DesktopSystem::icons[i].y, 48, 48};
    if (Input::inside_rect(r)) {
      // Build full path for desktop icons
      strcpy(full_path, "/home/user/Desktop/");
      strcat(full_path, DesktopSystem::icons[i].name);
      DesktopSystem::select_path(full_path);
      show_context_menu(x, y, TARGET_FILE, full_path);
      return;
    }
  }

  // Desktop itself
  show_context_menu(x, y, TARGET_DESKTOP, "/home/user/Desktop");
}

/* =========================================================
   TASKBAR
   ========================================================= */
void draw_taskbar() {
  FB::rect(0, FB::H - 32, FB::W, 32, 0x000000);
  auto font = FontSystem::font_load("default", 8);
  FontSystem::draw_text(font, 8, FB::H - 20, "MENU", 0xFFFFFF);

  int button_count = 0;
  for (int i = 0; i < win_count[current_desktop]; i++) {
    if (windows[current_desktop][i].alive) {
      uint32_t c = windows[current_desktop][i].focused ? 0x3A7AFE : 0x222222;
      FB::rect(80 + button_count * 110, FB::H - 32, 105, 32, c);

      // Truncate title if too long
      char title[16];
      int len = 0;
      while (windows[current_desktop][i].title[len] && len < 15) {
        title[len] = windows[current_desktop][i].title[len];
        len++;
      }
      title[len] = 0;

      FontSystem::draw_text(font, 84 + button_count * 110, FB::H - 20, title,
                            0xFFFFFF);
      button_count++;
    }
  }

  // Desktop indicators
  for (int i = 0; i < MAX_DESKTOPS; i++) {
    uint32_t c = (i == current_desktop) ? 0xFFFFFF : 0x555555;
    FB::rect(FB::W - 80 + i * 20, FB::H - 20, 12, 12, c);
  }
}

void handle_taskbar_click() {
  if (Input::clicked() && Input::y() > FB::H - 32) {
    int button_count = 0;
    for (int i = 0; i < win_count[current_desktop]; i++) {
      if (windows[current_desktop][i].alive) {
        if (Input::x() >= 80 + button_count * 110 &&
            Input::x() < 185 + button_count * 110) {
          focus_window(i);
          return;
        }
        button_count++;
      }
    }
    // Desktop switching
    if (Input::x() > FB::W - 80) {
      int d = (Input::x() - (FB::W - 80)) / 20;
      switch_desktop(d);
    }
  }
}

/* =========================================================
   CURSOR
   ========================================================= */
void draw_cursor(int x, int y) {
  // Solid triangle cursor
  for (int i = 0; i < 12; i++) {
    for (int j = 0; j <= i; j++) {
      FB::put(x + j, y + i, 0xFFFFFF);
    }
  }
}

/* =========================================================
   MAIN LOOP
   ========================================================= */
int explorer_context_items(ExtendedAction *out, int max, const char *path,
                           void *ctx) {
  (void)ctx;
  (void)max;
  (void)path;
  out[0] = {"Properties", nullptr};
  out[1] = {"Open in Terminal", nullptr};
  return 2;
}

void launch_explorer() {
  serial_log("GUI: Launching File Explorer...");
  explorer_init();
  Window *w = create_window(200, 120, 520, 360, "File Explorer",
                            &ThemeEngine::explorer_theme, explorer_draw,
                            explorer_click);
  w->wants_keyboard = true;
  w->key = explorer_key;
  w->context_provider = {nullptr, explorer_context_items};
}

/* =========================================================
   TERMINAL EMULATOR (Kernel-side GUI Terminal)
   ========================================================= */
namespace TerminalSystem {

#define TERM_COLS 60
#define TERM_ROWS 20
#define TERM_HISTORY 100
#define OUTPUT_BUF_SIZE 2048

static char term_buffer[TERM_ROWS][TERM_COLS + 1];
static int term_cursor_x = 0;
static int term_cursor_y = 0;
static char term_input[256];
static int term_input_len = 0;
static char term_cwd[256] = "/";

// Output capture buffer for spawned programs
static char program_output[OUTPUT_BUF_SIZE];
static volatile int output_head = 0;
static volatile int output_tail = 0;
static bool terminal_active = false;

static ThemeEngine::Theme term_theme = {0x1A1A2E, 0x00FF00, 0x00AA00, 0x333355};

// Called by kernel to add output to the capture buffer
void capture_output(const char *str) {
  if (!terminal_active)
    return;
  while (*str) {
    int next = (output_head + 1) % OUTPUT_BUF_SIZE;
    if (next != output_tail) {
      program_output[output_head] = *str++;
      output_head = next;
    } else {
      break; // Buffer full
    }
  }
}

// Read captured output into terminal buffer
void poll_output() {
  while (output_tail != output_head) {
    char c = program_output[output_tail];
    output_tail = (output_tail + 1) % OUTPUT_BUF_SIZE;
    // Display in terminal
    if (c == '\n') {
      term_cursor_x = 0;
      term_cursor_y++;
      if (term_cursor_y >= TERM_ROWS) {
        // Scroll
        for (int y = 0; y < TERM_ROWS - 1; y++) {
          for (int x = 0; x < TERM_COLS; x++) {
            term_buffer[y][x] = term_buffer[y + 1][x];
          }
        }
        for (int x = 0; x < TERM_COLS; x++) {
          term_buffer[TERM_ROWS - 1][x] = ' ';
        }
        term_cursor_y = TERM_ROWS - 1;
      }
    } else if (c >= 32 && c < 127) {
      if (term_cursor_x < TERM_COLS) {
        term_buffer[term_cursor_y][term_cursor_x++] = c;
      }
    }
  }
}

} // namespace TerminalSystem

// Global function for kernel to call
extern "C" void gui_terminal_output(const char *str) {
  TerminalSystem::capture_output(str);
}

namespace TerminalSystem {

void term_clear() {
  for (int y = 0; y < TERM_ROWS; y++) {
    for (int x = 0; x < TERM_COLS; x++) {
      term_buffer[y][x] = ' ';
    }
    term_buffer[y][TERM_COLS] = 0;
  }
  term_cursor_x = 0;
  term_cursor_y = 0;
}

void term_scroll() {
  for (int y = 0; y < TERM_ROWS - 1; y++) {
    for (int x = 0; x < TERM_COLS; x++) {
      term_buffer[y][x] = term_buffer[y + 1][x];
    }
  }
  for (int x = 0; x < TERM_COLS; x++) {
    term_buffer[TERM_ROWS - 1][x] = ' ';
  }
}

void term_putchar(char c) {
  if (c == '\n') {
    term_cursor_x = 0;
    term_cursor_y++;
    if (term_cursor_y >= TERM_ROWS) {
      term_scroll();
      term_cursor_y = TERM_ROWS - 1;
    }
  } else if (c == '\r') {
    term_cursor_x = 0;
  } else if (c == '\b') {
    if (term_cursor_x > 0)
      term_cursor_x--;
  } else {
    if (term_cursor_x < TERM_COLS) {
      term_buffer[term_cursor_y][term_cursor_x++] = c;
    }
    if (term_cursor_x >= TERM_COLS) {
      term_cursor_x = 0;
      term_cursor_y++;
      if (term_cursor_y >= TERM_ROWS) {
        term_scroll();
        term_cursor_y = TERM_ROWS - 1;
      }
    }
  }
}

void term_print(const char *s) {
  while (*s)
    term_putchar(*s++);
}

void term_prompt() {
  term_print("\n[");
  term_print(term_cwd);
  term_print("]$ ");
}

void term_exec(const char *cmd) {
  // Built-in commands
  if (strcmp(cmd, "clear") == 0) {
    term_clear();
    return;
  }
  if (strcmp(cmd, "help") == 0) {
    term_print("\nRetro-OS Terminal v2.0");
    term_print("\nBuilt-in commands:");
    term_print("\n  ls        - list directory");
    term_print("\n  cd <path> - change directory");
    term_print("\n  pwd       - print working directory");
    term_print("\n  cat <file>- display file contents");
    term_print("\n  mkdir <n> - create directory");
    term_print("\n  echo <t>  - print text");
    term_print("\n  ping      - test network (ICMP)");
    term_print("\n  tcptest   - test TCP connection");
    term_print("\n  run <elf> - run program");
    term_print("\n  clear     - clear screen");
    term_print("\n  help      - this message");
    return;
  }
  if (strcmp(cmd, "pwd") == 0) {
    term_print("\n");
    term_print(term_cwd);
    return;
  }
  if (strncmp(cmd, "cd ", 3) == 0) {
    const char *path = cmd + 3;
    if (path[0] == '/') {
      strcpy(term_cwd, path);
    } else if (strcmp(path, "..") == 0) {
      int len = strlen(term_cwd);
      while (len > 1 && term_cwd[len - 1] != '/')
        len--;
      if (len > 1)
        len--;
      term_cwd[len] = 0;
      if (term_cwd[0] == 0)
        strcpy(term_cwd, "/");
    } else {
      if (strcmp(term_cwd, "/") != 0)
        strcat(term_cwd, "/");
      strcat(term_cwd, path);
    }
    return;
  }
  if (strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0) {
    const char *path = (cmd[2] == ' ') ? cmd + 3 : term_cwd;
    int fd = sys_open(path, 0);
    if (fd >= 1000) {
      for (int i = 0; i < 32; i++) {
        struct {
          char name[64];
          uint32_t type;
        } tmp;
        if (sys_readdir(fd, i, &tmp) != 0)
          break;
        if (tmp.name[0] == 0)
          break;
        term_print("\n  ");
        term_print(tmp.name);
        if (tmp.type == 2)
          term_print("/");
      }
      sys_close(fd);
    } else {
      term_print("\nError: cannot open directory");
    }
    return;
  }
  if (strncmp(cmd, "cat ", 4) == 0) {
    const char *filename = cmd + 4;
    char fullpath[256];
    if (filename[0] == '/') {
      strcpy(fullpath, filename);
    } else {
      strcpy(fullpath, term_cwd);
      if (strcmp(term_cwd, "/") != 0)
        strcat(fullpath, "/");
      strcat(fullpath, filename);
    }
    // Open file and read
    vfs_node_t *node = vfs_resolve_path(fullpath);
    if (node && (node->flags & VFS_FILE)) {
      char buf[512];
      uint32_t sz = node->read ? node->read(node, 0, 511, (uint8_t *)buf) : 0;
      buf[sz] = 0;
      term_print("\n");
      term_print(buf);
    } else {
      term_print("\nError: cannot read file ");
      term_print(fullpath);
    }
    return;
  }
  if (strncmp(cmd, "mkdir ", 6) == 0) {
    const char *name = cmd + 6;
    char fullpath[256];
    if (name[0] == '/') {
      strcpy(fullpath, name);
    } else {
      strcpy(fullpath, term_cwd);
      if (strcmp(term_cwd, "/") != 0)
        strcat(fullpath, "/");
      strcat(fullpath, name);
    }
    if (vfs_create(fullpath, VFS_DIRECTORY)) {
      term_print("\nCreated: ");
      term_print(fullpath);
    } else {
      term_print("\nError: cannot create directory");
    }
    return;
  }
  if (strncmp(cmd, "echo ", 5) == 0) {
    term_print("\n");
    term_print(cmd + 5);
    return;
  }
  if (strncmp(cmd, "run ", 4) == 0) {
    const char *prog = cmd + 4;
    char path[64];
    strcpy(path, "/");
    strcat(path, prog);
    // Convert to uppercase for FAT16
    for (int i = 1; path[i]; i++) {
      if (path[i] >= 'a' && path[i] <= 'z')
        path[i] -= 32;
    }
    int ret = sys_spawn(path, nullptr);
    if (ret >= 0) {
      term_print("\nStarted: ");
      term_print(path);
    } else {
      term_print("\nError: cannot run ");
      term_print(path);
    }
    return;
  }
  if (strcmp(cmd, "ping") == 0) {
    term_print("\nPING: Sending test ICMP request to 192.168.0.1...\n");
    term_print("Check serial log for reply!");
    sys_spawn("/PING.ELF", nullptr);
    return;
  }
  if (strcmp(cmd, "tcptest") == 0) {
    term_print("\nTCP: Testing connection to example.com:80...\n");
    term_print("Check serial log for TCP handshake!");
    sys_spawn("/TCPTEST.ELF", nullptr);
    return;
  }
  // Try to run as program if ends in .elf
  int cmdlen = strlen(cmd);
  if (cmdlen > 4 && strcmp(cmd + cmdlen - 4, ".elf") == 0) {
    char path[64];
    strcpy(path, "/");
    strcat(path, cmd);
    for (int i = 1; path[i]; i++) {
      if (path[i] >= 'a' && path[i] <= 'z')
        path[i] -= 32;
    }
    int ret = sys_spawn(path, nullptr);
    if (ret >= 0) {
      term_print("\nStarted: ");
      term_print(path);
    } else {
      term_print("\nError: not found ");
      term_print(path);
    }
    return;
  }
  if (cmd[0] != 0) {
    term_print("\nUnknown command: ");
    term_print(cmd);
    term_print("\nType 'help' for available commands.");
  }
}

void term_init() {
  term_clear();
  terminal_active = true;
  output_head = 0;
  output_tail = 0;
  term_print("Retro-OS Terminal v2.0");
  term_print("\nType 'help' for commands.\n");
  term_prompt();
  term_input_len = 0;
  term_input[0] = 0;
}

void terminal_draw(Window *w) {
  // Poll for program output first
  poll_output();

  int px = w->x;
  int py = w->y;

  // Background
  FB::rect(px, py, w->w, w->h, term_theme.bg);

  // Draw text buffer
  auto font = FontSystem::font_load("default", 8);
  for (int y = 0; y < TERM_ROWS && y * 12 + 30 < w->h; y++) {
    FontSystem::draw_text(font, px + 8, py + 30 + y * 12, term_buffer[y],
                          term_theme.fg);
  }

  // Draw input line at cursor position
  int input_y = py + 30 + term_cursor_y * 12;

  // Blinking cursor
  static int blink = 0;
  blink++;
  if ((blink / 15) % 2 == 0) {
    int cursor_px = px + 8 + term_cursor_x * 8;
    FB::rect(cursor_px, input_y, 8, 10, term_theme.accent);
  }
}

bool terminal_click(Window *w, int x, int y) {
  (void)w;
  (void)x;
  (void)y;
  return true; // Consume click
}

void terminal_key(Window *w, int key, int state) {
  (void)w;
  if (state == 0)
    return; // Key up

  if (key == 0x0D || key == '\n') { // Enter
    term_input[term_input_len] = 0;
    term_exec(term_input);
    term_prompt();
    term_input_len = 0;
    term_input[0] = 0;
  } else if (key == 0x08 || key == '\b') { // Backspace
    if (term_input_len > 0) {
      term_input_len--;
      term_input[term_input_len] = 0;
      if (term_cursor_x > 0) {
        term_cursor_x--;
        term_buffer[term_cursor_y][term_cursor_x] = ' ';
      }
    }
  } else if (key >= 32 && key < 127) { // Printable
    if (term_input_len < 250) {
      term_input[term_input_len++] = (char)key;
      term_putchar((char)key);
    }
  }
}

} // namespace TerminalSystem

void launch_terminal() {
  serial_log("GUI: Launching Terminal...");
  TerminalSystem::term_init();
  Window *w = create_window(
      150, 100, 500, 300, "Terminal", &TerminalSystem::term_theme,
      TerminalSystem::terminal_draw, TerminalSystem::terminal_click);
  w->wants_keyboard = true;
  w->key = TerminalSystem::terminal_key;
}

/* =========================================================
   SYSTEM MONITOR (Shows memory, processes, system info)
   ========================================================= */
extern "C" {
extern uint32_t tick;
}

namespace SysMonitor {

static ThemeEngine::Theme sysmon_theme = {0x1A2A3A, 0xFFFFFF, 0x4FC3F7,
                                          0x2A3A4A};

// Helper to draw a progress bar
void draw_progress_bar(int x, int y, int w, int h, int percent, uint32_t fg,
                       uint32_t bg) {
  FB::rect(x, y, w, h, bg);
  int filled = (w * percent) / 100;
  if (filled > 0) {
    FB::rect(x, y, filled, h, fg);
  }
  // Border
  for (int i = 0; i < w; i++) {
    FB::put(x + i, y, 0x555555);
    FB::put(x + i, y + h - 1, 0x555555);
  }
  for (int i = 0; i < h; i++) {
    FB::put(x, y + i, 0x555555);
    FB::put(x + w - 1, y + i, 0x555555);
  }
}

// Helper to format number with commas (simplified)
void format_kb(char *out, uint32_t kb) {
  if (kb >= 1024) {
    uint32_t mb = kb / 1024;
    int rem = (kb % 1024) / 103; // One decimal place
    // Simple sprintf replacement
    char *p = out;
    if (mb >= 100) {
      *p++ = '0' + (mb / 100) % 10;
    }
    if (mb >= 10) {
      *p++ = '0' + (mb / 10) % 10;
    }
    *p++ = '0' + mb % 10;
    *p++ = '.';
    *p++ = '0' + rem;
    *p++ = ' ';
    *p++ = 'M';
    *p++ = 'B';
    *p = 0;
  } else {
    char *p = out;
    if (kb >= 100) {
      *p++ = '0' + (kb / 100) % 10;
    }
    if (kb >= 10) {
      *p++ = '0' + (kb / 10) % 10;
    }
    *p++ = '0' + kb % 10;
    *p++ = ' ';
    *p++ = 'K';
    *p++ = 'B';
    *p = 0;
  }
}

void sysmon_draw(Window *w) {
  int px = w->x;
  int py = w->y;

  // Background with gradient effect
  FB::rect(px, py, w->w, w->h, sysmon_theme.bg);

  auto font = FontSystem::font_load("default", 8);
  int line_y = py + 35;
  int label_x = px + 15;
  int value_x = px + 150;

  // Title section
  FontSystem::draw_text(font, px + 15, py + 32, "SYSTEM MONITOR", 0x4FC3F7);

  // Separator line
  for (int i = 10; i < w->w - 10; i++) {
    FB::put(px + i, py + 48, 0x3A4A5A);
  }

  line_y = py + 60;

  // Memory Section
  FontSystem::draw_text(font, label_x, line_y, "=== MEMORY ===", 0x81D4FA);
  line_y += 18;

  uint32_t total_blocks = pmm_get_block_count();
  uint32_t free_blocks = pmm_get_free_block_count();
  uint32_t used_blocks = total_blocks - free_blocks;
  uint32_t total_kb = total_blocks * 4; // 4KB per block
  uint32_t used_kb = used_blocks * 4;
  uint32_t free_kb = free_blocks * 4;
  int mem_percent = (used_blocks * 100) / total_blocks;

  char buf[64];

  FontSystem::draw_text(font, label_x, line_y, "Total Memory:", 0xCCCCCC);
  format_kb(buf, total_kb);
  FontSystem::draw_text(font, value_x, line_y, buf, 0xFFFFFF);
  line_y += 14;

  FontSystem::draw_text(font, label_x, line_y, "Used Memory:", 0xCCCCCC);
  format_kb(buf, used_kb);
  FontSystem::draw_text(font, value_x, line_y, buf, 0xFF8A65);
  line_y += 14;

  FontSystem::draw_text(font, label_x, line_y, "Free Memory:", 0xCCCCCC);
  format_kb(buf, free_kb);
  FontSystem::draw_text(font, value_x, line_y, buf, 0x81C784);
  line_y += 18;

  // Memory progress bar
  FontSystem::draw_text(font, label_x, line_y, "Usage:", 0xCCCCCC);
  draw_progress_bar(value_x, line_y, 120, 12, mem_percent,
                    mem_percent > 80 ? 0xFF5252
                                     : (mem_percent > 50 ? 0xFFB74D : 0x4FC3F7),
                    0x1A2A3A);
  // Draw percentage text
  char pct[8];
  if (mem_percent >= 10)
    pct[0] = '0' + (mem_percent / 10) % 10;
  else
    pct[0] = ' ';
  pct[1] = '0' + mem_percent % 10;
  pct[2] = '%';
  pct[3] = 0;
  FontSystem::draw_text(font, value_x + 130, line_y, pct, 0xFFFFFF);
  line_y += 25;

  // System Section
  FontSystem::draw_text(font, label_x, line_y, "=== SYSTEM ===", 0x81D4FA);
  line_y += 18;

  // Uptime
  uint32_t uptime_sec = tick / 1000;
  uint32_t hours = uptime_sec / 3600;
  uint32_t mins = (uptime_sec % 3600) / 60;
  uint32_t secs = uptime_sec % 60;

  FontSystem::draw_text(font, label_x, line_y, "Uptime:", 0xCCCCCC);
  char uptime_buf[32];
  char *p = uptime_buf;
  *p++ = '0' + hours / 10;
  *p++ = '0' + hours % 10;
  *p++ = ':';
  *p++ = '0' + mins / 10;
  *p++ = '0' + mins % 10;
  *p++ = ':';
  *p++ = '0' + secs / 10;
  *p++ = '0' + secs % 10;
  *p = 0;
  FontSystem::draw_text(font, value_x, line_y, uptime_buf, 0xFFFFFF);
  line_y += 14;

  FontSystem::draw_text(font, label_x, line_y, "Architecture:", 0xCCCCCC);
  FontSystem::draw_text(font, value_x, line_y, "x86 (i686)", 0xFFFFFF);
  line_y += 14;

  FontSystem::draw_text(font, label_x, line_y, "OS Version:", 0xCCCCCC);
  FontSystem::draw_text(font, value_x, line_y, "Retro-OS 1.0", 0xFFFFFF);
  line_y += 14;

  FontSystem::draw_text(font, label_x, line_y, "Kernel:", 0xCCCCCC);
  FontSystem::draw_text(font, value_x, line_y, "Higher-Half", 0xFFFFFF);
}

bool sysmon_click(Window *w, int x, int y) {
  (void)w;
  (void)x;
  (void)y;
  return true;
}

} // namespace SysMonitor

void launch_sysmonitor() {
  serial_log("GUI: Launching System Monitor...");
  create_window(250, 150, 320, 280, "System Monitor", &SysMonitor::sysmon_theme,
                SysMonitor::sysmon_draw, SysMonitor::sysmon_click);
}

/* =========================================================
   CALCULATOR APP (Kernel Side)
   ========================================================= */
namespace Calculator {
static ThemeEngine::Theme calc_theme = {0x1E1E2E, 0xFFFFFF, 0xFF9800, 0x2A2A40};

struct State {
  char display[32];
  double last_value;
  char pending_op;
  bool new_entry;
};
static State state = {"0", 0.0, 0, true};

double string_to_double(const char *s) {
  double res = 0;
  double factor = 1;
  bool decimal = false;
  int i = 0;
  if (s[0] == ' ')
    i++; // skip leading space if any
  for (; s[i]; i++) {
    if (s[i] == '.') {
      decimal = true;
      continue;
    }
    if (!decimal) {
      res = res * 10 + (s[i] - '0');
    } else {
      factor /= 10;
      res += (s[i] - '0') * factor;
    }
  }
  return res;
}

void double_to_string(double val, char *out) {
  if (val == 0) {
    strcpy(out, "0");
    return;
  }
  int whole = (int)val;
  int frac = (int)((val - whole) * 100);
  if (frac < 0)
    frac = -frac;

  char temp[32];
  int i = 0;
  int v = (whole < 0) ? -whole : whole;
  if (v == 0)
    temp[i++] = '0';
  while (v > 0) {
    temp[i++] = '0' + (v % 10);
    v /= 10;
  }

  int k = 0;
  if (whole < 0)
    out[k++] = '-';
  for (int j = i - 1; j >= 0; j--)
    out[k++] = temp[j];

  if (frac > 0) {
    out[k++] = '.';
    out[k++] = '0' + (frac / 10) % 10;
    out[k++] = '0' + (frac % 10);
    while (k > 0 && out[k - 1] == '0')
      k--;
    if (out[k - 1] == '.')
      k--;
  }
  out[k] = 0;
}

void perform_op() {
  double current = string_to_double(state.display);
  if (state.pending_op == '+')
    state.last_value += current;
  else if (state.pending_op == '-')
    state.last_value -= current;
  else if (state.pending_op == '*')
    state.last_value *= current;
  else if (state.pending_op == '/') {
    if (current != 0)
      state.last_value /= current;
  } else
    state.last_value = current;

  double_to_string(state.last_value, state.display);
}

void calc_draw(Window *w) {
  int px = w->x, py = w->y;
  FB::rect(px, py, w->w, w->h, calc_theme.bg);

  auto font = FontSystem::font_load("default", 16);
  auto small_font = FontSystem::font_load("default", 8);

  // Display area
  FB::rect(px + 10, py + 35, w->w - 20, 50, 0x2A2937);
  FB::rect(px + 10, py + 35, w->w - 20, 1, 0x333333);

  int text_w = FontSystem::text_width(font, state.display);
  FontSystem::draw_text(font, px + w->w - 30 - text_w, py + 52, state.display,
                        0x4FC3F7);

  const char *buttons[] = {"C", "<-", "/", "*", "7", "8", "9", "-", "4", "5",
                           "6", "+",  "1", "2", "3", "=", "0", ".", "=", "="};

  int start_y = py + 100;
  int grid_w = w->w - 20;
  int grid_h = w->h - 110;
  int btn_w = (grid_w - 15) / 4;
  int btn_h = (grid_h - 20) / 5;

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 4; col++) {
      int idx = row * 4 + col;
      if (idx >= 20)
        break;

      // Special handling for 0 (size 2x1)
      if (row == 4 && col == 1)
        continue;

      int bw = btn_w;
      int bh = btn_h;
      if (row == 4 && col == 0)
        bw = btn_w * 2 + 5;

      int bx = px + 10 + col * (btn_w + 5);
      int by = start_y + row * (btn_h + 5);

      uint32_t color = 0x2A3A4A;
      if (col == 3 || (row == 0 && col > 1))
        color = 0xFF9800; // Operations
      if (row == 0 && col < 2)
        color = 0x546E7A; // Special

      FB::rect(bx, by, bw, bh, color);
      FB::rect(bx, by, bw, 1, color + 0x111111);

      int tw = FontSystem::text_width(small_font, buttons[idx]);
      FontSystem::draw_text(small_font, bx + (bw - tw) / 2, by + (bh - 8) / 2,
                            buttons[idx], 0xFFFFFF);
    }
  }
}

bool calc_click(Window *w, int x, int y) {
  if (y < 100)
    return true;

  int grid_w = w->w - 20;
  int grid_h = w->h - 110;
  int btn_w = (grid_w - 15) / 4;
  int btn_h = (grid_h - 20) / 5;

  int col = (x - 10) / (btn_w + 5);
  int row = (y - 100) / (btn_h + 5);

  if (col < 0 || col >= 4 || row < 0 || row >= 5)
    return true;

  const char *buttons[] = {"C", "<-", "/", "*", "7", "8", "9", "-", "4", "5",
                           "6", "+",  "1", "2", "3", "=", "0", "0", ".", "="};
  int idx = row * 4 + col;
  const char *btn = buttons[idx];

  if (btn[0] >= '0' && btn[0] <= '9') {
    if (state.new_entry) {
      state.display[0] = btn[0];
      state.display[1] = 0;
      state.new_entry = false;
    } else {
      int len = strlen(state.display);
      if (len < 15) {
        state.display[len] = btn[0];
        state.display[len + 1] = 0;
      }
    }
  } else if (btn[0] == '.') {
    bool has_dot = false;
    for (int i = 0; state.display[i]; i++)
      if (state.display[i] == '.')
        has_dot = true;
    if (!has_dot) {
      int len = strlen(state.display);
      state.display[len++] = '.';
      state.display[len] = 0;
      state.new_entry = false;
    }
  } else if (btn[0] == 'C' && btn[1] == 0) {
    strcpy(state.display, "0");
    state.last_value = 0;
    state.pending_op = 0;
    state.new_entry = true;
  } else if (btn[0] == '<') {
    int len = strlen(state.display);
    if (len > 1) {
      state.display[len - 1] = 0;
    } else {
      strcpy(state.display, "0");
      state.new_entry = true;
    }
  } else if (btn[0] == '+' || btn[0] == '-' || btn[0] == '*' || btn[0] == '/') {
    perform_op();
    state.pending_op = btn[0];
    state.new_entry = true;
  } else if (btn[0] == '=') {
    perform_op();
    state.pending_op = 0;
    state.new_entry = true;
  }

  return true;
}

void calc_key(Window *w, int key, int state_key) {
  (void)w;
  if (state_key == 0)
    return; // Ignore key releases

  if (key >= '0' && key <= '9') {
    if (state.new_entry) {
      state.display[0] = (char)key;
      state.display[1] = 0;
      state.new_entry = false;
    } else {
      int len = strlen(state.display);
      if (len < 15) {
        state.display[len] = (char)key;
        state.display[len + 1] = 0;
      }
    }
  } else if (key == '.') {
    bool has_dot = false;
    for (int i = 0; state.display[i]; i++)
      if (state.display[i] == '.')
        has_dot = true;
    if (!has_dot) {
      if (state.new_entry) {
        strcpy(state.display, "0.");
        state.new_entry = false;
      } else {
        int len = strlen(state.display);
        if (len < 15) {
          state.display[len] = '.';
          state.display[len + 1] = 0;
        }
      }
    }
  } else if (key == 8) { // Backspace
    if (state.new_entry) {
      strcpy(state.display, "0");
    } else {
      int len = strlen(state.display);
      if (len > 1) {
        state.display[len - 1] = 0;
      } else {
        strcpy(state.display, "0");
        state.new_entry = true;
      }
    }
  } else if (key == 27) { // Escape -> Clear
    strcpy(state.display, "0");
    state.last_value = 0;
    state.pending_op = 0;
    state.new_entry = true;
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    perform_op();
    state.pending_op = (char)key;
    state.new_entry = true;
  } else if (key == 13 || key == '=') { // Enter or =
    perform_op();
    state.pending_op = 0;
    state.new_entry = true;
  }
}

} // namespace Calculator

void launch_calculator() {
  serial_log("GUI: Launching Calculator...");
  Window *w =
      create_window(350, 100, 220, 320, "Calculator", &Calculator::calc_theme,
                    Calculator::calc_draw, Calculator::calc_click);
  if (w) {
    w->wants_keyboard = true;
    w->key = Calculator::calc_key;
  }
}
// Launch Notepad
// Launch Notepad
void launch_notepad() {
  serial_log("GUI: Launching Notepad...");
  // Files are injected into root directory by inject_wallpaper.py
  // Case sensitivity or 8.3 format might be enforced by VFS
  int ret = sys_spawn("/NOTEPAD.ELF", nullptr);
  if (ret < 0) {
    serial_log("GUI: Failed to launch /NOTEPAD.ELF, trying lowercase...");
    sys_spawn("/notepad.elf", nullptr);
  }
}

void launch_browser() {
  serial_log("GUI: Launching Browser...");
  int ret = sys_spawn("/BROWSER.ELF", nullptr);
  if (ret < 0) {
      sys_spawn("/browser.elf", nullptr);
  }
}

// Include socket definitions
extern "C" {
#include "net.h"
#include "shm.h"
#include "socket.h"
}

// Kernel-internal helpers
int k_read(int fd, void *buf, int size) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -1;
  file_description_t *desc = current_process->fd_table[fd];
  return vfs_read(desc->node, desc->offset, (uint8_t *)buf, size);
}

int k_write(int fd, void *buf, int size) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -1;
  file_description_t *desc = current_process->fd_table[fd];
  return vfs_write(desc->node, desc->offset, (uint8_t *)buf, size);
}

// Window Server Implementation
namespace WindowServer {

#define WS_PORT "/tmp/ws.sock"
static int server_fd = -1;
static int client_fds[16]; // Max 16 clients for now
static int client_count = 0;

struct msg_gfx_create_window_t {
  int width;
  int height;
  char title[64];
};

struct msg_gfx_window_created_t {
  int window_id;
  int shm_id;
};

struct msg_gfx_invalidate_t {
  int window_id;
  int x, y, width, height;
};

struct msg_gfx_mouse_event_t {
  int window_id;
  int x, y;
  int buttons;
};

struct msg_gfx_key_event_t {
  int window_id;
  char key;
};

struct gfx_msg_t {
  uint32_t type;
  uint32_t size;
  union {
    msg_gfx_create_window_t create;
    msg_gfx_window_created_t created;
    msg_gfx_invalidate_t invalidate;
    msg_gfx_mouse_event_t mouse;
    msg_gfx_key_event_t key;
  } data;
} __attribute__((packed));

void ipc_window_key_callback(Window *w, int key, int scancode) {
  if (w->client_fd >= 0) {
    gfx_msg_t msg;
    msg.type = 5; // MSG_GFX_KEY_EVENT
    msg.data.key.window_id = w->id;
    msg.data.key.key = (char)key;
    k_write(w->client_fd, &msg, sizeof(msg));
  }
}

bool ipc_window_click_callback(Window *w, int x, int y) {
  if (w->client_fd >= 0) {
    gfx_msg_t msg;
    msg.type = 4; // MSG_GFX_MOUSE_EVENT
    msg.data.mouse.window_id = w->id;
    msg.data.mouse.x = x;
    msg.data.mouse.y = y;
    msg.data.mouse.buttons = 1; // Left click
    k_write(w->client_fd, &msg, sizeof(msg));
    return true;
  }
  return false;
}

// Helper to process a message blocking-ly (called once after accept to read
// Create Window)
void handle_client_setup(int fd) {
  gfx_msg_t msg;
  int n = k_read(fd, &msg, sizeof(gfx_msg_t));
  if (n > 0) {
    if (msg.type == 1) { // MSG_GFX_CREATE_WINDOW
      int w = msg.data.create.width;
      int h = msg.data.create.height;
      char *t = msg.data.create.title;

      // Create SHM
      uint32_t shm_size = w * h * 4;
      int shmid = sys_shmget(0, shm_size, 0); // alloc
      void *buf = (void *)sys_shmat(shmid);

      // Create Window
      Window *win =
          create_window(100, 100, w, h, t, &ThemeEngine::explorer_theme,
                        execute_ipc_draw, ipc_window_click_callback);
      if (win) {
        win->key = ipc_window_key_callback;
        win->wants_keyboard = true;
        win->client_fd = fd;
      }

      if (win && buf) {
        win->backbuffer = (uint32_t *)buf;
        // Store window ID and connection ID relation if needed?
        // For now, we assume 1:1 and don't track carefully.
        // But wait, Invalidate has window_id.
        // We should store 'win' pointer in a map or something.
        // Hack: Store 'fd' in Window struct? Or vice versa.
        // For now, we will lookup window by ID or just iterate.
        // Let's store win->id in the response.

        // Send response
        gfx_msg_t resp;
        resp.type = 2;                         // MSG_GFX_WINDOW_CREATED
        resp.data.created.window_id = win->id; // Use real ID
        resp.data.created.shm_id = shmid;
        k_write(fd, &resp, sizeof(resp));
      }
    }
  }
}

void handle_client_msg(int fd) {
  gfx_msg_t msg;
  int n = k_read(fd, &msg, sizeof(gfx_msg_t));
  if (n > 0) {
    if (msg.type == 3) { // MSG_GFX_INVALIDATE_RECT
      // Update the window!
      // We need to find the window.
      // Since we don't have a map, let's search windows
      int wid = msg.data.invalidate.window_id;
      // Iterate all windows
      for (int d = 0; d < 1; d++) {
        for (int i = 0; i < win_count[d]; i++) {
          if (windows[d][i].alive && windows[d][i].id == wid) {
            // Refresh it!
            // Since it has backbuffer, next draw_window loop will pick it up.
            // But checking "dirty" flags might be optimized later.
            // For now, just ensuring we read the msg clears the pipe.
            break;
          }
        }
      }
    }
    // Handle other messages if needed
  }
}

void init() {
  for (int i = 0; i < 16; i++)
    client_fds[i] = -1;

  server_fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    serial_log("WS: Failed to create socket");
    return;
  }

  if (sys_bind(server_fd, WS_PORT) < 0) {
    serial_log("WS: Failed to bind socket");
    return;
  }

  if (sys_listen(server_fd, 5) < 0) {
    serial_log("WS: Failed to listen");
    return;
  }

  serial_log("WS: WindowServer Listening on " WS_PORT);
}

void poll() {
  if (server_fd < 0)
    return;

  // 1. Accept new clients
  if (socket_can_accept(server_fd)) {
    int client = sys_accept(server_fd);
    if (client >= 0) {
      if (client_count < 16) {
        for (int i = 0; i < 16; i++) {
          if (client_fds[i] == -1) {
            client_fds[i] = client;
            client_count++;
            serial_log("WS: Client connected, handling handshake...");

            // process immediately for now (Blocking but short)
            handle_client_setup(client);
            break;
          }
        }
      } else {
        serial_log("WS: Too many clients, dropping");
        sys_close(client);
      }
    }
  }

  // 2. Poll existing clients
  for (int i = 0; i < 16; i++) {
    int fd = client_fds[i];
    if (fd != -1) {
      if (socket_can_read(fd)) {
        handle_client_msg(fd);
      }
    }
  }
}

} // namespace WindowServer

extern "C" void gui_main() {
  bga_set_video_mode(1024, 768, 32);
  FB::init();
  load_dir();
  DesktopSystem::refresh();

  WindowServer::init();

  // Add desktop icons (Fixed ones)
  // These are also added in refresh(), but let's ensure consistency if this is
  // init logic. The refresh() function clears and re-adds, so we don't strictly
  // need to duplicate here if refresh() is called. However, looking at the
  // code, refresh() IS called right above. BUT, lines 2634-2637 add icons
  // manually AGAIN? "DesktopSystem::add_icon..." This seems redundant or
  // initial setup. I should add Notepad here too just in case.

  // Icons are handled by refresh() now to ensure grid layout.
  // DesktopSystem::add_icon("Explorer", ...);

  // Launch initial explorer
  launch_explorer();

  serial_log("GUI: Entering main loop with net_poll");

  while (true) {
    Input::poll();
    WindowServer::poll();
    net_poll();

    // Fix 4: Keyboard Focus Contract
    int k, ks;
    if (sys_read_key(&k, &ks)) {
      if (g_rename.active) {
        handle_rename_key(k);
      } else {
        for (int i = win_count[current_desktop] - 1; i >= 0; i--) {
          if (windows[current_desktop][i].alive &&
              windows[current_desktop][i].focused &&
              windows[current_desktop][i].key) {
            windows[current_desktop][i].key(&windows[current_desktop][i], k,
                                            ks);
            break;
          }
        }
      }
    }

    DesktopSystem::draw_desktop();

    int count = win_count[current_desktop];
    for (int i = 0; i < count; i++) {
      if (windows[current_desktop][i].alive) {
        draw_window(&windows[current_desktop][i]);
      }
    }

    if (g_rename.active) {
      draw_rename_dialog();
    }

    // Fix 5: Context Menu Priority
    if (g_ctx_menu.visible) {
      handle_context_input();
      draw_context_menu();
    } else {
      // Global Right-Click detection
      if (Input::right_clicked()) {
        on_right_click(Input::x(), Input::y());
      }

      bool consumed = false;
      for (int i = win_count[current_desktop] - 1; i >= 0; i--) {
        if (windows[current_desktop][i].alive) {
          if (handle_window_input(&windows[current_desktop][i], i)) {
            consumed = true;
            break;
          }
        }
      }

      if (!consumed) {
        DesktopSystem::handle_desktop_input();
        handle_taskbar_click();
      }
      draw_taskbar();
    }

    draw_cursor(Input::x(), Input::y());

    // Fix 3: Compact window list
    for (int i = 0; i < win_count[current_desktop];) {
      if (!windows[current_desktop][i].alive &&
          windows[current_desktop][i].fade.value < 0.1f) {
        for (int j = i; j < win_count[current_desktop] - 1; j++)
          windows[current_desktop][j] = windows[current_desktop][j + 1];
        win_count[current_desktop]--;
      } else {
        i++;
      }
    }

    FB::swap();
  }
}
