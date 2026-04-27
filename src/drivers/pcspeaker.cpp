#include "pcspeaker.h"
#include "../drivers/timer.h" // assuming there's a sleep or tick wait
#include "../include/io.h"


void pcspeaker_init() {
  // Nothing special needed for init, but we ensure it's off
  pcspeaker_stop();
}

void pcspeaker_play(uint32_t frequency) {
  if (frequency == 0) {
    pcspeaker_stop();
    return;
  }

  uint32_t div = 1193180 / frequency;

  // Command register (0x43): Use channel 2, square wave mode
  outb(0x43, 0xB6);

  // Data register (0x42): Low byte then high byte
  outb(0x42, (uint8_t)(div & 0xFF));
  outb(0x42, (uint8_t)((div >> 8) & 0xFF));

  // Control register (0x61): Enable bits 0 and 1
  uint8_t tmp = inb(0x61);
  if (tmp != (tmp | 3)) {
    outb(0x61, tmp | 3);
  }
}

void pcspeaker_stop() {
  uint8_t tmp = inb(0x61) & 0xFC;
  outb(0x61, tmp);
}

// Global tick from kernel
extern "C" uint32_t tick;
extern "C" void sleep(
    uint32_t ms); // I'll assume a sleep exists or implement a simple busy wait

void pcspeaker_beep(uint32_t frequency, uint32_t duration_ms) {
  pcspeaker_play(frequency);
  // Note: This block might be tricky if we don't have a reliable sleep yet.
  // For now, let's assume the kernel has a way to wait.
  // In kernel.cpp, it says tick * 20 = ms (50Hz).
  // So 1 ms = 1/20 tick? No, 20ms = 1 tick.

  uint32_t start_tick = tick;
  uint32_t ticks_to_wait = duration_ms / 20;
  while (tick < start_tick + ticks_to_wait) {
    asm volatile("hlt"); // Wait for interrupt
  }

  pcspeaker_stop();
}

void flagship_speaker_pcm(const uint8_t *samples, uint32_t length) {
  // Flagship PCM emulation via PWM on channel 2
  // We manipulate the PIT counter in real-time to simulate a DAC
  for (uint32_t i = 0; i < length; i++) {
    uint8_t sample = samples[i];
    // Scale 8-bit sample (0-255) to PIT frequency range
    uint32_t div = 1193180 / (sample + 200); 
    
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    
    // Minimal delay
    for (volatile int d = 0; d < 500; d++);
  }
}
