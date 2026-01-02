#include "serial.h"
#include "../include/io.h"

#define PORT 0x3f8 // COM1

extern "C" {

void init_serial() {
  outb(PORT + 1, 0x00); // Disable all interrupts
  outb(PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
  outb(PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
  outb(PORT + 1, 0x00); //                  (hi byte)
  outb(PORT + 3, 0x03); // 8 bits, no parity, one stop bit
  outb(PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
  outb(PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

int is_transmit_empty() { return inb(PORT + 5) & 0x20; }

void serial_write(char c) {
  while (is_transmit_empty() == 0)
    ;
  outb(PORT, c);
}

void serial_log(const char *str) {
  for (int i = 0; str[i] != 0; i++) {
    serial_write(str[i]);
  }
  serial_write('\n');
}

void serial_log_hex(const char *label, uint32_t value) {
  serial_log(label);

  char hex[] = "0123456789ABCDEF";
  serial_write('0');
  serial_write('x');

  for (int i = 28; i >= 0; i -= 4) {
    serial_write(hex[(value >> i) & 0xF]);
  }
  serial_write('\n');
}

} // extern "C"
