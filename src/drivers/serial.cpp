#include "serial.h"
#include "../include/io.h"
#include "../kernel/apic.h"

extern volatile uint32_t g_smp_ready;

extern "C" {

void serial_init_port(uint16_t port) {
  outb(port + 1, 0x00); // Disable all interrupts
  outb(port + 3, 0x80); // Enable DLAB (set baud rate divisor)
  outb(port + 0, 0x01); // Set divisor to 1 (lo byte) 115200 baud
  outb(port + 1, 0x00); //                  (hi byte)
  outb(port + 3, 0x03); // 8 bits, no parity, one stop bit
  outb(port + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
  outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

void init_serial() {
    serial_init_port(COM1);
    serial_init_port(COM2);
    serial_init_port(COM3);
    serial_init_port(COM4);
}

int is_transmit_empty(uint16_t port) { return inb(port + 5) & 0x20; }

void serial_write_on(uint16_t port, char c) {
  while (is_transmit_empty(port) == 0)
    ;
  outb(port, c);
}

void serial_write(char c) {
    uint16_t port = COM1;
    if (g_smp_ready) {
        uint32_t cpu = get_cpu_index();
        if (cpu == 1) port = COM2;
        else if (cpu == 2) port = COM3;
        else if (cpu == 3) port = COM4;
    }
    serial_write_on(port, c);
}

void serial_print_on(uint16_t port, const char* str) {
    for (int i = 0; str[i] != 0; i++) {
        serial_write_on(port, str[i]);
    }
}

void serial_log(const char *str) {
  uint16_t port = COM1;
  if (g_smp_ready) {
      uint32_t cpu = get_cpu_index();
      if (cpu == 1) port = COM2;
      else if (cpu == 2) port = COM3;
      else if (cpu == 3) port = COM4;
  }
  
  for (int i = 0; str[i] != 0; i++) {
    serial_write_on(port, str[i]);
  }
  serial_write_on(port, '\n');
}

void serial_log_hex(const char *label, uint32_t value) {
  uint16_t port = COM1;
  if (g_smp_ready) {
      uint32_t cpu = get_cpu_index();
      if (cpu == 1) port = COM2;
      else if (cpu == 2) port = COM3;
      else if (cpu == 3) port = COM4;
  }

  serial_print_on(port, label);

  char hex[] = "0123456789ABCDEF";
  serial_write_on(port, '0');
  serial_write_on(port, 'x');

  for (int i = 28; i >= 0; i -= 4) {
    serial_write_on(port, hex[(value >> i) & 0xF]);
  }
  serial_write_on(port, '\n');
}

} // extern "C"
