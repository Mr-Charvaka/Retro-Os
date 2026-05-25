#include "include/os/ipc.hpp"
#include "include/os/syscalls.hpp"

extern "C" int main(int argc, char** argv) {
  OS::Syscall::print("DF App: Starting...\n");
  OS::IPCClient app;
  if (!app.connect()) {
    return 1;
  }

  if (!app.create_window("Disk Usage", 400, 300)) {
    return 1;
  }

  uint32_t total, free, block_size;
  if (OS::Syscall::statfs(&total, &free, &block_size) != 0) {
      return 1;
  }

  // Calculate usage
  uint32_t used = total - free;
  float usage_pct = (float)used / (float)total;

  app.fill_rect(0, 0, 400, 300, 0xFF101010);

  // Draw bar background
  app.fill_rect(50, 100, 300, 30, 0xFF303030);

  // Draw usage bar
  int bar_width = (int)(300 * usage_pct);
  app.fill_rect(50, 100, bar_width, 30, 0xFF00FF00);

  app.flush();

  while (true) {
    OS::Syscall::sleep(1000);
  }
  return 0;
}
