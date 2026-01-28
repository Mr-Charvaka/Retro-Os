#include "heap.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "memory.h"
#include "paging.h" // For getting physical address if needed
#include "slab.h"

kheap_t kheap;
int slab_is_initialized = 0;

extern uint32_t *kernel_directory;

// Heap badhane ka jugad
void expand_unix_heap(uint32_t new_size) {
  // Abhi toh fixed size hi hai
  // Dynamic expansion baad mein dekhenge
  serial_log("HEAP: Abhi fixed size hi chalne do.");
}

void init_kheap(uint32_t start, uint32_t end, uint32_t max) {
  if (start % 4096 != 0 || end % 4096 != 0) {
    serial_log("HEAP: Page align karo bhai, aise nahi chalega!");
    return;
  }

  kheap.start_address = start;
  kheap.end_address = end;
  kheap.max_address = max;
  kheap.supervisor = 1;
  kheap.readonly = 0;

  buddy_init(&kheap.buddy, start, end);
}

#include "../include/io.h"

void *kmalloc_real(uint32_t size, int align, uint32_t *phys) {
  cli();

  // Pehle Slab Allocator check karo (agar chhota size hai)
  if (slab_is_initialized && size <= 2048 && !align) {
    void *ptr = slab_alloc(size);
    if (ptr) {
      if (phys)
        *phys = (uint32_t)ptr;
      sti();
      return ptr;
    }
  }

  if (size == 0) {
    sti();
    return 0;
  }

  // Buddy Allocator use karenge
  // Kitne bytes chahiye? header + padding + pointer sab milake
  size_t required_size = size + sizeof(header_t) + 4;
  if (align)
    required_size += 4096;

  void *buddy_ptr = buddy_alloc(&kheap.buddy, required_size);
  if (!buddy_ptr) {
    serial_log("HEAP: OOM in Buddy Allocator!");
    sti();
    return nullptr;
  }

  uint32_t data_start = (uint32_t)buddy_ptr + sizeof(header_t);
  uint32_t adjusted_ptr = data_start;
  if (align && (adjusted_ptr & 0xFFF)) {
    adjusted_ptr = (adjusted_ptr + 0xFFF) & 0xFFFFF000;
  }
  adjusted_ptr += 4; // Always offset by 4 for header pointer

  // Buddy block ka pointer buffer se pehle chhupa dete hain
  *((uint32_t *)(adjusted_ptr - 4)) = (uint32_t)buddy_ptr;

  header_t *header = (header_t *)buddy_ptr;
  header->size = required_size;
  header->allocated = 1;
  header->magic = 0xCAFEBABE;

  void *data = (void *)adjusted_ptr;
  if (phys)
    *phys = (uint32_t)data;

  sti();
  return data;
}

void kfree(void *p) {
  cli();
  if (p == 0) {
    sti();
    return;
  }

  // Try Slab Free
  if (slab_is_initialized && slab_free(p)) {
    sti();
    return;
  }

  // Asli Buddy block pointer nikalo jo humne chhupa ke rakha tha
  uint32_t buddy_ptr = *((uint32_t *)((uintptr_t)p - 4));
  if (buddy_ptr < kheap.start_address || buddy_ptr > kheap.end_address) {
    serial_log("HEAP: Pointer galat hai - corruption lag raha hai!");
    sti();
    return;
  }

  header_t *header = (header_t *)buddy_ptr;
  if (header->magic != 0xCAFEBABE) {
    serial_log("HEAP: Double free ya corruption, kuch toh gadbad hai!");
    sti();
    return;
  }

  uint32_t size = header->size;
  header->allocated = 0;
  header->magic = 0xBADB00B5;

  buddy_free(&kheap.buddy, header, size);
  sti();
}

void *malloc(uint32_t size) { return kmalloc_real(size, 0, 0); }

void free(void *p) { kfree(p); }
