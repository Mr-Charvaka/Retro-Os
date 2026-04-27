#ifndef SERIAL_H
#define SERIAL_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

void init_serial();
void serial_init_port(uint16_t port);
void serial_write(char c);
void serial_write_on(uint16_t port, char c);
void serial_log(const char *str);
void serial_log_hex(const char *label, uint32_t value);
void serial_print_on(uint16_t port, const char* str);

#ifdef __cplusplus
}
#endif

#endif
