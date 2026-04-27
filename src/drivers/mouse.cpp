#include "../include/io.h"
#include "../include/irq.h"
#include "../include/types.h"
#include "../kernel/apic.h"
#include "serial.h"

uint8_t mouse_cycle = 0;
int8_t mouse_byte[3];

static int mouse_x = 512;
static int mouse_y = 384;
static uint8_t mouse_btn = 0;

extern volatile uint32_t g_smp_ready;

extern "C" void get_mouse_state(int *x, int *y, uint8_t *btn) {
  *x = mouse_x;
  *y = mouse_y;
  *btn = mouse_btn;
}

void mouse_wait(uint8_t type) {
  uint32_t _time_out = 100000;
  if (type == 0) {
    while (_time_out--) { // Wait for Data
      if ((inb(0x64) & 0x01) == 1) {
        return;
      }
    }
    return;
  } else {
    while (_time_out--) { // Wait for Signal
      if ((inb(0x64) & 0x02) == 0) {
        return;
      }
    }
    return;
  }
}

void mouse_write(uint8_t a_write) {
  // Wait to be able to send a command
  mouse_wait(1);
  // Tell the mouse we are sending a command
  outb(0x64, 0xD4);
  // Wait for the final part
  mouse_wait(1);
  // Finally write
  outb(0x60, a_write);
}

uint8_t mouse_read() {
  // Get response from mouse
  mouse_wait(0);
  return inb(0x60);
}

void mouse_callback(registers_t *regs) {
  uint8_t status = inb(0x64);
  
  if (g_smp_ready) {
      if (!(status & 0x01)) return; // Wait until hardware buffer actually has data
  }

  // Debug Heartbeat: shows hardware interrupts are reaching Core 0
  // if (tick % 100 == 0) serial_log_hex(".", tick);

  uint8_t data = inb(0x60);
  
  // Now check if it's actually mouse data (Bit 5 of status)
  if (!(status & 0x20)) {
    return;
  }

  // ==== PACKET SYNC CHECK ====
  // PS/2 first byte is always: Y-ovf, X-ovf, Y-sign, X-sign, Always-1, Middle, Right, Left
  // If bit 3 is not 1, we are out of sync.
  if (mouse_cycle == 0 && !(data & 0x08)) {
    return; // Drop byte and wait for a true start-of-packet
  }

  mouse_byte[mouse_cycle++] = data;

  if (mouse_cycle == 3) {
    mouse_cycle = 0;
    
    // Packet ready
    uint8_t state = mouse_byte[0];
    int8_t x_rel = mouse_byte[1];
    int8_t y_rel = mouse_byte[2];

    mouse_x += x_rel;
    mouse_y -= y_rel; // Y is inverted in mouse packets

    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_y < 0)
      mouse_y = 0;
    if (mouse_x > 1023)
      mouse_x = 1023;
    if (mouse_y > 767)
      mouse_y = 767;

    mouse_btn = state & 0x07; // Fill left, right, middle buttons
  }
}

void init_mouse() {
  serial_log("PS/2: Initializing controller...");

  // 1. Enable Keyboard & Mouse interfaces
  mouse_wait(1);
  outb(0x64, 0xAE); // Enable Keyboard
  mouse_wait(1);
  outb(0x64, 0xA8); // Enable Mouse (Auxiliary)

  // 2. Set Controller Command Byte
  mouse_wait(1);
  outb(0x64, 0x20); // Read Command Byte
  mouse_wait(0);
  uint8_t status = inb(0x60);

  // Enable IRQ1 (bit 0), IRQ12 (bit 1), enable clock (bit 4, 5 clear)
  status |= 0x03;  // Enable interrupts
  status &= ~0x30; // Enable clocks (clear disable bits)

  mouse_wait(1);
  outb(0x64, 0x60); // Write Command Byte
  mouse_wait(1);
  outb(0x60, status);

  // 3. Initialize Mouse Device
  serial_log("MOUSE: Sending reset (0xF6)...");
  int retries = 5;
  while (retries--) {
      mouse_write(0xF6); // Set defaults
      uint8_t response = mouse_read();
      serial_log_hex("MOUSE: Reset response: ", response);
      if (response == 0xFA) break; // ACK
  }

  serial_log("MOUSE: Sending enable (0xF4)...");
  retries = 5;
  while (retries--) {
      mouse_write(0xF4); // Enable data reporting
      uint8_t response = mouse_read();
      serial_log_hex("MOUSE: Enable response: ", response);
      if (response == 0xFA) break; // ACK
  }


  // 4. Register Handler
  register_interrupt_handler(44, mouse_callback); // IRQ12 = IDT 44
  ioapic_set_mask(12, false);

  serial_log("PS/2: Mouse/Keyboard interfaces enabled.");
}
