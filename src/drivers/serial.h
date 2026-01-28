#ifndef SERIAL_H
#define SERIAL_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void init_serial();
void serial_write(char c);
void serial_log(const char *str);
void serial_log_hex(const char *label, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
