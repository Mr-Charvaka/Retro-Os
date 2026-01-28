#include "../drivers/serial.h"
#include "../include/string.h"
#include <stdarg.h>

extern "C" {

void *kmalloc(uint32_t size);
void kfree(void *ptr);
uint32_t sys_time_ms();

void *retroos_calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *ptr = kmalloc(total);
  if (ptr) {
    memset(ptr, 0, total);
  }
  return ptr;
}

void retroos_free(void *ptr) { kfree(ptr); }

int retroos_printf(const char *format, ...) {
  // Simple wrapper until we have a real vsnprintf
  // Just log the message to serial for now
  serial_log(format);
  return 0;
}

int retroos_snprintf(char *str, size_t size, const char *format, ...) {
  if (size > 0)
    str[0] = 0;
  return 0;
}

void mbedtls_platform_exit(int status) {
  serial_log("mbedtls: exit called");
  while (1)
    ;
}

// Dummy entropy source for now
int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t len,
                                  size_t *olen) {
  (void)data;
  uint32_t t = sys_time_ms();
  for (size_t i = 0; i < len; i++) {
    output[i] = (unsigned char)(t ^ (i * 0x33));
  }
  *olen = len;
  return 0;
}

} // extern "C"
