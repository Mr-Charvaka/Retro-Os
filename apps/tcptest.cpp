// tcptest.cpp - TCP Connection Test Application
// Tests the TCP stack by connecting to an HTTP server

#include <stdint.h>

// Syscalls inline
static inline void syscall_print(const char *s) {
  asm volatile("int $0x80" ::"a"(0), "b"(s));
}

static inline void syscall_exit(int code) {
  asm volatile("int $0x80" ::"a"(0x0C), "b"(code));
}

// Syscall to test TCP connection
static inline int syscall_tcp_test() {
  int result;
  asm volatile("int $0x80" : "=a"(result) : "a"(0x9C)); // Syscall 156
  return result;
}

extern "C" void _start() {
  syscall_print("TCP Test: Starting connection test...\n");
  syscall_print("Attempting to connect to example.com (93.184.216.34:80)\n");

  int result = syscall_tcp_test();

  if (result == 0) {
    syscall_print("TCP: Connection test initiated!\n");
    syscall_print("Check serial log for TCP handshake...\n");
  } else {
    syscall_print("TCP: Test failed to start\n");
  }

  // Wait a bit for TCP handshake to complete
  syscall_print("Waiting for response (check serial log)...\n");
  for (volatile int i = 0; i < 100000000; i++)
    ;

  syscall_exit(0);
}
