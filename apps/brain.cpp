// =============================================================================
// brain.cpp — Ring 3 AI Brain Client for Retro-OS
// =============================================================================

#include "include/libc.h"
#include "include/stdio.h"
#include "include/types.h"
#include "../src/kernel/brain/brain_mailbox.h"

extern "C" void exit(int);

// Local helpers to avoid linking issues with static inlines
static char* my_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for ( ; i < n; i++) dest[i] = '\0';
    return dest;
}

static char* my_itoa(int value, char* str, int base) {
    char *p = str;
    char *p1, *p2;
    unsigned int uvalue = (unsigned int)value;
    if (base == 10 && value < 0) {
        *p++ = '-';
        uvalue = (unsigned int)(-value);
    }
    p1 = p;
    do {
        int digit = uvalue % base;
        *p++ = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        uvalue /= base;
    } while (uvalue);
    *p = 0;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
    return str;
}

void main_brain(int argc, char *argv[]) {
    if (argc < 2) {
        puts("Usage: brain \"<prompt>\" [-t temp] [-m max_tokens]\n");
        exit(1);
    }

    BrainMailbox *mb = brain_get_mailbox();
    if (!mb) {
        puts("ERROR: Cannot access AI Brain mailbox.\n");
        exit(1);
    }

    const char *prompt = argv[1];

    // Wait for IDLE status
    if (mb->status == BRAIN_STATUS_BUSY || mb->status == BRAIN_STATUS_LOADING) {
        puts("Brain is busy. Please wait...\n");
        while (mb->status != BRAIN_STATUS_IDLE && mb->status != BRAIN_STATUS_DONE) {
            for(volatile int i=0; i<100000; i++); // Yield
        }
    }

    // Set Up Command
    for (int i = 0; i < 1024; i++) mb->prompt[i] = 0;
    my_strncpy(mb->prompt, prompt, 1023);
    
    mb->temperature_x100 = 70; // 0.7
    mb->topp_x100 = 90; // Top-p default 0.9
    mb->max_tokens = 64;
    mb->status = BRAIN_STATUS_IDLE; // Reset status
    mb->command = BRAIN_CMD_GENERATE; // TRIGGER!

    fputs("\n[AI BRAIN] Prompt: \"", stdout);
    fputs(prompt, stdout);
    fputs("\"\n", stdout);
    fputs("[AI BRAIN] Generating...\n\n", stdout);

    // Watch status and output
    uint32_t last_len = 0;
    while (mb->status == BRAIN_STATUS_BUSY || mb->command == BRAIN_CMD_GENERATE) {
        uint32_t current_len = strlen(mb->output);
        if (current_len > last_len) {
            // Print only new chars
            fputs(mb->output + last_len, stdout);
            last_len = current_len;
        }
        for(volatile int i=0; i<500000; i++); // Busy wait/Yield
    }

    // Final output flush
    fputs(mb->output + last_len, stdout);
    
    char tbuf[32];
    my_itoa((int)mb->time_ms, tbuf, 10);
    fputs("\n\n[AI BRAIN] Done (", stdout);
    fputs(tbuf, stdout);
    fputs(" ms)\n", stdout);
}

extern "C" int main(int argc, char** argv) {
    main_brain(argc, argv);
    return 0;
}
