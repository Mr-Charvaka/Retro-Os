// =============================================================================
// ring2.h — Ring 2 (AI Brain) Subsystem Wrapper
// =============================================================================

#ifndef RING2_H
#define RING2_H

#include "../include/memory_map.h"
#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Constants ----
#define RING2_ALIVE_MAGIC   0x52494E32  // "RIN2"

// ---- Syscall Numbers ----
#define BRAIN_SYS_VERIFY_RING       0x00
#define BRAIN_SYS_LOG               0x01
#define BRAIN_SYS_GET_PROCESS_COUNT 0x02
#define BRAIN_SYS_GET_FREE_MEMORY   0x03
#define BRAIN_SYS_GET_UPTIME        0x04
#define BRAIN_SYS_ALLOC_PAGE        0x05
#define BRAIN_SYS_FILE_SIZE         0x06
#define BRAIN_SYS_FILE_READ         0x07
#define BRAIN_SYS_ALLOC_PAGES       0x08
#define BRAIN_SYS_FREE_PAGES        0x09
#define BRAIN_SYS_GET_TIME_MS       0x0A
#define BRAIN_SYS_YIELD             0x0B
#define BRAIN_SYS_FILE_READ_OFFSET  0x0C

// ---- System State ----
extern volatile uint32_t ring2_alive;
extern volatile uint32_t ring2_cpl;

// ---- Request types (Ring 3 → Ring 2 Call Gate) ----
#define BRAIN_REQ_PING      0x00
#define BRAIN_REQ_ADD_ONE   0x01
#define BRAIN_REQ_GET_CPL   0x02
#define BRAIN_REQ_ECHO      0x03

// ---- Launch & Entry ----
void launch_ring2(void);
void brain_main(void);
extern "C" void ring2_brain_entry(void);

// =============================================================================
// Syscall Wrappers (Inline ASM) — These run IN Ring 2
// =============================================================================

static inline uint32_t brain_sys_verify_ring(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_VERIFY_RING));
    return result;
}

static inline uint32_t brain_sys_log(const char *message) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_LOG), "b"(message));
    return result;
}

static inline uint32_t brain_sys_get_process_count(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_GET_PROCESS_COUNT));
    return result;
}

static inline uint32_t brain_sys_get_free_memory(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_GET_FREE_MEMORY));
    return result;
}

static inline uint32_t brain_sys_get_uptime(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_GET_UPTIME));
    return result;
}

static inline uint32_t brain_sys_alloc_page(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_ALLOC_PAGE));
    return result;
}

static inline uint32_t brain_sys_file_size(const char *path) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_FILE_SIZE), "b"(path));
    return result;
}

static inline uint32_t brain_sys_file_read(const char *path, void *buf, uint32_t max_size) {
    uint32_t result;
    asm volatile("int $0x81"
        : "=a"(result)
        : "a"(BRAIN_SYS_FILE_READ), "b"(path), "c"(buf), "d"(max_size));
    return result;
}

static inline uint32_t brain_sys_file_read_offset(const char *path, void *buf, uint32_t size, uint32_t offset) {
    uint32_t result;
    asm volatile("int $0x81"
        : "=a"(result)
        : "a"(BRAIN_SYS_FILE_READ_OFFSET), "b"(path), "c"(buf), "d"(size), "D"(offset));
    return result;
}

static inline uint32_t brain_sys_alloc_pages(uint32_t count) {
    uint32_t result;
    asm volatile("int $0x81"
        : "=a"(result)
        : "a"(BRAIN_SYS_ALLOC_PAGES), "b"(count));
    return result;
}

static inline uint32_t brain_sys_free_pages(uint32_t virt_addr, uint32_t count) {
    uint32_t result;
    asm volatile("int $0x81"
        : "=a"(result)
        : "a"(BRAIN_SYS_FREE_PAGES), "b"(virt_addr), "c"(count));
    return result;
}

static inline uint32_t brain_sys_get_time_ms(void) {
    uint32_t result;
    asm volatile("int $0x81" : "=a"(result) : "a"(BRAIN_SYS_GET_TIME_MS));
    return result;
}

static inline void brain_sys_yield(void) {
    asm volatile("int $0x81" : : "a"(BRAIN_SYS_YIELD));
}

// ---- Utility ----
static inline void brain_itoa(int value, char *buf) {
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12];
    int i = 0;
    int neg = 0;
    if (value < 0) { neg = 1; value = -value; }
    while (value > 0) { tmp[i++] = '0' + (value % 10); value /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif // RING2_H
