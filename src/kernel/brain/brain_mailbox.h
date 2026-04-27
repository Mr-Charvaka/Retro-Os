// =============================================================================
// brain_mailbox.h — Shared mailbox for Ring 3 ↔ Ring 2 communication
// =============================================================================

#ifndef BRAIN_MAILBOX_H
#define BRAIN_MAILBOX_H

#include <stdint.h>

// Mailbox lives at a fixed virtual address accessible from Ring 2 and Ring 3
// We place it at 0xD2800000 (safe distance from Ring 2 stack)
#define BRAIN_MAILBOX_VIRT  0xD2800000

// Commands
#define BRAIN_CMD_NONE      0x00
#define BRAIN_CMD_GENERATE  0x01
#define BRAIN_CMD_STATUS    0x02
#define BRAIN_CMD_SHUTDOWN  0xFF

// Status
#define BRAIN_STATUS_IDLE       0x00
#define BRAIN_STATUS_LOADING    0x01
#define BRAIN_STATUS_BUSY       0x02
#define BRAIN_STATUS_DONE       0x03
#define BRAIN_STATUS_ERROR      0x04

struct BrainMailbox {
    volatile uint32_t command;        // Ring 3 writes command here
    volatile uint32_t status;         // Ring 2 writes status here
    volatile uint32_t max_tokens;     // Generation parameter
    volatile uint32_t temperature_x100; // Temperature * 100 (integer, no FP in mailbox)
    volatile uint32_t topp_x100;      // Top-p * 100
    volatile uint32_t tokens_generated;
    volatile uint32_t time_ms;
    char prompt[1024];                // Ring 3 writes prompt here
    char output[4096];                // Ring 2 writes output here (for display)
};

// Get pointer to the mailbox (same address in all rings)
#ifdef __cplusplus
static inline BrainMailbox* brain_get_mailbox(void) {
    return (BrainMailbox *)BRAIN_MAILBOX_VIRT;
}
#endif

#endif // BRAIN_MAILBOX_H
