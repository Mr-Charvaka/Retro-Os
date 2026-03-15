#ifndef SB16_H
#define SB16_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// DSP Ports (Base 0x220)
#define SB_DSP_RESET 0x226
#define SB_DSP_READ 0x22A
#define SB_DSP_WRITE 0x22C
#define SB_DSP_STATUS 0x22C
#define SB_DSP_INT_ACK 0x22E
#define SB_DSP_INT16_ACK 0x22F

// DSP Commands
#define SB_CMD_SET_TIME_CONSTANT 0x40
#define SB_CMD_SET_SAMPLE_RATE 0x41
#define SB_CMD_SPEAKER_ON 0xD1
#define SB_CMD_SPEAKER_OFF 0xD3
#define SB_CMD_STOP_DMA_8 0xD0
#define SB_CMD_STOP_DMA_16 0xD5
#define SB_CMD_RESUME_DMA_8 0xD4
#define SB_CMD_RESUME_DMA_16 0xD6

void sb16_init();
void sb16_play_8bit(uint8_t *buffer, uint32_t length, uint16_t sample_rate);
void sb16_play_16bit(int16_t *buffer, uint32_t length, uint16_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif
