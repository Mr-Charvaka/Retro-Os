#ifndef SYS_WAIT_H
#define SYS_WAIT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Wait Status Macros
// ============================================================================

// Extract exit status from wait status (bits 8-15)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)

// Check if child terminated normally
#define WIFEXITED(status) (((status) & 0x7F) == 0)

// Extract signal number if terminated by signal
#define WTERMSIG(status) ((status) & 0x7F)

// Check if child was terminated by signal
#define WIFSIGNALED(status)                                                    \
  (((status) & 0x7F) != 0 && ((status) & 0x7F) != 0x7F)

// Extract stop signal
#define WSTOPSIG(status) WEXITSTATUS(status)

// Check if child is stopped
#define WIFSTOPPED(status) (((status) & 0xFF) == 0x7F)

// Check if child was continued
#define WIFCONTINUED(status) ((status) == 0xFFFF)

// Core dump flag
#define WCOREDUMP(status) ((status) & 0x80)

// ============================================================================
// Wait Options for waitpid() and waitid()
// ============================================================================
#define WNOHANG 0x00000001    // Don't block if no child has exited
#define WUNTRACED 0x00000002  // Also return for stopped children
#define WCONTINUED 0x00000008 // Also return for continued children
#define WSTOPPED WUNTRACED    // Alias

// For waitid()
#define WEXITED 0x00000004 // Wait for exited children
#define WNOWAIT 0x01000000 // Don't reap, just poll status

// ============================================================================
// idtype_t for waitid()
// ============================================================================
typedef enum {
  P_ALL = 0, // Wait for any child
  P_PID = 1, // Wait for specific PID
  P_PGID = 2 // Wait for any child in process group
} idtype_t;

// ============================================================================
// siginfo_t subset for waitid() - uses the full siginfo_t from signal.h
// ============================================================================

// ============================================================================
// Wait Functions (Kernel syscalls)
// ============================================================================
int sys_wait(int *status);
int sys_waitpid(int pid, int *status, int options);
int sys_waitid(int idtype, int id, void *infop, int options);

// Userspace wrappers (for libc)
int wait(int *status);
int waitpid(int pid, int *status, int options);
int waitid(idtype_t idtype, int id, void *infop, int options);

#ifdef __cplusplus
}
#endif

#endif // SYS_WAIT_H
