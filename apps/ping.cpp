#include <stddef.h>
#include <stdint.h>
#include <syscall.h>

extern "C" void _start() {
  // 10.0.2.2 in Big-Endian is 0x0202000A
  uint32_t gateway = 0x0202000A;

  syscall_print("PING: Sending ICMP request to 10.0.2.2...\n");
  syscall_print("Check serial log for 'Ping Successful' reply!\n");

  // Call our new ping syscall
  asm volatile("int $0x80" : : "a"(SYS_NET_PING), "b"(gateway));

  syscall_print("Waiting for reply (check serial log)...\n");

  // Simple busy-wait or sleep if available
  // 50 ticks = 1 second at 50Hz
  uint32_t start = 0; // syscall_get_ticks() not available easily
  for (volatile int i = 0; i < 50000000; i++)
    ; // Busy wait

  syscall_exit(0);
}
