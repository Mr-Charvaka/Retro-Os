#ifndef PE_LOADER_H
#define PE_LOADER_H

#include <stdint.h>

uint32_t load_pe(const char *filename, uint32_t *top_address);

#endif
