#ifndef AC97_H
#define AC97_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ac97_init();
void ac97_play(uint32_t phys_addr, uint32_t count);
bool ac97_is_playing();

#ifdef __cplusplus
}
#endif

#endif
