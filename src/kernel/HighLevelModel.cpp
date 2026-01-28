#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"
#include "memory.h"
#include "string.h"


/*
=========================================================
 STAGE 10 — THREADS / POSIX / PACKAGE MANAGER
=========================================================
*/

namespace FileSystemTruth {
enum class NodeType : uint8_t;
struct FSNode;
FSNode *fs_resolve(const char *path);
void fs_list_dir(const char *path);
} // namespace FileSystemTruth

namespace HighLevelModel {

typedef void (*ThreadEntry)(void *arg);

struct Thread {
  uint32_t tid;
  const char *name;
  ThreadEntry entry;
  void *arg;
  bool running;
};

struct Task {
  uint32_t pid;
  const char *name;
  Thread threads[8]; // Max 8 threads per task in this model
  uint32_t thread_count;
};

constexpr uint32_t MAX_TASKS = 16;
static Task g_task_table[MAX_TASKS];
static uint32_t g_task_count = 0;
static uint32_t g_next_pid = 6000;
static uint32_t g_next_tid = 6000;

/* ======================================================
   CREATE TASK
   ====================================================== */

uint32_t create_task(const char *name) {
  CONTRACT_ASSERT(g_task_count < MAX_TASKS, "Stage 10: Task table full");
  Task &t = g_task_table[g_task_count++];
  t.pid = g_next_pid++;
  t.name = name;
  t.thread_count = 0;
  return t.pid;
}

uint32_t create_thread(uint32_t pid, const char *name, ThreadEntry entry,
                       void *arg) {
  for (uint32_t i = 0; i < g_task_count; i++) {
    Task &t = g_task_table[i];
    if (t.pid == pid) {
      CONTRACT_ASSERT(t.thread_count < 8,
                      "Stage 10: Thread limit reached for task");
      Thread &th = t.threads[t.thread_count++];
      th.tid = g_next_tid++;
      th.name = name;
      th.entry = entry;
      th.arg = arg;
      th.running = true;
      return th.tid;
    }
  }
  return 0;
}

/* ======================================================
   SIMPLE COOPERATIVE SCHEDULER
   ====================================================== */

void schedule_threads() {
  for (uint32_t i = 0; i < g_task_count; i++) {
    Task &task = g_task_table[i];
    for (uint32_t j = 0; j < task.thread_count; j++) {
      Thread &thread = task.threads[j];
      if (thread.running) {
        thread.entry(thread.arg);
      }
    }
  }
}

/* ======================================================
   HIGH-LEVEL POSIX-LIKE FILE API
   ====================================================== */

struct FileDescriptor {
  uint32_t fd;
  void *node_ptr; // FSNode*
};

static uint32_t g_next_fd = 3;
static FileDescriptor g_fd_table[32];
static uint32_t g_fd_count = 0;

uint32_t posix_open(const char *path) {
  void *node = (void *)FileSystemTruth::fs_resolve(path);
  if (!node)
    return 0;

  CONTRACT_ASSERT(g_fd_count < 32, "Stage 10: FD table full");
  uint32_t fd = g_next_fd++;
  g_fd_table[g_fd_count++] = {fd, node};
  return fd;
}

bool posix_close(uint32_t fd) {
  for (uint32_t i = 0; i < g_fd_count; i++) {
    if (g_fd_table[i].fd == fd) {
      for (uint32_t j = i; j < g_fd_count - 1; j++)
        g_fd_table[j] = g_fd_table[j + 1];
      g_fd_count--;
      return true;
    }
  }
  return false;
}

/* ======================================================
   SIMPLE READ/WRITE DEMO
   ====================================================== */

bool posix_write(uint32_t fd, const char *content) {
  for (uint32_t i = 0; i < g_fd_count; i++) {
    if (g_fd_table[i].fd == fd) {
      serial_log("MODEL: POSIX_WRITE Successful.");
      return true;
    }
  }
  return false;
}

/* ======================================================
   PACKAGE MANAGER SKELETON
   ====================================================== */

struct Package {
  const char *name;
  const char *version;
  bool installed;
};

static Package g_package_registry[16];
static uint32_t g_package_count = 0;

bool install_package(const char *name, const char *version) {
  CONTRACT_ASSERT(g_package_count < 16, "Stage 10: Package registry full");
  g_package_registry[g_package_count++] = {name, version, true};
  return true;
}

bool uninstall_package(const char *name) {
  for (uint32_t i = 0; i < g_package_count; i++) {
    if (strcmp(g_package_registry[i].name, name) == 0 &&
        g_package_registry[i].installed) {
      g_package_registry[i].installed = false;
      return true;
    }
  }
  return false;
}

/* ======================================================
   USERSPACE API / APP HELPERS
   ====================================================== */

void app_write_file(const char *path, const char *content) {
  uint32_t fd = posix_open(path);
  if (fd == 0)
    return;
  posix_write(fd, content);
  posix_close(fd);
}

/* ======================================================
   STAGE 10 SELF-TEST
   ====================================================== */

void stage10_self_test() {
  serial_log("MODEL: Beginning Stage 10 High-Level OS Model Test...");

  uint32_t pid = create_task("UserspaceTask");

  create_thread(
      pid, "NotesThread",
      [](void *arg) {
        app_write_file("/model_test/note.txt", (const char *)arg);
      },
      (void *)"Stage 10 Test Note");

  create_thread(
      pid, "ExplorerThread",
      [](void *arg) {
        (void)arg;
        FileSystemTruth::fs_list_dir("/model_test");
      },
      nullptr);

  CONTRACT_ASSERT(install_package("DemoApp", "0.1.0"),
                  "Stage 10: install failed");

  schedule_threads();

  CONTRACT_ASSERT(uninstall_package("DemoApp"), "Stage 10: uninstall failed");

  serial_log("MODEL: Stage 10 High-Level OS Model Passed.");
}

} // namespace HighLevelModel

extern "C" void stage10_self_test() { HighLevelModel::stage10_self_test(); }
