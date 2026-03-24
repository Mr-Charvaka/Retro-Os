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
#include <stdarg.h>
#include <stdio.h>

extern "C" int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  size_t written = 0;
  const char *f = format;
  while (*f && written < size - 1) {
    if (*f == '%') {
      f++;
      if (*f == '.') {
        f++;
        if (*f == '*') {
          f++;
          if (*f == 's') {
            int len = va_arg(ap, int);
            const char *s = va_arg(ap, const char *);
            for (int i = 0; i < len && written < size - 1; i++) str[written++] = *s++;
          }
        }
      } else if (*f == 's') {
        const char *s = va_arg(ap, const char *);
        while (*s && written < size - 1) str[written++] = *s++;
      } else if (*f == 'd') {
        int d = va_arg(ap, int);
        if (d == 0) {
          str[written++] = '0';
        } else {
          if (d < 0) {
            str[written++] = '-';
            d = -d;
          }
          char buf[12];
          int i = 0;
          while (d > 0) { buf[i++] = (d % 10) + '0'; d /= 10; }
          while (i > 0 && written < size - 1) str[written++] = buf[--i];
        }
      } else if (*f == 'p') {
        uint32_t p = va_arg(ap, uint32_t);
        const char *hex = "0123456789ABCDEF";
        for (int i = 7; i >= 0 && written < size - 1; i--) {
          str[written++] = hex[(p >> (i * 4)) & 0xF];
        }
      } else if (*f == '%') {
        str[written++] = '%';
      }
    } else {
      str[written++] = *f;
    }
    f++;
  }
  str[written] = 0;
  return written;
}

extern "C" int printf(const char *format, ...) {
  char buf[512];
  va_list args;
  va_start(args, format);
  int ret = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  serial_log(buf);
  return ret;
}

extern "C" char *strdup(const char *s) {
  size_t len = 0;
  while (s[len]) len++;
  char *d = (char *)kmalloc(len + 1);
  if (!d) return nullptr;
  for (size_t i = 0; i <= len; i++) d[i] = s[i];
  return d;
}
