#include <stddef.h>
#include <stdint.h>
#include <syscall.h>

extern "C" int main(int argc, char** argv) {
  // 10.0.2.2 in Big-Endian is 0x0202000A
  uint32_t gateway = 0x0202000A;

  syscall_print("PING: Sending ICMP request to 10.0.2.2...\n");
  syscall_print("Check serial log for 'Ping Successful' reply!\n");

  // Call our new ping syscall
  asm volatile("int $0x80" : : "a"(SYS_NET_PING), "b"(gateway));

  syscall_print("Waiting for reply (check serial log)...\n");

  // Simple busy-wait or sleep if available
  for (volatile int i = 0; i < 50000000; i++); // Busy wait

  return 0;
}
