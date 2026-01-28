#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"


/*
=========================================================
 STAGE 3 — EXECUTION MODEL
=========================================================
*/

namespace ExecutionModel {

/* ======================================================
   PROCESS STATES (STRICT)
   ====================================================== */

enum class ProcessState : uint8_t { Created, Runnable, Running, Blocked, Dead };

/* ======================================================
   PROCESS CONTROL BLOCK (PCB)
   ====================================================== */

struct Process {
  PID pid;
  ProcessState state;

  // Execution
  void (*entry_point)();

  // Accounting / safety
  uint64_t ticks_executed;
  bool kill_requested;
};

/* ======================================================
   PROCESS TABLE
   ====================================================== */

constexpr uint32_t MAX_PROCESSES = 256;

static Process g_process_table[MAX_PROCESSES];
static uint32_t g_process_count = 0;
static PID g_next_pid = 1000; // Start at 1000 for model processes

/* ======================================================
   CURRENTLY RUNNING PROCESS
   ====================================================== */

static Process *g_current_process = nullptr;

/* ======================================================
   PROCESS CREATION
   ====================================================== */

PID process_create(void (*entry)()) {
  CONTRACT_ASSERT(entry != nullptr, "Model: Process entry null");
  CONTRACT_ASSERT(g_process_count < MAX_PROCESSES, "Model: Process table full");

  Process &p = g_process_table[g_process_count++];

  p.pid = g_next_pid++;
  p.state = ProcessState::Created;
  p.entry_point = entry;
  p.ticks_executed = 0;
  p.kill_requested = false;

  p.state = ProcessState::Runnable;

  return p.pid;
}

/* ======================================================
   PROCESS LOOKUP
   ====================================================== */

Process *process_by_pid(PID pid) {
  for (uint32_t i = 0; i < g_process_count; ++i) {
    if (g_process_table[i].pid == pid)
      return &g_process_table[i];
  }
  return nullptr;
}

/* ======================================================
   PROCESS TERMINATION
   ====================================================== */

void process_request_kill(PID pid) {
  Process *p = process_by_pid(pid);
  if (!p)
    return;

  p->kill_requested = true;
}

static void process_finalize(Process &p) { p.state = ProcessState::Dead; }

/* ======================================================
   BLOCKING / UNBLOCKING
   ====================================================== */

void process_block(Process &p) {
  CONTRACT_ASSERT(p.state == ProcessState::Running,
                  "Model: Only running process may block");
  p.state = ProcessState::Blocked;
}

void process_unblock(Process &p) {
  CONTRACT_ASSERT(p.state == ProcessState::Blocked,
                  "Model: Unblock only from blocked");
  p.state = ProcessState::Runnable;
}

/* ======================================================
   SCHEDULER (COOPERATIVE, CORRECT)
   ====================================================== */

static Process *scheduler_pick_next() {
  for (uint32_t i = 0; i < g_process_count; ++i) {
    Process &p = g_process_table[i];
    if (p.state == ProcessState::Runnable)
      return &p;
  }
  return nullptr;
}

void scheduler_tick() {
  Process *next = scheduler_pick_next();
  if (!next)
    return;

  g_current_process = next;

  CONTRACT_ASSERT(next->state == ProcessState::Runnable,
                  "Model: Scheduler picked invalid state");

  next->state = ProcessState::Running;

  // Execute process
  next->entry_point();
  next->ticks_executed++;

  // Post-run safety
  if (next->kill_requested) {
    process_finalize(*next);
  } else if (next->state == ProcessState::Running) {
    next->state = ProcessState::Runnable;
  }

  g_current_process = nullptr;
}

/* ======================================================
   CURRENT PROCESS ACCESS
   ====================================================== */

Process &current_process() {
  CONTRACT_ASSERT(g_current_process != nullptr, "Model: No current process");
  return *g_current_process;
}

PID current_pid() { return g_current_process ? g_current_process->pid : -1; }

/* ======================================================
   YIELD & EXIT (USER-LEVEL API)
   ====================================================== */

void process_yield() {
  Process &p = current_process();
  p.state = ProcessState::Runnable;
}

[[noreturn]] void process_exit() {
  Process &p = current_process();
  p.state = ProcessState::Dead;
  while (true) {
  }
}

/* ======================================================
   STAGE 3 INVARIANTS (SELF-TEST)
   ====================================================== */

void stage3_self_test() {
  serial_log("MODEL: Beginning Stage 3 Execution Model Test...");

  static bool test1_ran = false;
  PID p1 = process_create([] {
    test1_ran = true;
    ExecutionModel::process_yield();
  });

  PID p2 = process_create([] { ExecutionModel::process_exit(); });

  CONTRACT_ASSERT(p1 != -1, "Model: p1 create failed");
  CONTRACT_ASSERT(p2 != -1, "Model: p2 create failed");

  scheduler_tick();
  scheduler_tick();

  CONTRACT_ASSERT(test1_ran, "Model: p1 did not execute");
  serial_log("MODEL: Stage 3 Execution Model Passed.");
}

} // namespace ExecutionModel

extern "C" void stage3_self_test() { ExecutionModel::stage3_self_test(); }

extern "C" PID execution_model_current_pid() {
  return ExecutionModel::current_pid();
}
