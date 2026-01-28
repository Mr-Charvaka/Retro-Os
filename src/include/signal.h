#ifndef SIGNAL_H
#define SIGNAL_H

#include "isr.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// POSIX Signal Numbers (1-31 are standard, 32-63 are real-time)
// ============================================================================
#define SIGHUP 1     // Hangup
#define SIGINT 2     // Interrupt (Ctrl+C)
#define SIGQUIT 3    // Quit (Ctrl+\)
#define SIGILL 4     // Illegal instruction
#define SIGTRAP 5    // Trace/breakpoint trap
#define SIGABRT 6    // Abort
#define SIGBUS 7     // Bus error
#define SIGFPE 8     // Floating-point exception
#define SIGKILL 9    // Kill (cannot be caught or ignored)
#define SIGUSR1 10   // User-defined signal 1
#define SIGSEGV 11   // Segmentation fault
#define SIGUSR2 12   // User-defined signal 2
#define SIGPIPE 13   // Broken pipe
#define SIGALRM 14   // Alarm clock
#define SIGTERM 15   // Termination
#define SIGSTKFLT 16 // Stack fault
#define SIGCHLD 17   // Child stopped or terminated
#define SIGCONT 18   // Continue if stopped
#define SIGSTOP 19   // Stop (cannot be caught or ignored)
#define SIGTSTP 20   // Stop from tty (Ctrl+Z)
#define SIGTTIN 21   // Background read from tty
#define SIGTTOU 22   // Background write to tty
#define SIGURG 23    // Urgent data on socket
#define SIGXCPU 24   // CPU time limit exceeded
#define SIGXFSZ 25   // File size limit exceeded
#define SIGVTALRM 26 // Virtual timer expired
#define SIGPROF 27   // Profiling timer expired
#define SIGWINCH 28  // Window size change
#define SIGIO 29     // I/O possible
#define SIGPWR 30    // Power failure
#define SIGSYS 31    // Bad system call

#define NSIG 64 // Total number of signals
#define _NSIG NSIG

// Real-time signals (POSIX.1b)
#define SIGRTMIN 32
#define SIGRTMAX 63

// ============================================================================
// Signal Handler Types
// ============================================================================
typedef void (*sighandler_t)(int);
typedef void (*sigaction_handler_t)(int, void *, void *);

#define SIG_DFL ((sighandler_t)0)    // Default action
#define SIG_IGN ((sighandler_t)1)    // Ignore signal
#define SIG_ERR ((sighandler_t) - 1) // Error return

// ============================================================================
// Signal Set Type - 64-bit bitmask for all signals
// ============================================================================
typedef uint64_t sigset_t;

// ============================================================================
// siginfo_t - Signal information structure (simplified)
// ============================================================================
typedef struct siginfo {
  int si_signo;  // Signal number
  int si_errno;  // Error number
  int si_code;   // Signal code
  int si_pid;    // Sending process ID
  int si_uid;    // Real user ID of sender
  int si_status; // Exit value or signal
  void *si_addr; // Memory location causing fault
  int si_value;  // Signal value (union sigval)
} siginfo_t;

// si_code values for SIGCHLD
#define CLD_EXITED 1    // Child exited
#define CLD_KILLED 2    // Child killed
#define CLD_DUMPED 3    // Child terminated abnormally
#define CLD_TRAPPED 4   // Traced child trapped
#define CLD_STOPPED 5   // Child stopped
#define CLD_CONTINUED 6 // Child continued

// si_code values for SIGSEGV
#define SEGV_MAPERR 1 // Address not mapped
#define SEGV_ACCERR 2 // Invalid permissions

// si_code values for SIGBUS
#define BUS_ADRALN 1 // Invalid address alignment
#define BUS_ADRERR 2 // Non-existent physical address
#define BUS_OBJERR 3 // Object-specific error

// ============================================================================
// sigaction structure - Detailed signal handling
// ============================================================================
struct sigaction {
  union {
    sighandler_t sa_handler;          // Simple handler
    sigaction_handler_t sa_sigaction; // Extended handler (with siginfo)
  };
  sigset_t sa_mask;          // Signals to block during handler
  int sa_flags;              // Action flags
  void (*sa_restorer)(void); // Signal trampoline (internal)
};

// sa_flags values
#define SA_NOCLDSTOP 0x00000001 // Don't send SIGCHLD when children stop
#define SA_NOCLDWAIT 0x00000002 // Don't create zombie on child death
#define SA_SIGINFO 0x00000004   // Use sa_sigaction, not sa_handler
#define SA_ONSTACK 0x08000000   // Use alternate signal stack
#define SA_RESTART 0x10000000   // Restart syscall on signal return
#define SA_NODEFER 0x40000000   // Don't block signal in its handler
#define SA_RESETHAND 0x80000000 // Reset to SIG_DFL on entry

// For compatibility
#define SA_NOMASK SA_NODEFER
#define SA_ONESHOT SA_RESETHAND

// ============================================================================
// sigprocmask() 'how' values
// ============================================================================
#define SIG_BLOCK 0   // Block signals in 'set'
#define SIG_UNBLOCK 1 // Unblock signals in 'set'
#define SIG_SETMASK 2 // Set mask to 'set'

// ============================================================================
// Signal Set Manipulation Functions (implemented in signal.cpp)
// ============================================================================
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

// ============================================================================
// Kernel System Calls
// ============================================================================
int sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);
int sigwait(const sigset_t *set, int *sig);
int kill(int pid, int signum);
unsigned int alarm(unsigned int seconds);
int pause(void);
int abort(void);

// Kernel syscall implementations
int sys_sigaction(int signum, const struct sigaction *act,
                  struct sigaction *oldact);
int sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sys_sigpending(sigset_t *set);
int sys_sigsuspend(const sigset_t *mask);
int sys_sigwait(const sigset_t *set, int *sig);
int sys_sigtimedwait(const sigset_t *set, siginfo_t *info, const void *timeout);
int sys_kill(int pid, int signum);
int sys_signal(int signum, sighandler_t handler);
int sys_pause(void);
int sys_abort(void);

// Internal kernel functions
int kernel_sigreturn(registers_t *regs);
void handle_signals(registers_t *regs);
void signal_init(void);

#ifdef __cplusplus
}
#endif

#endif // SIGNAL_H
