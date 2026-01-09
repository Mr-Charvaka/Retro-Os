#ifndef E1000_H
#define E1000_H

#include <stdint.h>

extern "C" void e1000_init(uint8_t bus, uint8_t slot, uint8_t func);
extern "C" void e1000_send(void *data, uint16_t length);
extern "C" int e1000_receive(uint8_t *out);

#endif
