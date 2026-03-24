#ifndef PCSPEAKER_H
#define PCSPEAKER_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void pcspeaker_init();
void pcspeaker_play(uint32_t frequency);
void pcspeaker_stop();
void pcspeaker_beep(uint32_t frequency, uint32_t duration_ms);
void flagship_speaker_pcm(const uint8_t *samples, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
