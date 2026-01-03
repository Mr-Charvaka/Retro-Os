// TTY - Terminal Device Implementation
#include "tty.h"
#include "../drivers/serial.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

extern "C" {

static tty_t console_tty;

// Shared buffer for GUI Terminal
extern char terminal_output_buffer[2048];
extern int terminal_output_len;

void tty_init() {
  memset(&console_tty, 0, sizeof(tty_t));
  strcpy(console_tty.name, "tty0");
  console_tty.flags = TTY_ECHO | TTY_CANON | TTY_ISIG;
  wait_queue_init(&console_tty.read_wait);
  console_tty.line_ready = 0;
  console_tty.fg_pgid = 1; // Init process
  serial_log("TTY: Console initialized.");
}

tty_t *tty_get_console() { return &console_tty; }

int tty_write(tty_t *tty, const char *buf, int len) {
  (void)tty;

  for (int i = 0; i < len; i++) {
    if (terminal_output_len < 2000) {
      terminal_output_buffer[terminal_output_len++] = buf[i];
      terminal_output_buffer[terminal_output_len] = 0;
    }
    // Also write to serial for debugging
    serial_write(buf[i]);
  }
  return len;
}

int tty_read(tty_t *tty, char *buf, int len) {
  if (!tty)
    return -1;

  // If canonical mode, wait for a complete newline-terminated line
  if (tty->flags & TTY_CANON) {
    while (tty->line_ready == 0) {
      sleep_on(&tty->read_wait);
    }

    // Copy the entire line
    int copy_len = (len < tty->line_len) ? len : tty->line_len;
    memcpy(buf, tty->line_buffer, copy_len);

    // Reset for next line
    tty->line_len = 0;
    tty->line_ready = 0;
    return copy_len;
  } else {
    // Raw mode - return any available characters
    if (tty->input_count == 0) {
      sleep_on(&tty->read_wait);
    }

    int copied = 0;
    while (copied < len && tty->input_count > 0) {
      buf[copied++] = tty->input_buffer[tty->input_tail];
      tty->input_tail = (tty->input_tail + 1) % TTY_BUFFER_SIZE;
      tty->input_count--;
    }
    return copied;
  }
}

void tty_input_char(tty_t *tty, char c) {
  if (!tty)
    return;

  // Handle special characters
  if (tty->flags & TTY_ISIG) {
    if (c == 3) { // Ctrl+C
      // Send SIGINT to foreground process group
      extern process_t *ready_queue;
      process_t *p = ready_queue;
      if (p) {
        do {
          if (p->id == (uint32_t)tty->fg_pgid) {
            p->pending_signals |= (1 << SIGINT);
          }
          p = p->next;
        } while (p != ready_queue);
      }
      return;
    }
  }

  // Echo to shared buffer if enabled
  if (tty->flags & TTY_ECHO) {
    char ec = c;
    if (c == '\r')
      ec = '\n';
    tty_write(tty, &ec, 1);
  }

  // If canonical mode, buffer until newline
  if (tty->flags & TTY_CANON) {
    if (c == '\b' || c == 127) { // Backspace
      if (tty->line_len > 0) {
        tty->line_len--;
      }
    } else if (c == '\r' || c == '\n') {
      tty->line_buffer[tty->line_len++] = '\n';
      tty->line_buffer[tty->line_len] = 0;
      tty->line_ready = 1;
      wake_up(&tty->read_wait);
    } else if (tty->line_len < TTY_BUFFER_SIZE - 1) {
      tty->line_buffer[tty->line_len++] = c;
    }
  } else {
    // Raw mode - add to input buffer
    if (tty->input_count < TTY_BUFFER_SIZE) {
      tty->input_buffer[tty->input_head] = c;
      tty->input_head = (tty->input_head + 1) % TTY_BUFFER_SIZE;
      tty->input_count++;
      wake_up(&tty->read_wait);
    }
  }
}

void tty_set_flags(tty_t *tty, uint32_t flags) {
  if (tty)
    tty->flags = flags;
}

} // extern "C"
