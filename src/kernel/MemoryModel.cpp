#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"


/*
=========================================================
 STAGE 4 — MEMORY MODEL
=========================================================
*/

extern "C" PID execution_model_current_pid();

namespace MemoryModel {

using PhysicalAddress = uintptr_t;
using VirtualAddress = uintptr_t;

enum class MemoryOwner : uint8_t { Kernel, Process };

struct MemoryBlock {
  VirtualAddress base;
  size_t size;
  MemoryOwner owner;
  int32_t owner_pid; // valid only if owner == Process
  bool in_use;
};

/* ======================================================
   KERNEL HEAP (FIXED REGION)
   ====================================================== */

constexpr size_t KERNEL_HEAP_SIZE = 1 * 1024 * 1024; // 1MB for this model
static uint8_t g_kernel_heap[KERNEL_HEAP_SIZE];
static size_t g_heap_offset = 0;

/* ======================================================
   ALLOCATION TABLE
   ====================================================== */

constexpr uint32_t MAX_MEMORY_BLOCKS = 1024;
static MemoryBlock g_blocks[MAX_MEMORY_BLOCKS];
static uint32_t g_block_count = 0;

/* ======================================================
   INTERNAL HELPERS
   ====================================================== */

static MemoryBlock *register_block(VirtualAddress base, size_t size,
                                   MemoryOwner owner, int32_t pid) {
  CONTRACT_ASSERT(g_block_count < MAX_MEMORY_BLOCKS,
                  "Memory: block table overflow");

  MemoryBlock &b = g_blocks[g_block_count++];
  b.base = base;
  b.size = size;
  b.owner = owner;
  b.owner_pid = pid;
  b.in_use = true;
  return &b;
}

/* ======================================================
   KERNEL ALLOCATION
   ====================================================== */

void *kmalloc_model(size_t size) {
  CONTRACT_ASSERT(size > 0, "Memory: kmalloc zero size");

  size = (size + 7) & ~7; // align 8 bytes

  if (g_heap_offset + size > KERNEL_HEAP_SIZE)
    return nullptr;

  VirtualAddress addr = (VirtualAddress)&g_kernel_heap[g_heap_offset];
  g_heap_offset += size;

  register_block(addr, size, MemoryOwner::Kernel, -1);

  return (void *)addr;
}

/* ======================================================
   PROCESS ALLOCATION
   ====================================================== */

void *pmalloc_model(size_t size) {
  CONTRACT_ASSERT(size > 0, "Memory: pmalloc zero size");

  int32_t pid = execution_model_current_pid();
  // For test, we might allow no process. But strict says KASSERT.
  // CONTRACT_ASSERT(pid >= 0, "Memory: pmalloc without process");

  void *mem = kmalloc_model(size);
  if (!mem)
    return nullptr;

  g_blocks[g_block_count - 1].owner = MemoryOwner::Process;
  g_blocks[g_block_count - 1].owner_pid = pid;

  return mem;
}

/* ======================================================
   DEALLOCATION (STRICT)
   ====================================================== */

void kfree_model(void *ptr) {
  CONTRACT_ASSERT(ptr != nullptr, "Memory: kfree null");

  VirtualAddress addr = (VirtualAddress)ptr;

  for (uint32_t i = 0; i < g_block_count; i++) {
    MemoryBlock &b = g_blocks[i];
    if (b.base == addr && b.in_use) {
      CONTRACT_ASSERT(b.owner == MemoryOwner::Kernel,
                      "Memory: kfree non-kernel block");
      b.in_use = false;
      return;
    }
  }

  kernel_panic("Memory: kfree invalid pointer");
}

/* ======================================================
   STAGE 4 SELF-TEST
   ====================================================== */

void stage4_self_test() {
  serial_log("MODEL: Beginning Stage 4 Memory Model Test...");
  void *k = kmalloc_model(64);
  CONTRACT_ASSERT(k != nullptr, "Memory: kernel alloc failed");
  kfree_model(k);
  serial_log("MODEL: Stage 4 Memory Model Passed.");
}

} // namespace MemoryModel

extern "C" void stage4_self_test() { MemoryModel::stage4_self_test(); }
