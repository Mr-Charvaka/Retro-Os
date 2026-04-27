#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Master security initialization
void pae_security_init(void);

// Ring 2 container management
void pae_security_tighten_ring2_segments(void);

// Validation for Ring 2 syscalls (INT 0x81)
bool pae_security_validate_ring2_ptr(uint32_t ptr, uint32_t size, bool needs_write);
bool pae_security_validate_syscall(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

// Optional: Integrity monitoring
void pae_security_snapshot_checksums(void);
bool pae_security_verify_integrity(void);

#ifdef __cplusplus
}
#endif
