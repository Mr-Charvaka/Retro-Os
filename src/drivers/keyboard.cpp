#include "../drivers/vga.h"
#include "../include/io.h"
#include "../include/irq.h"
#include "../include/signal.h"
#include "../include/types.h"
#include "../kernel/gui.h"
#include "../kernel/process.h"
#include "../kernel/tty.h"
#include "serial.h"

// Simple US Keyboard Layout (partial)
char kbd_us[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    '\n', 0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   '-', 0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0};

static int ctrl_pressed = 0;

static void keyboard_callback(registers_t *regs) {
  uint8_t scancode = inb(0x60);

  if (scancode == 0x1D) {
    ctrl_pressed = 1;
    return;
  }
  if (scancode == 0x9D) { // Release Left Ctrl
    ctrl_pressed = 0;
    return;
  }

  // Only handle key press (ignore key release which has high bit set)
  if (scancode & 0x80) {
    // Key release
  } else {
    // Check for Ctrl+C (Scan code 0x2E is 'c')
    if (ctrl_pressed && scancode == 0x2E) {
      serial_log("KEYBOARD: Ctrl+C detected. Sending SIGINT.");
      sys_kill(current_process->id, SIGINT);
      return;
    }

    // Key press
    char c = kbd_us[scancode];
    if (c != 0) {
      handle_key_press(c);                  // Forward to GUI
      tty_input_char(tty_get_console(), c); // Forward to TTY
    }
  }
}

void init_keyboard() {
  register_interrupt_handler(33, keyboard_callback); // IRQ1 = IDT 33
}
