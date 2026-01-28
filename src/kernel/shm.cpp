// Shared Memory - Mil-baat ke memory use karne ka jugad
#include "shm.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "pmm.h"
#include "process.h"
#include "vm.h"

extern "C" {

static shm_segment_t shm_segments[SHM_MAX_SEGMENTS];

void shm_init() {
  for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
    shm_segments[i].in_use = 0;
    shm_segments[i].ref_count = 0;
  }
  serial_log("SHM: Sab set hai.");
}

int sys_shmget(uint32_t key, uint32_t size, int flags) {
  (void)flags;

  // Dekho agar is key ka segment pehle se bana hai kya
  for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
    if (shm_segments[i].in_use && shm_segments[i].key == key) {
      return i;
    }
  }

  // Koi khali slot dhundo
  int slot = -1;
  for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
    if (!shm_segments[i].in_use) {
      slot = i;
      break;
    }
  }

  if (slot < 0)
    return -1;

  // Window resize wagera ke liye kam se kam 4MB toh chahiye hi
  uint32_t min_size = 4 * 1024 * 1024; // 4MB
  uint32_t final_size = (size > min_size) ? size : min_size;

  uint32_t pages = (final_size + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE;

  // Physical pages mangwao system se
  void *phys = pmm_alloc_contiguous_blocks(pages);
  if (!phys) {
    serial_log("SHM ERROR: Memory nahi mil rahi contiguous!");
    return -1;
  }

  // Segment ko initialize karo
  shm_segments[slot].key = key;
  shm_segments[slot].size = final_size;
  shm_segments[slot].phys_addr = phys;
  shm_segments[slot].ref_count = 0;
  shm_segments[slot].in_use = 1;

  serial_log_hex("SHM: Created segment ", slot);
  return slot;
}

void shm_free(int shmid) {
  if (shmid < 0 || shmid >= SHM_MAX_SEGMENTS)
    return;
  if (!shm_segments[shmid].in_use)
    return;

  shm_segment_t *seg = &shm_segments[shmid];

  // Physical memory wapas kar do
  uint32_t pages = (seg->size + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE;
  pmm_free_contiguous_blocks(seg->phys_addr, pages);

  seg->in_use = 0;
  seg->key = 0;
  seg->ref_count = 0;
  serial_log_hex("SHM: Freed segment ", shmid);
}

void *sys_shmat(int shmid) {
  if (shmid < 0 || shmid >= SHM_MAX_SEGMENTS)
    return 0;
  if (!shm_segments[shmid].in_use)
    return 0;

  shm_segment_t *seg = &shm_segments[shmid];

  // FIXED MAPPING: Har segment ke liye 0x70000000 range mein 8MB ki jagah fix
  // hai. Isse pointers sabhi processes ke beech interchange ho sakte hain bina
  // tension ke.
  uint32_t virt = 0x70000000 + (shmid * 0x800000);
  seg->virt_start = virt; // Documentation aur lookup ke liye set kar liya

  // Physical pages ko virtual address pe map karo
  uint32_t phys = (uint32_t)(uintptr_t)seg->phys_addr;
  uint32_t num_pages = (seg->size + 4095) / SHM_PAGE_SIZE;

  for (uint32_t i = 0; i < num_pages; i++) {
    vm_map_page(phys + i * SHM_PAGE_SIZE, virt + i * SHM_PAGE_SIZE,
                7); // User|RW|Present
  }

  seg->ref_count++;

  serial_log_hex("SHM: Attached segment at ", virt);
  return (void *)(uintptr_t)virt;
}

int sys_shmdt(void *addr) {
  (void)addr;
  return 0;
}

shm_segment_t *shm_get_segment(uint32_t virt_addr) {
  // Use the fixed mapping math to find the potential segment index
  if (virt_addr < 0x70000000 || virt_addr >= 0x80000000)
    return nullptr;

  int shmid = (virt_addr - 0x70000000) / 0x800000;
  if (shmid >= 0 && shmid < SHM_MAX_SEGMENTS) {
    if (shm_segments[shmid].in_use) {
      // Optional: Verify address is within this segment's allocated size
      if (virt_addr <
          0x70000000 + (shmid * 0x800000) + shm_segments[shmid].size) {
        return &shm_segments[shmid];
      }
    }
  }
  return nullptr;
}

} // extern "C"
