#include "Contracts.h"
#include "include/os/syscalls.hpp"
#include "os_api.h"
#include <stdint.h>
#include <string>
#include <vector>

// For apps, we bridge the contracts to the minimal OS API we already built.

extern "C" {

void kernel_panic(const char *reason) {
  // In userland, we'll just exit for now.
  asm volatile("int $0x80" : : "a"(12), "b"(1)); // SYS_EXIT = 12
  while (1)
    ;
}

FS_ReadResult fs_readdir(const char *path) {
  auto entries = os_fs_readdir(std::string(path));
  static FS_DirEntry static_entries[64];
  uint32_t count = 0;
  for (uint32_t i = 0; i < entries.size(); i++) {
    if (count >= 64)
      break;
    auto &e = entries[i];
    int j = 0;
    const char *name_src = e.name.c_str();
    while (name_src[j] && j < 127) {
      static_entries[count].name[j] = name_src[j];
      j++;
    }
    static_entries[count].name[j] = 0;
    static_entries[count].is_directory = e.is_dir;
    count++;
  }
  return {static_entries, count};
}

bool fs_create_dir(const char *path) { return os_fs_mkdir(std::string(path)); }

bool fs_remove(const char *path) { return os_fs_remove(std::string(path)); }

bool fs_exists(const char *path) { return os_fs_exists(std::string(path)); }

PID spawn_process(const char *path) {
  int pid = OS::Syscall::fork();
  if (pid == 0) {
    const char *argv[] = {path, nullptr};
    OS::Syscall::execve(path, (char **)argv, nullptr);
    OS::Syscall::exit(1);
  }
  return (PID)pid;
}

bool kill_process(PID pid) {
  (void)pid;
  return true;
}

bool ipc_send(PID target, const IPC_Message &msg) {
  (void)target;
  (void)msg;
  return true;
}

bool ipc_receive(IPC_Message &msg) {
  (void)msg;
  return false;
}

void system_self_test() {
  // Apps can call this to verify the environment if they want,
  // but usually it's a kernel-side thing.
}
}
