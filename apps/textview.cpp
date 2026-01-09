// textview.cpp
// User-space Text File Viewer for Retro-OS

#include "font.h" // Injected from src/include via build flags
#include <stddef.h>
#include <stdint.h>


extern "C" {
// ===== Syscalls =====
int sys_open(const char *path, int flags);
int sys_read(int fd, void *buf, uint32_t sz);
int sys_close(int fd);
void sys_exit(int code);

// GUI drawing (same framebuffer system)
void *sys_get_framebuffer();
int sys_fb_width();
int sys_fb_height();
void sys_fb_swap();

// Custom wrappers to reach the IDs we defined
void *sys_get_fb() {
  void *res;
  asm volatile("int $0x80" : "=a"(res) : "a"(150));
  return res;
}
int sys_fb_w() {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(151));
  return res;
}
int sys_fb_h() {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(152));
  return res;
}
void sys_fb_swp() { asm volatile("int $0x80" ::"a"(153)); }

// POSIX-style wrappers
int sys_open_wrap(const char *path, int flags) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(2), "b"(path), "c"(flags));
  return res;
}
int sys_read_wrap(int fd, void *buf, int sz) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(3), "b"(fd), "c"(buf), "d"(sz));
  return res;
}
int sys_close_wrap(int fd) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(5), "b"(fd));
  return res;
}
void sys_exit_wrap(int code) {
  asm volatile("int $0x80" ::"a"(12), "b"(code));
  while (1)
    ;
}
}

// Redefine to use our ASM wrappers
#define sys_open sys_open_wrap
#define sys_read sys_read_wrap
#define sys_close sys_close_wrap
#define sys_exit sys_exit_wrap
#define sys_get_framebuffer sys_get_fb
#define sys_fb_width sys_fb_w
#define sys_fb_height sys_fb_h
#define sys_fb_swap sys_fb_swp

// =====================
// BASIC DRAWING
// =====================

static uint32_t *fb;
static int W, H;

void fb_init() {
  fb = (uint32_t *)sys_get_framebuffer();
  W = sys_fb_width();
  H = sys_fb_height();
}

void fb_clear(uint32_t c) {
  if (!fb)
    return;
  for (int i = 0; i < W * H; i++)
    fb[i] = c;
}

void fb_put(int x, int y, uint32_t c) {
  if (!fb || x < 0 || y < 0 || x >= W || y >= H)
    return;
  fb[y * W + x] = c;
}

// =====================
// FONT (8x8)
// =====================

void draw_char(int x, int y, char ch) {
  if (ch < 0 || ch > 127)
    return;
  const uint8_t *g = font8x8_basic[(int)ch];
  for (int r = 0; r < 8; r++)
    for (int b = 0; b < 8; b++)
      if (g[r] & (1 << (7 - b)))
        fb_put(x + b, y + r, 0xFFFFFF);
}

void draw_text(int x, int y, const char *s) {
  while (*s) {
    draw_char(x, y, *s++);
    x += 8;
  }
}

// =====================
// TEXT BUFFER
// =====================

#define MAX_FILE_SIZE (64 * 1024)

static char file_buffer[MAX_FILE_SIZE];
static int file_size = 0;
static int scroll = 0;

// =====================
// LOAD FILE
// =====================

void load_file(const char *path) {
  int fd = sys_open(path, 0);
  if (fd < 0)
    sys_exit(1);

  file_size = sys_read(fd, file_buffer, MAX_FILE_SIZE - 1);
  if (file_size < 0)
    file_size = 0;
  file_buffer[file_size] = 0;

  sys_close(fd);
}

// =====================
// RENDER LOOP
// =====================

void render() {
  fb_clear(0x1A1A1A); // Dark grey background

  // Header
  fb_clear(
      0x2D2D2D); // Background for top bit manually? No, clear all then rect
  for (int i = 0; i < W * 24; i++)
    fb[i] = 0x333333;
  draw_text(16, 8, "RETRO-OS TEXT VIEWER");

  int x = 16;
  int y = 40;
  int line = 0;

  for (int i = 0; i < file_size; i++) {
    if (line < scroll) {
      if (file_buffer[i] == '\n')
        line++;
      continue;
    }

    if (file_buffer[i] == '\n') {
      y += 12;
      x = 16;
      line++;
      if (y > H - 16)
        break;
      continue;
    }

    if (file_buffer[i] == '\r')
      continue;

    draw_char(x, y, file_buffer[i]);
    x += 8;
    if (x > W - 16) {
      x = 16;
      y += 12;
    }

    if (y > H - 16)
      break;
  }

  sys_fb_swap();
}

// =====================
// ENTRY POINT
// =====================

extern "C" void _start(int argc, char **argv) {
  fb_init();

  if (argc < 2) {
    // Mock default if no arg for testing?
    // load_file("/home/user/Desktop/test.txt");
    sys_exit(1);
  } else {
    load_file(argv[1]);
  }

  while (1) {
    render();
    // future: sys_yield() if available
  }
}
