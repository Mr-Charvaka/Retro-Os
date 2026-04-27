#include "include/syscall.h"

/* Minimalistic Shell using raw syscalls only */

// Simple strlen since we aren't using libc
static int my_strlen(const char* s) {
    int l = 0;
    while (s && s[l]) l++;
    return l;
}

// Simple strcmp
static int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Helper for print
static void print(const char* msg) {
    syscall_print(msg);
}

extern "C" int main(int argc, char** argv) {
    char cmd[1024];
    char* args[16];
    char path[256];
    
    print("\n[SHELL] Interactive mode started (Zero-Libc)\n");
    
    while (1) {
        print("Retro-OS> ");
        
        // Read prompt
        int n = syscall_read(0, cmd, 1023);
        if (n <= 0) continue;
        
        // Null terminate and strip newline
        cmd[n] = 0;
        if (n > 0 && cmd[n-1] == '\n') cmd[n-1] = 0;
        if (n > 1 && cmd[n-2] == '\r') cmd[n-2] = 0; // Handle CRLF
        
        if (my_strlen(cmd) == 0) continue;
        
        // Simple command handling: 'exit'
        if (my_strcmp(cmd, "exit") == 0) break;
        
        // Fork and Exec
        int pid = syscall_fork();
        if (pid == 0) {
            // Child
            // Simple path resolution (assume /C/)
            if (cmd[0] != '/') {
                path[0] = '/';
                path[1] = 'C';
                path[2] = '/';
                int i = 0;
                while (cmd[i]) {
                    path[i+3] = cmd[i];
                    i++;
                }
                path[i+3] = 0;
            } else {
                int i = 0;
                while (cmd[i]) {
                    path[i] = cmd[i];
                    i++;
                }
                path[i] = 0;
            }
            
            char* argv_child[] = {path, (char*)0};
            char* env_child[] = {(char*)0};
            
            syscall_execve(path, argv_child, env_child);
            
            // If we are here, exec failed
            print("sh: command not found: ");
            print(cmd);
            print("\n");
            syscall_exit(1);
        } else if (pid > 0) {
            // Parent: Wait
            int status;
            syscall_wait(&status);
        } else {
            print("sh: fork failed\n");
        }
    }
    
    print("sh: exiting...\n");
    return 0;
}
