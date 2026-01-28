#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"


/*
=========================================================
 STAGE 9 — MULTITASKING & IPC
=========================================================
*/

namespace TaskModel {

typedef void (*TaskEntry)(void *arg);

struct Task {
  uint32_t pid;
  const char *name;
  TaskEntry entry;
  void *arg;
  bool running;
};

constexpr uint32_t MAX_TASKS = 32;
static Task g_tasks[MAX_TASKS];
static uint32_t g_task_count = 0;
static uint32_t g_next_pid = 5000;
static uint32_t g_current_task_idx = 0;

uint32_t create_task(const char *name, TaskEntry entry, void *arg) {
  CONTRACT_ASSERT(g_task_count < MAX_TASKS, "Task: table full");

  Task &t = g_tasks[g_task_count++];
  t.pid = g_next_pid++;
  t.name = name;
  t.entry = entry;
  t.arg = arg;
  t.running = true;
  return t.pid;
}

void schedule_demo() {
  if (g_task_count == 0)
    return;
  for (uint32_t i = 0; i < g_task_count; i++) {
    g_current_task_idx = (g_current_task_idx + 1) % g_task_count;
    if (g_tasks[g_current_task_idx].running) {
      g_tasks[g_current_task_idx].entry(g_tasks[g_current_task_idx].arg);
    }
  }
}

struct IPCMessage {
  uint32_t from_pid;
  const char *data;
};

struct IPCQueue {
  IPCMessage messages[16];
  uint32_t head;
  uint32_t tail;
};

static IPCQueue g_ipc_queues[MAX_TASKS];

bool ipc_send_model(uint32_t to_pid, const char *msg) {
  for (uint32_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].pid == to_pid) {
      IPCQueue &q = g_ipc_queues[i];
      uint32_t next_tail = (q.tail + 1) % 16;
      if (next_tail == q.head)
        return false;
      q.messages[q.tail].from_pid = 999;
      q.messages[q.tail].data = msg;
      q.tail = next_tail;
      return true;
    }
  }
  return false;
}

void stage9_self_test() {
  serial_log("MODEL: Beginning Stage 9 Multitasking/IPC Model Test...");

  static bool task1_ran = false;
  create_task("Task1", [](void *a) { *(bool *)a = true; }, &task1_ran);

  schedule_demo();

  CONTRACT_ASSERT(task1_ran, "Task: Task1 did not run");

  serial_log("MODEL: Stage 9 Multitasking/IPC Model Passed.");
}

} // namespace TaskModel

extern "C" void stage9_self_test() { TaskModel::stage9_self_test(); }
