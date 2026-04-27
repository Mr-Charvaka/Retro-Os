// =============================================================================
// brain_syscall.cpp — INT 0x81 Handler for Ring 2 AI Brain
// =============================================================================

#include "../drivers/serial.h"
#include "../include/isr.h"
#include "../include/memory_map.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "pmm.h"
#include "paging.h"
#include "pae.h"
#include "process.h"

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

// ---- External State ----
extern uint32_t tick;
extern process_t *ready_queue;

// ---- Ring 2 virtual address range for dynamic allocations ----
// We'll map Ring 2 allocations starting at RING2_HEAP_VIRT upward
static uint32_t brain_next_virt = RING2_HEAP_VIRT;

// Helper: Hand out pre-mapped Ring 2 pages
static uint32_t brain_alloc_mapped_pages(uint32_t count) {
    if (count == 0 || count > 65536) return 0; 

    uint32_t virt_addr = brain_next_virt;
    
    // Rev AC: Physically backed and mapped at boot in init_paging.
    // Just advance the virtual cursor.
    brain_next_virt += count * 4096;

    return virt_addr;
}

// ---- Main INT 0x81 Handler ----
static void brain_syscall_handler(registers_t *regs) {
    uint32_t syscall_id = regs->eax;
    uint32_t result = 0xFFFFFFFF;

    switch (syscall_id) {

        // === Syscall 0x00: Verify Ring Level ===
        case BRAIN_SYS_VERIFY_RING: {
            result = regs->cs & 0x3; // RPL bits of the CALLER'S CS
            break;
        }

        // === Syscall 0x01: Log message to serial ===
        case BRAIN_SYS_LOG: {
            const char *msg = (const char *)regs->ebx;
            if (msg && (uint32_t)msg >= 0xC0000000) {
                serial_log(msg); // Prepend prefix in the Ring 2 wrapper if desired
                result = 0;
            }
            break;
        }

        // === Syscall 0x02: Get process count ===
        case BRAIN_SYS_GET_PROCESS_COUNT: {
            uint32_t count = 0;
            process_t *p = ready_queue;
            if (p) {
                process_t *start = p;
                do {
                    if (p->state != PROCESS_ZOMBIE) count++;
                    p = p->next;
                } while (p != start);
            }
            result = count;
            break;
        }

        // === Syscall 0x03: Get free memory (bytes) ===
        case BRAIN_SYS_GET_FREE_MEMORY: {
            result = pmm_get_free_block_count() * 4096;
            break;
        }

        // === Syscall 0x04: Get uptime (ticks) ===
        case BRAIN_SYS_GET_UPTIME: {
            result = tick;
            break;
        }

        // === Syscall 0x05: Allocate single page ===
        case BRAIN_SYS_ALLOC_PAGE: {
            result = brain_alloc_mapped_pages(1);
            break;
        }

        // === Syscall 0x06: Get file size ===
        case BRAIN_SYS_FILE_SIZE: {
            const char *path = (const char *)regs->ebx;
            if (!path || (uint32_t)path < 0xC0000000) break;

            vfs_node_t *node = vfs_resolve_path(path);
            if (node) {
                result = (uint32_t)node->size; 
            }
            break;
        }

        // === Syscall 0x07: Read file into buffer (STRICT DIAGNOSTIC) ===
        case BRAIN_SYS_FILE_READ: {
            const char *path = (const char *)regs->ebx;
            uint8_t *user_buf = (uint8_t *)regs->ecx;
            uint32_t max_size = regs->edx;

            // serial_log("[BRAIN_READ] === FILE READ START ===\n");
            if (path && (uint32_t)path >= 0xC0000000) {
                // serial_log("[BRAIN_READ] Path: ");
                // serial_log(path);
                // serial_log("\n");
            } else {
                serial_log("[BRAIN_READ] ERROR: Invalid path pointer!\n");
                result = 0xFFFFFFFF; break;
            }
            // serial_log_hex("[BRAIN_READ] Buffer: ", (uint32_t)user_buf);
            // serial_log_hex("[BRAIN_READ] MaxSize: ", max_size);

            if (!user_buf) { result = 0xFFFFFFFF; break; }

            vfs_node_t *node = vfs_resolve_path(path);
            if (!node) {
                serial_log("[BRAIN_READ] ERROR: File not found!\n");
                result = 0xFFFFFFFF; break;
            }
            // serial_log_hex("[BRAIN_READ] File node size: ", (uint32_t)node->size);

            uint32_t to_read = (node->size < (uint64_t)max_size) ? (uint32_t)node->size : max_size;
            // serial_log_hex("[BRAIN_READ] Will read: ", to_read);

            // TEST: Can we write to the target buffer?
            // serial_log("[BRAIN_READ] Testing buffer write...\n");
            volatile uint8_t *test_ptr = (volatile uint8_t *)user_buf;
            *test_ptr = 0xAA;
            if (*test_ptr != 0xAA) {
                serial_log("[BRAIN_READ] FATAL: Buffer NOT writable (readback mismatch)!\n");
                result = 0xFFFFFFFF; break;
            }
            // serial_log("[BRAIN_READ] Buffer write OK\n");

            uint32_t total_read = 0;
            uint8_t *kbuf = (uint8_t *)kmalloc(4096);
            if (!kbuf) {
                serial_log("[BRAIN_READ] ERROR: kmalloc failed!\n");
                result = 0xFFFFFFFF; break;
            }

            while (total_read < to_read) {
                uint32_t chunk = (to_read - total_read > 4096) ? 4096 : (to_read - total_read);
                int ret = vfs_read(node, (uint64_t)total_read, kbuf, (uint64_t)chunk);
                if (ret <= 0) {
                    serial_log_hex("[BRAIN_READ] vfs_read FAILED at offset: ", total_read);
                    break;
                }
                if (total_read + (uint32_t)ret > to_read) ret = (int)(to_read - total_read);
                memcpy(user_buf + total_read, kbuf, (uint32_t)ret);
                total_read += (uint32_t)ret;

                // Log progress every 64KB
                // Socially Responsible 1MB Back-off
                if ((total_read & 0xFFFFF) == 0) {
                    // Force a roughly 10-20ms gap so other cores can load files
                    for (volatile int i = 0; i < 5000000; i = i + 1) { asm volatile("pause"); }
                    kernel_yield(); 
                }
                else if ((total_read & 0xFFFF) == 0) {
                    kernel_yield(); // Regular yielding every 64KB
                }
            }

            // serial_log_hex("[BRAIN_READ] DONE. Total: ", total_read);
            kfree(kbuf);
            result = total_read;
            break;
        }

        // === Syscall 0x08: Allocate N contiguous pages ===
        case BRAIN_SYS_ALLOC_PAGES: {
            uint32_t count = regs->ebx;
            uint32_t addr = brain_alloc_mapped_pages(count);
            result = addr ? addr : 0xFFFFFFFF;
            break;
        }

        // === Syscall 0x09: Free pages ===
        case BRAIN_SYS_FREE_PAGES: {
            uint32_t virt = regs->ebx;
            uint32_t count = regs->ecx;

            if (virt < RING2_HEAP_VIRT || count == 0 || count > 65536) break;

            for (uint32_t i = 0; i < count; i++) {
                uint32_t page_virt = virt + (i * 4096);
                uint32_t *pte = paging_get_pte(page_virt);
                if (pte && (*pte & 0x1)) {
                    uint32_t phys = *pte & 0xFFFFF000;
                    pmm_free_block((void *)phys);
                    *pte = 0; // unmap
                }
            }
            // Flush TLB
            for (uint32_t i = 0; i < count; i++) {
                uint32_t page_virt = virt + (i * 4096);
                asm volatile("invlpg (%0)" :: "r"(page_virt) : "memory");
            }
            result = 0;
            break;
        }

        // === Syscall 0x0A: Get time in milliseconds ===
        case BRAIN_SYS_GET_TIME_MS: {
            result = tick * 10; 
            break;
        }
        
        // === Syscall 0x0B: Yield CPU ===
        case BRAIN_SYS_YIELD: {
            kernel_yield();
            result = 0;
            break;
        }

        // === Syscall 0x0C: Read file into buffer with offset ===
        case BRAIN_SYS_FILE_READ_OFFSET: {
            const char *path = (const char *)regs->ebx;
            uint8_t *user_buf = (uint8_t *)regs->ecx;
            uint32_t size = regs->edx;
            uint32_t offset = regs->edi;

            if (!path || !user_buf || (uint32_t)path < 0xC0000000) {
                result = 0xFFFFFFFF; break;
            }

            vfs_node_t *node = vfs_resolve_path(path);
            if (!node) {
                result = 0xFFFFFFFF; break;
            }

            // Cap at file size
            if (offset >= (uint32_t)node->size) {
                result = 0; break;
            }
            if (offset + size > (uint32_t)node->size) {
                size = (uint32_t)node->size - (uint32_t)offset;
            }

            serial_log_hex("[BRAIN_READ] Path addr: ", (uint32_t)path);
            result = (uint32_t)vfs_read(node, (uint64_t)offset, user_buf, (uint64_t)size);
            break;
        }

        default: {
            serial_log_hex("BRAIN_SYSCALL: Unknown syscall ", syscall_id);
            result = 0xFFFFFFFF;
            break;
        }
    }

    regs->eax = result;
}

// ---- Register the handler ----
extern "C" void init_brain_syscalls(void) {
    register_interrupt_handler(129, (isr_t)brain_syscall_handler);
    serial_log("BRAIN_SYSCALL: INT 0x81 handler updated (v0-v10 registered).");
}
