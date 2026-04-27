#include <stdint.h>

extern "C" int main(int argc, char** argv) {
    // SECURITY TEST: Trigger INT 0x81 from Ring 3.
    // This MUST cause a General Protection Fault (#GP) because
    // the IDT gate DPL is 2, while we are CPL 3.
    
    // Using inline asm to avoid any library dependencies
    unsigned int result;
    __asm__ volatile (
        "mov $0x00, %%eax\n"  // BRAIN_SYS_VERIFY_RING
        "int $0x81\n"
        : "=a"(result)
        :
        : "memory"
    );

    // If we get here, security is broken!
    // We should never reach this line.
    for(;;);
    return 0;
}
