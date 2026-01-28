#include <os/syscalls.hpp>

extern "C" void _start() {
  OS::Syscall::print("Minimal OS Init Started\n");
  while (1) {
    OS::Syscall::sleep(100);
  }
}
