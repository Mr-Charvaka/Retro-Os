// test.cpp - Simple console test app
// This app prints a message and exits

#include "include/os/syscalls.hpp"

extern "C" void _start() {
  OS::Syscall::print("\n");
  OS::Syscall::print("========================================\n");
  OS::Syscall::print("  Hello, Aman!\n");
  OS::Syscall::print("  The 'run' command is working!\n");
  OS::Syscall::print("  This is TEST.ELF running successfully.\n");
  OS::Syscall::print("  Retro-OS Terminal v2.0 is FULLY WORKING!\n");
  OS::Syscall::print("========================================\n");
  OS::Syscall::print("\n");

  // Just loop forever (or could call exit)
  while (1) {
    OS::Syscall::sleep(1000);
  }
}
