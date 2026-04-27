#include "serial.h"
#include "../include/io.h"
#include "../kernel/apic.h"

extern volatile uint32_t g_smp_ready;
extern "C" uint32_t tick;

static volatile int serial_lock = 0;

static void serial_lock_acquire() {
    while (__sync_lock_test_and_set(&serial_lock, 1)) {
        asm volatile("pause");
    }
}

static void serial_lock_release() {
    __sync_lock_release(&serial_lock);
}

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

static void serial_print_str_on(uint16_t port, const char* str) {
    while (*str) {
        serial_write_on(port, *str++);
    }
}

static void serial_print_prefix(uint16_t port) {
    uint32_t ms = tick * 10;
    uint32_t cpu = g_smp_ready ? get_cpu_index() : 0;

    serial_write_on(port, '[');
    
    // Simple 6-digit MS counter
    char time_buf[16];
    int i = 0;
    uint32_t temp = ms;
    if (temp == 0) time_buf[i++] = '0';
    else {
        while (temp > 0 && i < 10) {
            time_buf[i++] = (temp % 10) + '0';
            temp /= 10;
        }
    }
    while (i < 5) time_buf[i++] = '0'; // Padding (5 digits for 99.9s)
    for (int j = i - 1; j >= 0; j--) serial_write_on(port, time_buf[j]);

    serial_print_str_on(port, "ms][C");
    serial_write_on(port, (char)('0' + cpu));
    serial_print_str_on(port, "] ");
}

void serial_write(char c) {
    serial_write_on(COM1, c);
}

void serial_print_on(uint16_t port, const char* str) {
    while (*str) {
        serial_write_on(port, *str++);
    }
}

void serial_log(const char *str) {
  serial_lock_acquire();
  
  uint16_t port = COM1; 
  
  serial_print_prefix(port);
  serial_print_str_on(port, str);
  serial_write_on(port, '\n');
  
  serial_lock_release();
}

void serial_log_hex(const char *label, uint32_t value) {
  serial_lock_acquire();

  uint16_t port = COM1;
  serial_print_prefix(port);
  serial_print_str_on(port, label);

  char hex[] = "0123456789ABCDEF";
  serial_write_on(port, '0');
  serial_write_on(port, 'x');

  for (int i = 28; i >= 0; i -= 4) {
    serial_write_on(port, hex[(value >> i) & 0xF]);
  }
  serial_write_on(port, '\n');

  serial_lock_release();
}

} // extern "C"
