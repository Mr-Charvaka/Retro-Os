#include "include/libc.h"
#include "include/syscall.h"

extern "C" int main() {
    syscall_print("=== Retro-OS Audio Demo ===\n");
    syscall_print("Targeting Sound Blaster 16 (Detected in Boot Log)\n");
    
    // Create a 1-second buffer of 8-bit PCM (sawtooth wave)
    uint32_t sample_rate = 22050;
    uint32_t len = sample_rate; // 1 second
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        syscall_print("Malloc failed!\n");
        return 1;
    }
    
    syscall_print("Generating 8-bit Sawtooth Pattern...\n");
    for (uint32_t i = 0; i < len; i++) {
        // Generate a 440Hz sawtooth wave manually
        // 22050 / 440 = 50 samples per cycle
        buf[i] = (uint8_t)((i % 50) * 5); 
    }
    
    syscall_print("Playing via SB16 (Syscall 136)...\n");
    // EBX=buffer, ECX=length
    asm volatile("int $0x80" : : "a"(136), "b"(buf), "c"(len));
    
    // Hold for a bit
    for(volatile int i=0; i<1000000; i++);

    syscall_print("Generating Square Wave Pattern...\n");
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (i % 100 < 50) ? 200 : 50;
    }
    
    syscall_print("Playing square wave...\n");
    asm volatile("int $0x80" : : "a"(136), "b"(buf), "c"(len));
    
    syscall_print("Demo complete.\n");
    free(buf);
    return 0;
}
