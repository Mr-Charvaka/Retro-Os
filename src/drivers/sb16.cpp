#include "sb16.h"
#include "../include/io.h"
#include "../include/irq.h"
#include "../include/isr.h"
#include "../kernel/paging.h"
#include "serial.h"


// SB16 State
static volatile int g_audio_in_use = 0;
static volatile int g_audio_buffer_index = 1; // 0 or 1 for double buffering
bool g_sb16_present = false;

// Hardware Delay
static void sb16_delay() {
  for (volatile int i = 0; i < 1000; i++)
    ;
}

// DSP Helpers
void sb16_write(uint8_t data) {
  while (inb(SB_DSP_WRITE) & 0x80)
    ;
  outb(SB_DSP_WRITE, data);
}

static uint8_t sb16_read() {
  uint32_t timeout = 10000;
  while (!(inb(SB_DSP_READ + 4) & 0x80) && timeout--) // 0x22E is Poll
    ;
  return inb(SB_DSP_READ);
}

static bool sb16_reset() {
  outb(SB_DSP_RESET, 1);
  sb16_delay();
  outb(SB_DSP_RESET, 0);
  sb16_delay();

  if (sb16_read() == 0xAA) {
    return true;
  }
  return false;
}

// Interrupt Handler for SB16 (IRQ 5)
extern "C" void sb16_interrupt_handler(registers_t *regs) {
  (void)regs;
  // Acknowledge 8-bit interrupt
  inb(SB_DSP_INT_ACK);
  // Acknowledge 16-bit interrupt
  inb(SB_DSP_INT16_ACK);

  // Toggle buffer index for the user-mode player
  g_audio_buffer_index = !g_audio_buffer_index;
}

// DMA Setup for Channel 5 (16-bit)
static void dma_setup_channel5(uint32_t phys_addr, uint32_t length_in_words) {
  uint32_t length = length_in_words - 1;

  outb(0xD4, 5);    // Mask channel 5 (4 + 1)
  outb(0xD8, 0);    // Reset Flip-Flop
  outb(0xD6, 0x59); // Mode: Auto-init, Address Increment, Read (Memory -> SB16)

  // Address (Port 0xC4, unit is words, so shift 1)
  uint32_t word_addr = phys_addr >> 1;
  outb(0xC4, (uint8_t)(word_addr & 0xFF));
  outb(0xC4, (uint8_t)((word_addr >> 8) & 0xFF));
  outb(0x8B, (uint8_t)((phys_addr >> 16) & 0xFF)); // Page (0x8B for Ch 5)

  // Count (Port 0xC6, unit is words)
  outb(0xC6, (uint8_t)(length & 0xFF));
  outb(0xC6, (uint8_t)((length >> 8) & 0xFF));

  outb(0xD4, 1); // Unmask channel 5 (0 + 1)
}

void sb16_init() {
  if (sb16_reset()) {
    serial_log("SB16: Sound Blaster 16 Detected!");
    g_sb16_present = true;
    sb16_write(SB_CMD_SPEAKER_ON);

    // Register IRQ 5 handler
    // register_interrupt_handler(IRQ5, sb16_interrupt_handler);
  } else {
    serial_log("SB16: Sound Blaster 16 NOT found.");
  }
}

void sb16_play_16bit(int16_t *buffer, uint32_t length_bytes,
                     uint16_t sample_rate) {
  uint32_t phys = VIRT_TO_PHYS(buffer);
  uint32_t word_count = length_bytes / 2;

  // 1. Setup DMA Channel 5
  dma_setup_channel5(phys, word_count);

  // 2. Setup DSP Sample Rate
  sb16_write(SB_CMD_SET_SAMPLE_RATE);
  sb16_write((uint8_t)(sample_rate >> 8));
  sb16_write((uint8_t)(sample_rate & 0xFF));

  // 3. Setup Block Size for interrupts
  // We want interrupts every half-buffer for double buffering
  uint32_t block_words = (word_count / 2) - 1;
  sb16_write(0x48); // Set block size
  sb16_write((uint8_t)(block_words & 0xFF));
  sb16_write((uint8_t)((block_words >> 8) & 0xFF));

  // 4. Start 16-bit Auto-init DMA (0xB6)
  sb16_write(0xB6); // 16-bit Output
  sb16_write(0x30); // 16-bit High-speed Auto-init
}

extern "C" int32_t sys_audio_cstatus() { return g_audio_buffer_index; }

extern "C" void sb16_play_8bit(uint8_t *buffer, uint32_t length,
                               uint16_t sample_rate) {
  uint32_t phys = VIRT_TO_PHYS(buffer);

  // Setup DMA 1 (Existing logic)
  outb(0x0A, 5);
  outb(0x0C, 0);
  outb(0x0B, 0x49);
  outb(0x02, (uint8_t)(phys & 0xFF));
  outb(0x02, (uint8_t)((phys >> 8) & 0xFF));
  outb(0x83, (uint8_t)((phys >> 16) & 0xFF));
  outb(0x03, (uint8_t)((length - 1) & 0xFF));
  outb(0x03, (uint8_t)(((length - 1) >> 8) & 0xFF));
  outb(0x0A, 1);

  sb16_write(SB_CMD_SET_SAMPLE_RATE);
  sb16_write((uint8_t)(sample_rate >> 8));
  sb16_write((uint8_t)(sample_rate & 0xFF));
  sb16_write(0x1C);
}
