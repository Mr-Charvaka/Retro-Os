#include "../include/types.h"

// Define size_t
typedef __SIZE_TYPE__ size_t;

extern "C" {
void *kmalloc(uint32_t size);
void kfree(void *ptr);
void serial_log(const char *msg);
}

// Linker-provided symbols for global constructors/destructors
extern "C" {
typedef void (*constructor_t)();
extern constructor_t __init_array_start[];
extern constructor_t __init_array_end[];
extern constructor_t __fini_array_start[];
extern constructor_t __fini_array_end[];
}

// Call all global constructors (must be called early in main)
extern "C" void __cxx_global_ctor_init() {
  serial_log("C++: Calling global constructors...");
  for (constructor_t *f = __init_array_start; f < __init_array_end; f++) {
    (*f)();
  }
  serial_log("C++: Global constructors complete.");
}

// Call all global destructors (typically never called in kernel)
extern "C" void __cxx_global_dtor_fini() {
  for (constructor_t *f = __fini_array_start; f < __fini_array_end; f++) {
    (*f)();
  }
}

void *operator new(size_t size) {
  void *ptr = kmalloc(size);
  if (!ptr) {
    serial_log("NEW: FAILED ALLOCATION!");
  }
  return ptr;
}

void *operator new[](size_t size) { return kmalloc(size); }

void operator delete(void *ptr) { kfree(ptr); }
void operator delete[](void *ptr) { kfree(ptr); }
void operator delete(void *ptr, size_t size) {
  (void)size;
  kfree(ptr);
}

extern "C" void __cxa_pure_virtual() {
  serial_log("[C++] Panic: pure virtual function called!");
  while (1) {
  }
}

extern "C" {
// Stack Smash Protection (SSP) Canary
uintptr_t __stack_chk_guard = 0x595e9fbd;

__attribute__((noreturn)) void __stack_chk_fail() {
  serial_log("PANIC: Stack Smashing Detected!");
  while (1) {
    asm volatile("hlt");
  }
}
}
