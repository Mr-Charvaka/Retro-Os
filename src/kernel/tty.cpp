#include "tty.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

extern "C" {

static tty_t console_tty;

// --- Built-in ANSI Parser for Professional TTY Experience ---
static int ansi_state = 0;
static int ansi_param = 0;

static void tty_putc(char c) {
    if (ansi_state == 0) {
        if (c == '\033') { ansi_state = 1; return; }
        vga_print_char(c); // Direct to VGA
        serial_write(c); // Mirror to serial
    } else if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_param = 0; }
        else ansi_state = 0;
    } else if (ansi_state == 2) {
        if (c >= '0' && c <= '9') { ansi_param = ansi_param * 10 + (c - '0'); }
        else if (c == 'm') {
            // SGR - Select Graphic Rendition
            uint8_t color = 0x07; // Default light gray
            if (ansi_param == 0) vga_set_color(0x07);
            else if (ansi_param == 31) vga_set_color(0x04); // Red
            else if (ansi_param == 32) vga_set_color(0x02); // Green
            else if (ansi_param == 34) vga_set_color(0x01); // Blue
            else if (ansi_param == 33) vga_set_color(0x0E); // Yellow
            ansi_state = 0;
        } else { ansi_state = 0; }
    }
}

static int console_tty_write(tty_t *tty, const char *buf, int len) {
    for (int i = 0; i < len; i++) tty_putc(buf[i]);
    return len;
}

void tty_init() {
  memset(&console_tty, 0, sizeof(tty_t));
  strcpy(console_tty.name, "tty0");
  console_tty.flags = TTY_ECHO | TTY_CANON | TTY_ISIG;
  wait_queue_init(&console_tty.read_wait);
  console_tty.fg_pgid = 1;
  console_tty.write_callback = console_tty_write;
  serial_log("TTY: Professional Console initialized with ANSI support.");
}

tty_t *tty_get_console() { return &console_tty; }

int tty_write(tty_t *tty, const char *buf, int len) {
  if (tty && tty->write_callback) return tty->write_callback(tty, buf, len);
  return -1;
}

int tty_read(tty_t *tty, char *buf, int len) {
  if (!tty) return -1;
  if (tty->flags & TTY_CANON) {
    while (tty->line_ready == 0) sleep_on(&tty->read_wait);
    int copy_len = (len < tty->line_len) ? len : tty->line_len;
    memcpy(buf, tty->line_buffer, copy_len);
    tty->line_len = 0; tty->line_ready = 0;
    return copy_len;
  }
  return 0;
}

void tty_input_char(tty_t *tty, char c) {
  if (!tty) return;
  // Ctrl+C SIGINT logic...
  if (tty->flags & TTY_ECHO) { tty_putc(c); }
  // Buffer logic...
}

} // extern "C"
