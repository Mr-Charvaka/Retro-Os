// init.cpp — Retro-OS init process (Ring 3)
// Compiled with -fno-pic -fno-pie — NO GOT access

#include <stdint.h>

/* SYSCALL MAPPING (from kernel/syscall.cpp) */
#define SYS_PRINT   0
#define SYS_WRITE   4
#define SYS_FORK    9
#define SYS_EXECVE  10
#define SYS_WAIT    11
#define SYS_EXIT    12
#define SYS_SLEEP   34
#define SYS_YIELD   11 // Wait, SYS_WAIT is 11. Is there a yield? 
                       // Looking at syscall.cpp, there is no explicit yield
                       // but sleep(0) or just wait() can act as one.
                       // Actually, syscall.cpp line 1391: sys_wait is 11.
                       // I'll use wait(-1) for reaping.

namespace OS {
namespace Syscall {

static inline int32_t syscall1(int num, uint32_t a1) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
    return ret;
}

static inline int32_t syscall3(int num, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    // Use SYS_WRITE (4) to stdout (1)
    syscall3(SYS_WRITE, 1, (uint32_t)s, (uint32_t)len);
}

static void sleep(uint32_t ms) {
    // Note: Kernel sleep is in ticks. At 50Hz, 1 tick = 20ms.
    // So ticks = ms / 20.
    syscall1(SYS_SLEEP, ms / 20);
}

static int exec(const char *path) {
    return syscall3(SYS_EXECVE, (uint32_t)path, 0, 0);
}

static int fork() {
    int32_t ret;
    asm volatile("movl %1, %%eax; int $0x80" : "=a"(ret) : "i"(SYS_FORK) : "memory");
    return ret;
}

static void exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    for(;;);
}

static int waitpid(int pid) {
    // Syscall 66 is SYS_WAITPID(pid, status*, options)
    return syscall3(66, (uint32_t)pid, 0, 0);
}

} // namespace Syscall
} // namespace OS

using namespace OS::Syscall;

static int spawn(const char *path) {
    int pid = fork();
    if (pid == 0) {
        int r = exec(path);
        if (r < 0) {
            print("INIT ERROR: Exec failed for ");
            print(path);
            print("\n");
            exit(127);
        }
    }
    return pid;
}

extern "C" int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    print("[INIT] Retro-OS Init Starting (Clean non-PIC)...\n");

    print("[INIT] Launching Terminal...\n");
    spawn("/C/TERMINAL.ELF");
    sleep(1000); // Wait 1s for ELF loading to avoid ATA contention

    print("[INIT] Launching Shell...\n");
    spawn("/C/SH.ELF");
    sleep(500);

    print("[INIT] All services launched.\n");

    // PID 1 reap loop — never exit
    for (;;) {
        waitpid(-1);
        // Simple yield substitute
        sleep(10);
    }

    return 0;
}
