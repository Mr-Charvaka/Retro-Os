#include "../drivers/vga.h"
#include "../include/io.h"
#include "../include/irq.h"
#include "../include/types.h"
#include "../kernel/process.h"
#include "../kernel/tty.h"
#include "serial.h"

// US Keyboard Layout (Normal)
char kbd_us[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    13,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   '-', 0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0};

// US Keyboard Layout (Shifted)
char kbd_us_shifted[128] = {
    0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    13,   0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   '_', 0,   0,   0,
    0,    0,    0,   0,   0,   0,   0,   0,   0};

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;

extern "C" void keyboard_set_last_key(int k);

static void keyboard_callback(registers_t *regs) {
  uint8_t scancode = inb(0x60);
  serial_log_hex("KEYBOARD: Scancode ", scancode);

  // Modifier Keys (Daba ke rakha hai)
  if (scancode == 0x2A || scancode == 0x36) { // LShift, RShift
    shift_pressed = 1;
    return;
  }
  if (scancode == 0x1D) { // LCtrl
    ctrl_pressed = 1;
    return;
  }
  if (scancode == 0x38) { // LAlt
    alt_pressed = 1;
    return;
  }
  if (scancode == 0x3A) { // Caps Lock
    caps_lock = !caps_lock;
    return;
  }

  // Modifier Keys (Chhod diya)
  if (scancode == 0xAA || scancode == 0xB6) { // LShift, RShift release
    shift_pressed = 0;
    return;
  }
  if (scancode == 0x9D) { // LCtrl release
    ctrl_pressed = 0;
    return;
  }
  if (scancode == 0xB8) { // LAlt release
    alt_pressed = 0;
    return;
  }

  // Dabaye gaye normal buttons
  if (!(scancode & 0x80)) {
    // Combinations check karo
    if (ctrl_pressed && scancode == 0x2E) { // Ctrl+C
      serial_log("KEYBOARD: Ctrl+C detected. Sending SIGINT.");
      sys_kill(current_process->id, SIGINT);
      return;
    }

    // Character mapping decide karo
    char c = 0;
    int is_letter = (scancode >= 0x10 && scancode <= 0x19) || // q to p
                    (scancode >= 0x1E && scancode <= 0x26) || // a to l
                    (scancode >= 0x2C && scancode <= 0x32);   // z se m tak

    if (is_letter) {
      // Letters: Shift XOR Caps Lock ka logic
      if (shift_pressed ^ caps_lock) {
        c = kbd_us_shifted[scancode];
      } else {
        c = kbd_us[scancode];
      }
    } else {
      // Non-letters: Sirf Shift ko dekho
      if (shift_pressed) {
        c = kbd_us_shifted[scancode];
      } else {
        c = kbd_us[scancode];
      }
    }

    // Special Keys (Arrows aur F buttons ke liye scancode mapping)
    if (c == 0) {
      if (scancode == 0x48)
        c = 17; // Up
      if (scancode == 0x50)
        c = 18; // Down
      if (scancode == 0x4B)
        c = 19; // Left
      if (scancode == 0x4D)
        c = 20; // Right
      if (scancode == 0x3E)
        c = 14; // F4
    }

    // Kernel Mode - seedha shell pe jao
    if (c != 0) {
      keyboard_set_last_key(c);
    }
  }
}

#include "../kernel/apic.h"

void init_keyboard() {
  register_interrupt_handler(33, keyboard_callback); // IRQ1 = IDT 33
  ioapic_set_mask(1, false);
  serial_log("KEYBOARD: Initialized.");
}
