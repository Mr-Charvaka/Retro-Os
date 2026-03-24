#ifndef TSC_H
#define TSC_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t rdtsc();
void tsc_calibrate();
void tsc_delay_ns(uint64_t ns);

#ifdef __cplusplus
}
#endif

#endif
