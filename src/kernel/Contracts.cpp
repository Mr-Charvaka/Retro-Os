#include "Contracts.h"
#include "../drivers/serial.h"
#include "heap.h"
#include "process.h"
#include "string.h"
#include "vfs.h"

extern uint32_t next_pid;

extern "C" {

void kernel_panic(const char *reason) {
  serial_log("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  serial_log("!!!                  KERNEL PANIC                      !!!");
  serial_log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  serial_log(reason);
  serial_log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  asm volatile("cli");
  while (true) {
    asm volatile("hlt");
  }
}

FS_ReadResult fs_readdir(const char *path) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node || (node->flags & 0x0F) != VFS_DIRECTORY) {
    return {nullptr, 0};
  }

  static FS_DirEntry static_entries[32];
  uint32_t count = 0;

  struct dirent *de = nullptr;
  while (count < 32) {
    de = readdir_vfs(node, count);
    if (!de)
      break;

    int i = 0;
    while (de->d_name[i] && i < 127) {
      static_entries[count].name[i] = de->d_name[i];
      i++;
    }
    static_entries[count].name[i] = 0;
    static_entries[count].is_directory = (de->d_type == VFS_DIRECTORY);
    count++;
  }

  return {static_entries, count};
}

bool fs_create_dir(const char *path) { return vfs_mkdir(path, 0) == 0; }

bool fs_remove(const char *path) { return vfs_unlink(path) == 0; }

bool fs_exists(const char *path) {
  vfs_node_t *node = vfs_resolve_path(path);
  return node != nullptr;
}

PID spawn_process(const char *path) {
  uint32_t pid_before = next_pid;
  create_user_process(path, nullptr);
  if (next_pid > pid_before) {
    return (PID)pid_before;
  }
  return -1;
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
  serial_log("CONTRACT: Beginning System Self-Test...");

  CONTRACT_ASSERT(fs_exists("/"), "FS: root missing");

  // Test process spawning with a known ELF
  PID hello = spawn_process("HELLO.ELF");
  CONTRACT_ASSERT(hello >= 0, "Process: cannot spawn hello");

  // Test filesystem persistence laws
  CONTRACT_ASSERT(fs_create_dir("/CON_TEST"), "FS: mkdir failed");
  CONTRACT_ASSERT(fs_exists("/CON_TEST"), "FS: persistence failure");

  fs_remove("/CON_TEST");
  serial_log("CONTRACT: System Self-Test Passed.");
}
}
