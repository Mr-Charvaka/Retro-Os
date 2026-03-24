#include <stddef.h>
#include <stdint.h>
#include <syscall.h>

extern "C" void _start() {
  syscall_print("=== Retro-OS Network Test Suite ===\n");

  // 1. Check Link / DHCP Status
  syscall_print("Step 1: Checking DHCP status...\n");
  uint32_t status = 0;
  asm volatile("int $0x80" : "=a"(status) : "a"(SYS_NET_STATUS));

  if (status == 1) {
    syscall_print("  [OK] DHCP configured successfully.\n");
  } else {
    syscall_print(
        "  [FAIL] DHCP not configured yet. Wait 5s or check serial logs.\n");
  }

  // 2. Resolve a domain (DNS test)
  syscall_print("\nStep 2: Testing DNS (Resolving neverssl.com)...\n");
  const char *domain = "neverssl.com";
  uint32_t resolved_ip = 0;
  asm volatile("int $0x80"
               : "=a"(resolved_ip)
               : "a"(157), "b"(domain)); // SYS_DNS_RESOLVE = 157

  if (resolved_ip != 0) {
    syscall_print("  [OK] DNS Resolved neverssl.com to IP: ");
    // Simple hex print as we don't have ip_ntoa easily in userland here
    // (Should be 13.227.2.148 etc)
    // For simplicity, just say OK
    syscall_print("SUCCESS\n");
  } else {
    syscall_print("  [FAIL] DNS Resolution failed.\n");
  }

  // 3. Ping the gateway
  syscall_print("\nStep 3: Pinging Gateway (10.0.2.2)...\n");
  uint32_t gateway = 0x0202000A;
  asm volatile("int $0x80" : : "a"(155), "b"(gateway)); // SYS_NET_PING = 155
  syscall_print("  [DONE] Ping request sent. Watch serial log for response.\n");

  syscall_print("\n=== Test Suite Complete ===\n");
  syscall_exit(0);
}
