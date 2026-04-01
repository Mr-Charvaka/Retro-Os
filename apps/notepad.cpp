// =============================================
//  NOTEPAD APPLICATION FOR RETRO-OS
//  File: apps/notepad.cpp
// =============================================

#include "include/os/ipc.hpp"
#include "include/os/syscalls.hpp"

// ---------------------------------------------
// Helpers
// ---------------------------------------------
static int my_strlen(const char *s) {
  int len = 0;
  while (s[len])
    len++;
  return len;
}

static void my_strcpy(char *d, const char *s) {
  while (*s)
    *d++ = *s++;
  *d = 0;
}

static char *my_strdup(const char *s) {
  if (!s)
    return nullptr;
  int len = my_strlen(s);
  char *d = new char[len + 1];
  my_strcpy(d, s);
  return d;
}

// ---------------------------------------------
// Font
// ---------------------------------------------
static uint8_t font8x8[128][8];

void init_font8x8() {
  for (int i = 0; i < 128; i++)
    for (int j = 0; j < 8; j++)
      font8x8[i][j] = 0;

  auto set_char = [](int c, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                     uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
    font8x8[c][0] = d0;
    font8x8[c][1] = d1;
    font8x8[c][2] = d2;
    font8x8[c][3] = d3;
    font8x8[c][4] = d4;
    font8x8[c][5] = d5;
    font8x8[c][6] = d6;
    font8x8[c][7] = d7;
  };

  set_char(32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  set_char(33, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00);
  set_char(34, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00);
  set_char(35, 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00);
  set_char(36, 0x1C, 0x3E, 0x61, 0x3C, 0x07, 0x7E, 0x38, 0x00);
  set_char(39, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00);
  set_char(40, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00);
  set_char(41, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00);
  set_char(42, 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00);
  set_char(43, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00);
  set_char(44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30);
  set_char(45, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00);
  set_char(46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00);
  set_char(47, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00);
  set_char(48, 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00);
  set_char(49, 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00);
  set_char(50, 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00);
  set_char(51, 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00);
  set_char(52, 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00);
  set_char(53, 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00);
  set_char(54, 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00);
  set_char(55, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00);
  set_char(56, 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00);
  set_char(57, 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00);
  set_char(58, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00);
  set_char(59, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30);
  set_char(60, 0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00);
  set_char(61, 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00);
  set_char(62, 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00);
  set_char(63, 0x3C, 0x66, 0x66, 0x0C, 0x18, 0x00, 0x18, 0x00);
  set_char(64, 0x3C, 0x66, 0x06, 0x3E, 0x66, 0x66, 0x3C, 0x00);
  set_char(65, 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00);
  set_char(66, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00);
  set_char(67, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00);
  set_char(68, 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00);
  set_char(69, 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00);
  set_char(70, 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00);
  set_char(71, 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00);
  set_char(72, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00);
  set_char(73, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00);
  set_char(74, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00);
  set_char(75, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00);
  set_char(76, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00);
  set_char(77, 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00);
  set_char(78, 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00);
  set_char(79, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00);
  set_char(80, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00);
  set_char(81, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00);
  set_char(82, 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00);
  set_char(83, 0x3C, 0x60, 0x3C, 0x06, 0x06, 0x66, 0x3C, 0x00);
  set_char(84, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00);
  set_char(85, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00);
  set_char(86, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00);
  set_char(87, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00);
  set_char(88, 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00);
  set_char(89, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x00);
  set_char(90, 0x7E, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x7E, 0x00);
  set_char(91, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00);
  set_char(92, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00);
  set_char(93, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x00);
  set_char(94, 0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00);
  set_char(95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00);
  set_char(96, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00);
  set_char(97, 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00);
  set_char(98, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00);
  set_char(99, 0x00, 0x00, 0x3C, 0x60, 0x60, 0x66, 0x3C, 0x00);
  set_char(100, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00);
  set_char(101, 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00);
  set_char(102, 0x0E, 0x18, 0x3E, 0x18, 0x18, 0x18, 0x18, 0x00);
  set_char(103, 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C);
  set_char(104, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00);
  set_char(105, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00);
  set_char(106, 0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00);
  set_char(107, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00);
  set_char(108, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00);
  set_char(109, 0x00, 0x00, 0x7E, 0x6B, 0x6B, 0x6B, 0x6B, 0x00);
  set_char(110, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00);
  set_char(111, 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00);
  set_char(112, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60);
  set_char(113, 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06);
  set_char(114, 0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00);
  set_char(115, 0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00);
  set_char(116, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x0E, 0x00);
  set_char(117, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00);
  set_char(118, 0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00);
  set_char(119, 0x00, 0x00, 0x63, 0x6B, 0x6B, 0x7F, 0x3E, 0x00);
  set_char(120, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00);
  set_char(121, 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C);
  set_char(122, 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00);
  set_char(123, 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00);
  set_char(124, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00);
  set_char(125, 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00);
  set_char(126, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
}

#define FONT_W 9
#define FONT_H 16
static const char *DEFAULT_NOTE_PATH = "/home/user/Documents/note.txt";

// ---------------------------------------------
// Text Buffer
// ---------------------------------------------
struct TextBuffer {
  char **lines;
  int line_count;
  int line_capacity;

  int cursor_row = 0;
  int cursor_col = 0;
  int scroll_y = 0;
  bool dirty = false;

  TextBuffer() : lines(nullptr), line_count(0), line_capacity(0) {
    add_line("");
  }

  ~TextBuffer() {
    if (lines) {
      for (int i = 0; i < line_count; i++)
        delete[] lines[i];
      delete[] lines;
    }
  }

  void clear() {
    if (lines) {
      for (int i = 0; i < line_count; i++)
        delete[] lines[i];
      delete[] lines;
    }
    lines = nullptr;
    line_count = 0;
    line_capacity = 0;
    cursor_row = cursor_col = scroll_y = 0;
    add_line("");
    dirty = false;
  }

  void add_line(const char *txt) {
    if (line_count >= line_capacity) {
      line_capacity = (line_capacity == 0) ? 16 : line_capacity * 2;
      char **new_lines = new char *[line_capacity];
      if (lines) {
        for (int i = 0; i < line_count; i++)
          new_lines[i] = lines[i];
        delete[] lines;
      }
      lines = new_lines;
    }
    lines[line_count++] = my_strdup(txt);
  }

  void insert_line_at(int idx, const char *txt) {
    if (line_count >= line_capacity) {
      line_capacity = (line_capacity == 0) ? 16 : line_capacity * 2;
      char **new_lines = new char *[line_capacity];
      if (lines) {
        for (int i = 0; i < line_count; i++)
          new_lines[i] = lines[i];
        delete[] lines;
      }
      lines = new_lines;
    }
    for (int i = line_count; i > idx; i--) {
      lines[i] = lines[i - 1];
    }
    lines[idx] = my_strdup(txt);
    line_count++;
  }

  void remove_line_at(int idx) {
    if (idx < 0 || idx >= line_count)
      return;
    delete[] lines[idx];
    for (int i = idx; i < line_count - 1; i++) {
      lines[i] = lines[i + 1];
    }
    line_count--;
  }

  void append_char(char *&s, char c) {
    int len = my_strlen(s);
    char *n = new char[len + 2];
    my_strcpy(n, s);
    n[len] = c;
    n[len + 1] = 0;
    delete[] s;
    s = n;
  }

  bool load_file(const char *path) {
    int fd = OS::Syscall::open(path, O_RDONLY);
    if (fd < 0)
      return false;

    clear();
    char *buf = new char[32768];
    int n = OS::Syscall::read(fd, buf, 32768);
    OS::Syscall::close(fd);

    if (n < 0) {
      delete[] buf;
      return false;
    }

    if (n > 0) {
      for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
          add_line("");
        } else if (buf[i] == '\r') {
          // ignore
        } else if ((buf[i] >= 32 && buf[i] <= 126) || buf[i] == '\t') {
          append_char(lines[line_count - 1], buf[i]);
        }
      }
    }
    delete[] buf;
    dirty = false;
    return true;
  }

  bool save_file(const char *path) {
    int fd = OS::Syscall::open(path, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0)
      return false;

    for (int i = 0; i < line_count; i++) {
      int len = my_strlen(lines[i]);
      int w = OS::Syscall::write(fd, lines[i], len);
      if (w != len) {
        OS::Syscall::close(fd);
        return false;
      }
      if (i < line_count - 1) {
        if (OS::Syscall::write(fd, "\n", 1) != 1) {
          OS::Syscall::close(fd);
          return false;
        }
      }
    }
    OS::Syscall::close(fd);
    dirty = false;
    return true;
  }

  char *concat(const char *s1, const char *s2) {
    int len1 = my_strlen(s1);
    int len2 = my_strlen(s2);
    char *res = new char[len1 + len2 + 1];
    my_strcpy(res, s1);
    my_strcpy(res + len1, s2);
    return res;
  }

  void insert_char(char c) {
    if (cursor_row >= line_count)
      cursor_row = line_count - 1;

    char *line = lines[cursor_row];
    int len = my_strlen(line);

    char *new_line = new char[len + 2];
    for (int i = 0; i < cursor_col; i++)
      new_line[i] = line[i];
    new_line[cursor_col] = c;
    for (int i = cursor_col; i < len; i++)
      new_line[i + 1] = line[i];
    new_line[len + 1] = 0;

    delete[] line;
    lines[cursor_row] = new_line;
    cursor_col++;
    dirty = true;
  }

  void backspace() {
    if (cursor_col == 0 && cursor_row > 0) {
      int prev_len = my_strlen(lines[cursor_row - 1]);
      char *merged = concat(lines[cursor_row - 1], lines[cursor_row]);
      delete[] lines[cursor_row - 1];
      lines[cursor_row - 1] = merged;
      remove_line_at(cursor_row);
      cursor_row--;
      cursor_col = prev_len;
    } else if (cursor_col > 0) {
      char *line = lines[cursor_row];
      int len = my_strlen(line);
      char *new_line = new char[len];
      for (int i = 0; i < cursor_col - 1; i++)
        new_line[i] = line[i];
      for (int i = cursor_col; i < len; i++)
        new_line[i - 1] = line[i];
      new_line[len - 1] = 0;
      delete[] line;
      lines[cursor_row] = new_line;
      cursor_col--;
    }
    dirty = true;
  }

  void newline() {
    char *line = lines[cursor_row];
    int len = my_strlen(line);
    char *first_part = new char[cursor_col + 1];
    for (int i = 0; i < cursor_col; i++)
      first_part[i] = line[i];
    first_part[cursor_col] = 0;
    char *second_part = new char[len - cursor_col + 1];
    for (int i = cursor_col; i < len; i++)
      second_part[i - cursor_col] = line[i];
    second_part[len - cursor_col] = 0;
    delete[] line;
    lines[cursor_row] = first_part;
    insert_line_at(cursor_row + 1, second_part);
    delete[] second_part;
    cursor_row++;
    cursor_col = 0;
    dirty = true;
  }
};

// ---------------------------------------------
// App
// ---------------------------------------------
class NotepadApp {
  OS::IPCClient *ipc;
  TextBuffer buffer;
  char title[64];
  char current_path[256];
  int width, height;
  enum { MENU_NONE, MENU_FILE, MENU_EDIT, MENU_HELP } active_menu;
  char status[64];

public:
  NotepadApp() : ipc(nullptr), width(600), height(400), active_menu(MENU_NONE) {
    my_strcpy(title, "Notepad");
    my_strcpy(current_path, DEFAULT_NOTE_PATH);
    status[0] = 0;
  }
  ~NotepadApp() {
    if (ipc)
      delete ipc;
  }

  bool init() {
    ipc = new OS::IPCClient();
    if (!ipc->connect())
      return false;
    if (!ipc->create_window(title, width, height))
      return false;
    return true;
  }

  bool open_path(const char *path) {
    if (!path || !path[0])
      return false;
    my_strcpy(current_path, path);
    if (buffer.load_file(path)) {
      // Place cursor at end of loaded content for immediate editing
      buffer.cursor_row = buffer.line_count - 1;
      if (buffer.cursor_row < 0)
        buffer.cursor_row = 0;
      buffer.cursor_col = my_strlen(buffer.lines[buffer.cursor_row]);
      my_strcpy(status, "File Opened");
      return true;
    }
    buffer.clear();
    my_strcpy(status, "New File");
    return false;
  }

  void draw_text(int x, int y, const char *txt, uint32_t color) {
    int cx = x;
    while (*txt) {
      uint8_t c = (uint8_t)*txt;
      if (c >= 128)
        c = '?';
      const uint8_t *glyph = font8x8[c];
      for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
          if (glyph[r] & (1 << (7 - col))) {
            ipc->fill_rect(cx + col, y + r * 2, 1, 2, color);
          }
        }
      }
      cx += FONT_W;
      txt++;
    }
  }

  void render() {
    // Background
    ipc->fill_rect(0, 0, width, height, 0xFFFFFFFF);

    // Toolbar
    ipc->fill_rect(0, 0, width, 24, 0xFFD0D0D0);
    draw_text(10, 4, "File",
              active_menu == MENU_FILE ? 0xFFFF0000 : 0xFF000000);
    draw_text(60, 4, "Edit",
              active_menu == MENU_EDIT ? 0xFFFF0000 : 0xFF000000);
    draw_text(110, 4, "Help",
              active_menu == MENU_HELP ? 0xFFFF0000 : 0xFF000000);

    // Status / Path
    if (current_path[0])
      draw_text(200, 4, current_path, 0xFF666666);
    if (status[0])
      draw_text(width - 95, 4, status, 0xFF008800);

    // Separator
    ipc->fill_rect(0, 24, width, 1, 0xFF888888);

    int start_y = 30;
    int rows_visible = (height - start_y) / FONT_H;

    // Scrolling follow cursor
    if (buffer.cursor_row < buffer.scroll_y)
      buffer.scroll_y = buffer.cursor_row;
    if (buffer.cursor_row >= buffer.scroll_y + rows_visible)
      buffer.scroll_y = buffer.cursor_row - rows_visible + 1;

    for (int i = 0; i < rows_visible; i++) {
      int line_idx = buffer.scroll_y + i;
      if (line_idx >= buffer.line_count)
        break;
      draw_text(4, start_y + i * FONT_H, buffer.lines[line_idx], 0xFF000000);
    }

    // Cursor
    if (buffer.cursor_row >= buffer.scroll_y &&
        buffer.cursor_row < buffer.scroll_y + rows_visible) {
      int cy = start_y + (buffer.cursor_row - buffer.scroll_y) * FONT_H;
      int cx = 4 + buffer.cursor_col * FONT_W;
      ipc->fill_rect(cx, cy, 2, FONT_H, 0xFFFF0000); // Red cursor!
    }

    // Dropdown menus - More visual pop
    if (active_menu == MENU_FILE) {
      ipc->fill_rect(8, 24, 120, 100, 0xFF000000); // Shadow/Border
      ipc->fill_rect(10, 26, 116, 96, 0xFFF5F5F5); // Background
      draw_text(15, 30, "1. New", 0xFF000000);
      draw_text(15, 50, "2. Open", 0xFF000000);
      draw_text(15, 70, "3. Save", 0xFF000000);
      draw_text(15, 90, "4. Exit", 0xFF000000);
    } else if (active_menu == MENU_EDIT) {
      ipc->fill_rect(58, 24, 120, 40, 0xFF000000);
      ipc->fill_rect(60, 26, 116, 36, 0xFFF5F5F5);
      draw_text(65, 30, "1. Clear", 0xFF000000);
    } else if (active_menu == MENU_HELP) {
      ipc->fill_rect(108, 24, 150, 40, 0xFF000000);
      ipc->fill_rect(110, 26, 146, 36, 0xFFF5F5F5);
      draw_text(115, 30, "Retro Notepad v1", 0xFF0000FF);
    }

    ipc->flush();
  }

  void handle_click(int x, int y) {
    if (y < 24) {
      if (x > 5 && x < 50)
        active_menu = (active_menu == MENU_FILE) ? MENU_NONE : MENU_FILE;
      else if (x > 55 && x < 100)
        active_menu = (active_menu == MENU_EDIT) ? MENU_NONE : MENU_EDIT;
      else if (x > 105 && x < 150)
        active_menu = (active_menu == MENU_HELP) ? MENU_NONE : MENU_HELP;
      else
        active_menu = MENU_NONE;
      my_strcpy(status, "");
      render();
      return;
    }

    if (active_menu == MENU_FILE) {
      // Match exact drawn menu geometry: x=[10..126], y=[26..106)
      if (x >= 10 && x <= 126 && y >= 26 && y < 106) {
        int item = (y - 26) / 20;
        if (item == 0) { // New
          buffer.clear();
          my_strcpy(current_path, DEFAULT_NOTE_PATH);
          my_strcpy(status, "New Doc Created");
          active_menu = MENU_NONE;
          render();
          return;
        } else if (item == 1) { // Open
          if (!current_path[0])
            my_strcpy(current_path, DEFAULT_NOTE_PATH);
          open_path(current_path);
          active_menu = MENU_NONE;
          render();
          return;
        } else if (item == 2) { // Save
          if (!current_path[0])
            my_strcpy(current_path, DEFAULT_NOTE_PATH);
          if (buffer.save_file(current_path))
            my_strcpy(status, "Saved!");
          else
            my_strcpy(status, "Save Failed");
          active_menu = MENU_NONE;
          render();
          return;
        } else if (item == 3) { // Exit
          active_menu = MENU_NONE;
          render();
          if (buffer.dirty) {
            if (!current_path[0])
              my_strcpy(current_path, DEFAULT_NOTE_PATH);
            buffer.save_file(current_path);
          }
          ipc->close_window(); // same encoding as red X button
          OS::Syscall::exit(0);
          return;
        }
      }

      // Clicked while File menu is open but not on a valid item -> close menu
      active_menu = MENU_NONE;
      render();
      return;
    } else if (active_menu == MENU_EDIT) {
      // Match exact drawn edit menu geometry: x=[60..176], y=[26..46)
      if (x >= 60 && x <= 176 && y >= 26 && y < 46) {
        buffer.clear();
        my_strcpy(status, "Cleared");
      }
      active_menu = MENU_NONE;
      render();
      return;
    } else {
      active_menu = MENU_NONE;

      // Click in editor area -> move cursor to clicked text position
      int start_y = 30;
      if (y >= start_y && buffer.line_count > 0) {
        int row_in_view = (y - start_y) / FONT_H;
        if (row_in_view < 0)
          row_in_view = 0;

        int target_row = buffer.scroll_y + row_in_view;
        if (target_row < 0)
          target_row = 0;
        if (target_row >= buffer.line_count)
          target_row = buffer.line_count - 1;

        int target_col = (x - 4) / FONT_W;
        if (target_col < 0)
          target_col = 0;

        int line_len = my_strlen(buffer.lines[target_row]);
        if (target_col > line_len)
          target_col = line_len;

        buffer.cursor_row = target_row;
        buffer.cursor_col = target_col;
      }

      render();
    }
  }

  void run() {
    render();
    while (true) {
      OS::gfx_msg_t msg;
      if (ipc->recv_msg(&msg)) {
        if (msg.type == OS::MSG_GFX_KEY_EVENT) {
          char k = msg.data.key.key;
          my_strcpy(status, ""); // Clear status on type
          bool edited = false;

          // Arrow keys from keyboard driver mapping:
          // 17=Up, 18=Down, 19=Left, 20=Right
          if (k == 19) {
            if (buffer.cursor_col > 0) {
              buffer.cursor_col--;
            } else if (buffer.cursor_row > 0) {
              buffer.cursor_row--;
              buffer.cursor_col = my_strlen(buffer.lines[buffer.cursor_row]);
            }
          } else if (k == 20) {
            int len = my_strlen(buffer.lines[buffer.cursor_row]);
            if (buffer.cursor_col < len) {
              buffer.cursor_col++;
            } else if (buffer.cursor_row + 1 < buffer.line_count) {
              buffer.cursor_row++;
              buffer.cursor_col = 0;
            }
          } else if (k == 17) {
            if (buffer.cursor_row > 0)
              buffer.cursor_row--;
            int len = my_strlen(buffer.lines[buffer.cursor_row]);
            if (buffer.cursor_col > len)
              buffer.cursor_col = len;
          } else if (k == 18) {
            if (buffer.cursor_row + 1 < buffer.line_count)
              buffer.cursor_row++;
            int len = my_strlen(buffer.lines[buffer.cursor_row]);
            if (buffer.cursor_col > len)
              buffer.cursor_col = len;
          } else if (k == '\b')
            buffer.backspace(), edited = true;
          else if (k == '\n' || k == '\r')
            buffer.newline(), edited = true;
          else if (k == 14) { // F4 quick-save
            if (!current_path[0])
              my_strcpy(current_path, DEFAULT_NOTE_PATH);
            if (buffer.save_file(current_path))
              my_strcpy(status, "Saved!");
            else
              my_strcpy(status, "Save Failed");
          } else if (k >= 32 && k <= 126)
            buffer.insert_char(k), edited = true;

          // Autosave after edits to avoid data loss on close/crash.
          if (edited) {
            if (!current_path[0])
              my_strcpy(current_path, DEFAULT_NOTE_PATH);
            if (buffer.save_file(current_path))
              my_strcpy(status, "Saved!");
            else
              my_strcpy(status, "Save Failed");
          }
          render();
        } else if (msg.type == OS::MSG_GFX_MOUSE_EVENT) {
          handle_click(msg.data.mouse.x, msg.data.mouse.y);
        }
      }
    }
  }
};

extern "C" void _start(int argc, char **argv) {
  init_font8x8();
  NotepadApp app;
  if (app.init()) {
    if (argc > 1 && argv && argv[1]) {
      app.open_path(argv[1]);
    }
    app.run();
  }
  OS::Syscall::exit(0);
}
