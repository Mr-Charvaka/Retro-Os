// =================================================================
// brain_gate.h — Ring 3 API for calling the Ring 2 Brain
// =================================================================
// User-space (Ring 3) code includes this header to communicate
// with the Ring 2 AI Brain service via the hardware call gate.
//
// Usage:
//   uint32_t result = brain_call(BRAIN_REQ_PING, 0);
//   if (result == 0x0052494E) { /* Brain is alive */ }
//
// The lcall instruction triggers the CPU's call gate mechanism,
// which handles all privilege transitions automatically.
// =================================================================

#ifndef BRAIN_GATE_H
#define BRAIN_GATE_H

#include "memory_map.h"

// Request type constants (must match ring2.h definitions)
#define BRAIN_REQ_PING      0x00
#define BRAIN_REQ_ADD_ONE   0x01
#define BRAIN_REQ_GET_CPL   0x02
#define BRAIN_REQ_ECHO      0x03

// =================================================================
// brain_call — Send a request to the Ring 2 Brain via Call Gate
// =================================================================
//
// Parameters:
//   request_type: What to ask the Brain to do (BRAIN_REQ_*)
//   data_ptr:     Pointer to request data (or 0 if none)
//
// Returns:
//   Result from Ring 2 handler (in EAX)
//
// CPU operation when lcall executes:
//   1. Read call gate from GDT[8]
//   2. Check CPL(3) ≤ Gate DPL(3) → allowed
//   3. Switch to Ring 2 stack (TSS.esp2 / TSS.ss2)
//   4. Push Ring 3's SS, ESP onto Ring 2 stack
//   5. Copy 2 dwords from Ring 3 stack to Ring 2 stack
//   6. Push Ring 3's CS, EIP onto Ring 2 stack
//   7. Load CS = Ring 2 code, EIP = handler address
//   8. CPL becomes 2
//   ... handler executes, puts result in EAX ...
//   9. RETF $8 pops CS:EIP, cleans params, pops SS:ESP
//   10. CPL becomes 3 again, result in EAX
//
static inline uint32_t brain_call(uint32_t request_type, uint32_t data_ptr) {
    uint32_t result;

    // lcall requires a 6-byte far pointer: 4 bytes offset + 2 bytes selector.
    // For call gates, the offset is IGNORED by the CPU (the gate provides
    // the real target address). We set it to 0.
    //
    // Stack layout before lcall:
    //   [ESP+4]  data_ptr      ← param 2 (CPU copies this)
    //   [ESP+0]  request_type  ← param 1 (CPU copies this)
    //
    // After lcall returns, result is in EAX.
    // We must clean up the 2 params we pushed (8 bytes).
    //
    // NOTE: The "lcall" with a far pointer in inline asm uses
    // the form: lcall $selector, $offset
    // Since this is AT&T syntax in GCC inline asm:
    //   "lcall $0x43, $0x0"
    //   0x43 = BRAIN_GATE_SEL = (8*8)|3

    asm volatile(
        "pushl %2          \n\t"   // Push data_ptr (param 2)
        "pushl %1          \n\t"   // Push request_type (param 1)
        "lcall $0x43, $0x0 \n\t"   // Far call through gate selector 0x43
        "addl $8, %%esp    \n\t"   // Clean up 2 pushed params
        : "=a" (result)            // Output: EAX = result
        : "r" (request_type),      // Input: request_type
          "r" (data_ptr)           // Input: data_ptr
        : "memory", "ecx", "edx"  // Clobbers: memory + scratch regs
    );

    return result;
}

#endif // BRAIN_GATE_H
