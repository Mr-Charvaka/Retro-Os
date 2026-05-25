#ifndef PROCESS_H
#define PROCESS_H

#include "../include/isr.h"
#include "../include/signal.h"
#include "../include/types.h"
#include "apic.h"
#include "../include/vfs.h"
#include "paging.h"

#define MAX_PROCESS_FILES 16
#define DEFAULT_TIME_SLICE 10 // 10 timer ticks (~100ms at 100Hz)
#define DEFAULT_PRIORITY 120  // Linux-like, 0-139 range

// --- GENESIS 2.0: BINARY GENOME ---
typedef struct {
  uint32_t metabolic_efficiency; // 0-100: Reduces instruction gas cost
  uint32_t defensive_posture;    // 0-100: Hardness against scavenge/theft
  uint32_t mutation_rate;        // 0-100: Probability of bit-flips during crossover
  uint32_t sensory_range;       // 0-100: Ability to detect nearby energy sources
  uint64_t signature;           // Unique genetic signature (Hash)
} genome_t;

typedef enum {
  PROCESS_RUNNING,
  PROCESS_READY,
  PROCESS_ZOMBIE,
  PROCESS_WAITING,
  PROCESS_SLEEPING // Sleeping on timer
} process_state_t;

typedef struct file_description {
  vfs_node_t *node;   // The actual VFS node
  uint64_t offset;    // Current seek position (cursor)
  uint32_t flags;     // Open flags (O_RDONLY, etc)
  uint32_t ref_count; // Reference count for fork/dup
} file_description_t;

typedef struct process {
  uint32_t id;               // Process ID
  uint32_t pgid;             // Process Group ID
  uint32_t sid;              // Session ID
  process_state_t state;     // Current state
  uint32_t exit_code;        // Exit code (for ZOMBIE state)
  struct process *parent;    // Parent process
  uint32_t esp;              // Stack Pointer (Kernel Stack)
  uint32_t kernel_stack_top; // Top of kernel stack for TSS
  uintptr_t page_directory;  // Page Directory (Physical Address)
  uint32_t entry_point;      // User mode entry point
  uint32_t user_stack_top;   // Top of user stack
  uint32_t heap_end;         // Current program break (end of heap)
  file_description_t *fd_table[MAX_PROCESS_FILES]; // File Descriptor Table

  // User/Group IDs
  uint32_t uid;  // Real user ID
  uint32_t euid; // Effective user ID
  uint32_t suid; // Saved user ID
  uint32_t gid;  // Real group ID
  uint32_t egid; // Effective group ID
  uint32_t sgid; // Saved group ID

  // Priority scheduling
  int priority;         // 0-139 (lower = higher priority)
  int time_slice;       // Time quantum in ticks
  int time_remaining;   // Remaining ticks before reschedule
  uint32_t sleep_until; // Tick count to wake up (for sleep)

  // Alarm timer
  uint32_t alarm_time; // Tick when SIGALRM should be sent (0 = disabled)

  // Signal handling
  struct sigaction signal_actions[64];
  sigset_t pending_signals;
  sigset_t signal_mask;
  registers_t saved_context; // Context before signal
  int in_signal_handler;     // Recursion protection

  // Resource usage (for times() syscall)
  uint32_t utime;      // User time in ticks
  uint32_t stime;      // System time in ticks
  uint32_t cutime;     // Children user time
  uint32_t cstime;     // Children system time
  uint32_t start_time; // Process start time (tick)

  char cwd[256];    // Current Working Directory
  uint32_t pledges; // Pledges (Bitmask of allowed actions)

  struct unveil_node {
    char path[128];
    uint32_t permissions; // 4=r, 2=w, 1=x, 8=create
    struct unveil_node *next;
  } *unveils;

  struct process *next; // Next process in list
  int pinned_cpu;       // SMP: -1 = any, 0-N = specific core
  uint16_t tica_color;  // TICA Domain Affinity (0x1 = GREEN)
  
  // --- GENESIS 2.0: SOVEREIGN STATE ---
  genome_t genome;      // Physical DNA
  uint64_t energy_bank; // Local metabolic reserve (synced to CPU bank)
} process_t;

// Pledge definitions
#define PLEDGE_STDIO 0x01
#define PLEDGE_RPATH 0x02 // Read files
#define PLEDGE_WPATH 0x04 // Write files
#define PLEDGE_CPATH 0x08 // Create files
#define PLEDGE_EXEC 0x10
#define PLEDGE_PROC 0x20 // Process ops (kill, etc)
#define PLEDGE_INET 0x40 // Network
#define PLEDGE_ALL 0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

extern process_t *current_processes[MAX_CPUS];
#define current_process (current_processes[get_cpu_index()])
extern process_t *ready_queue;

void init_multitasking(uint32_t pd_phys);
void init_ap_scheduling();
process_t *create_kernel_thread(void (*fn)());
void schedule();
extern "C" void kernel_yield();
int create_user_process(const char *filename, char *const argv[]);
int get_pid();
void enter_user_mode();
int fork_process(registers_t *regs);
void exit_process(int status);
int wait_process(int *status);
int sys_waitpid(int pid, int *status, int options);
int sys_waitid(int idtype, int id, void *infop, int options);
int exec_process(registers_t *regs, const char *path, char *const argv[],
                 char *const envp[]);

#ifdef __cplusplus
}
#endif

// Immediate exit (no cleanup)
void sys__exit(int status);

// Alarm timer
uint32_t sys_alarm(uint32_t seconds);

// Atomic and Spinlock helpers for SMP
static inline int atomic_cas(volatile int *ptr, int expected, int desired) {
  int result;
  asm volatile("lock cmpxchgl %2, %1"
               : "=a"(result), "+m"(*ptr)
               : "r"(desired), "0"(expected)
               : "memory");
  return result == expected;
}

static inline void spinlock_lock(volatile int *lock) {
  while (!atomic_cas(lock, 0, 1)) {
    asm volatile("pause");
  }
}

static inline void spinlock_unlock(volatile int *lock) {
  *lock = 0;
}

// IRQ-safe variants
static inline uint32_t spinlock_lock_irq(volatile int *lock) {
    uint32_t flags;
    asm volatile("pushf; cli; pop %0" : "=r"(flags));
    spinlock_lock(lock);
    return flags;
}

static inline void spinlock_unlock_irq(volatile int *lock, uint32_t flags) {
    spinlock_unlock(lock);
    asm volatile("push %0; popf" : : "r"(flags));
}

// Check and deliver alarms (called from timer)
void check_process_alarms(void);

// posix_spawn (simplified fork+exec)
int sys_posix_spawn(int *pid, const char *path, void *file_actions, void *attrp,
                    char *const argv[], char *const envp[]);

#endif
