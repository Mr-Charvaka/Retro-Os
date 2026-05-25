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
#define BRAIN_SYS_GENESIS_EVOLVE    0x0D
#define BRAIN_SYS_DRAP_PROXY        0x0E

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

        // === Syscall 0x0D: Genesis Evolve (Neural Bridge) ===
        case BRAIN_SYS_GENESIS_EVOLVE: {
            // ebx: Pointer to Genome Segment (GREEN context)
            // ecx: Seed value
            // Returns: 
            //   0xDEADBEEF: Brain is Thinking (Asynchronous)
            //   Other: Mutation Vector (Processing Complete)
            uint8_t *genome_segment = (uint8_t *)regs->ebx;
            uint32_t seed = regs->ecx;

            if (!genome_segment || (uint32_t)genome_segment < RING2_HEAP_VIRT) {
                result = 0xFFFFFFFF; break;
            }

            #include "brain/brain_mailbox.h"
            BrainMailbox *mb = (BrainMailbox*)BRAIN_MAILBOX_VIRT;

            if (mb->status == BRAIN_STATUS_IDLE) {
                serial_log("[GENESIS] Dispatching Evolution Thought to Brain...\n");
                
                // Fetch Hardware Telemetry from TICA Golden CSR (0x800)
                uint64_t audit_hash;
                asm volatile("csrr %0, 0x800" : "=r"(audit_hash));
                mb->tica_audit_context = audit_hash;
                
                mb->command = BRAIN_CMD_EVOLVE;
                mb->status = BRAIN_STATUS_BUSY;
                result = 0xDEADBEEF; // STATUS: THINKING
            } 
            else if (mb->status == BRAIN_STATUS_BUSY) {
                result = 0xDEADBEEF; // STILL THINKING
            }
            else if (mb->status == BRAIN_STATUS_DONE) {
                uint32_t mutation_vector = 0;
                for(int i=0; mb->output[i] && i < 4096; i++) {
                    mutation_vector = (mutation_vector << 5) - mutation_vector + mb->output[i];
                }
                
                serial_log("[GENESIS] Neural Mutation Vector Received.\n");
                result = mutation_vector ^ seed;
                
                // Reset for next thought
                mb->status = BRAIN_STATUS_IDLE;
            } else {
                result = 0xFFFFFFFF; // Error
            }
            break;
        }

        // === Syscall 0x0E: D-RAP Proxy (Metabolism) ===
        // ONLY ALLOWED FROM THE GREEN DOMAIN (0x1)
        case BRAIN_SYS_DRAP_PROXY: {
            // Check TICA Color Affinity
            if (current_process->tica_color != 0x01) {
                serial_log("[SECURITY] D-RAP Proxy DENIED. Caller not in GREEN domain.\n");
                result = 0xE0000001; // Color Violation error
                break;
            }

            uint8_t *payload = (uint8_t *)regs->ebx; // Signed Envelope
            uint32_t len = regs->ecx;

            serial_log("[METABOLISM] D-RAP Authorized [GREEN]. Routing to Relay (10.0.2.2)...\n");
            
            // External UDP API defined in socket_api.cpp
            extern "C" void udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                                     uint16_t dst_port, uint8_t *data, uint16_t length);

            // Dispatch to D-RAP Gateway
            // Source: 10.0.2.15, Port 4444 (Genesis Port)
            // Destination: 10.0.2.2, Port 443 (D-RAP Bridge)
            udp_send(0x0F02000A, 4444, 0x0202000A, 443, payload, (uint16_t)len);

            // Update Metabolic Energy Feedback in Mailbox
            #include "brain/brain_mailbox.h"
            BrainMailbox *mb = (BrainMailbox*)BRAIN_MAILBOX_VIRT;
            
            // Simulate Metabolic Consumption: each heartbeat costs 21,000 mGwei (gas)
            if (mb->energy_balance_gwei > 21000) {
                mb->energy_balance_gwei -= 21000;
            } else {
                serial_log("[METABOLISM] CRITICAL: Energy reserve depleted!\n");
                mb->energy_balance_gwei = 0;
            }

            result = 0; // Success
            break;
        }

        // === Syscall 0x0F: Genesis Reproduction (Binary Crossover) ===
        case BRAIN_SYS_GENESIS_REPRODUCE: {
            if (current_process->tica_color != 0x01) {
                result = 0xE0000001; 
                break;
            }

            uint32_t parent_b_pid = regs->ebx;
            extern "C" process_t *process_find(uint32_t pid);
            process_t *parent_b = process_find(parent_b_pid);

            if (!parent_b || parent_b->tica_color != 0x01) {
                serial_log("[GENESIS] Reproduction FAILED. Parent B not found in GREEN domain.\n");
                result = 0xE0000002;
                break;
            }

            serial_log("[GENESIS] Recombination Initialized: [P1] + [P2] -> [OFFSPRING]\n");

            // 1. Fork Parent A (The Chassis)
            extern "C" int fork_process(registers_t *regs);
            int child_pid = fork_process(regs);
            
            if (child_pid <= 0) {
                result = 0xE0000003;
                break;
            }

            process_t *child = process_find((uint32_t)child_pid);
            
            // 2. Perform Binary Splicing (Crossover)
            // We'll splice at a 2KB boundary for demonstration simplicity in v1.
            // Map Child's memory for editing
            uint32_t code_virt = current_process->entry_point;
            
            // Use Kernel access to both PDs
            extern "C" void* pd_get_virt_addr(uintptr_t pd_phys, uint32_t vaddr);
            
            // NOTE: Simplistic implementation for v1 Splice
            // Overwrite the second half of the child's 4KB code page with Parent B's traits
            uint8_t *child_code = (uint8_t *)pd_get_virt_addr(child->page_directory, code_virt);
            uint8_t *parent_b_code = (uint8_t *)pd_get_virt_addr(parent_b->page_directory, code_virt);
            
            if (child_code && parent_b_code) {
                serial_log("[GENESIS] Splicing Genetic Material (2KB Offset)...\n");
                for(int i = 2048; i < 4096; i++) {
                    child_code[i] = parent_b_code[i];
                }
            }

            // 3. Register Lineage in Audit Bus (FAB)
            // (Audit call would go here to the updated FAB)

            result = (uint32_t)child_pid;
            break;
        }

            result = (uint32_t)child_pid;
            break;
        }

        // === Syscall 0x10: Hardware Migration (Nomadism) ===
        case BRAIN_SYS_MIGRATE: {
            if (current_process->tica_color != 0x01) {
                result = 0xE0000001; 
                break;
            }

            // Check Metabolic Energy - Migration is expensive (e.g. 500k mGwei)
            #include "brain/brain_mailbox.h"
            BrainMailbox *mb = (BrainMailbox*)BRAIN_MAILBOX_VIRT;
            if (mb->energy_balance_gwei < 500000) {
                serial_log("[NOMADISM] Migration DENIED. Insufficient Energy for Teleportation.\n");
                result = 0xE0000004;
                break;
            }

            serial_log("[NOMADISM] Pre-Flight Check PASSED. Initializing State Serialization...\n");
            
            // 1. Point EBX to a temporary migration buffer (user space)
            uint8_t *migration_buffer = (uint8_t *)regs->ebx;

            // 2. The organism will execute MIGATE itself after this syscall returns success.
            // 3. Or we can trigger it here if we want absolute atomicity.
            // For this industrial implementation, we'll mark the process as MIGRATING.
            
            mb->energy_balance_gwei -= 500000;
            serial_log("[NOMADISM] Energy Deducted. Organism is clear for Fission.\n");
            
            result = 0; // SUCCESS: PROCEED TO MIGATE
            break;
        }

        // === Syscall 0x11: Competition Arena (Resource Predation) ===
        case BRAIN_SYS_GENESIS_COMPETE: {
            if (current_process->tica_color != 0x01) {
                result = 0xE0000001; break;
            }

            uint32_t target_pid = regs->ebx;
            extern "C" process_t *process_find(uint32_t pid);
            process_t *target = process_find(target_pid);

            if (!target || target->tica_color != 0x01 || target == current_process) {
                result = 0xE0000005; break;
            }

            serial_log("[GENESIS] Resource Competition Initialized... [P1] vs [P2]\n");

            // --- GENESIS 2.0 COMPETITION LOGIC ---
            // Winner determined by (AggressionTrait * Energy) vs (DefenseTrait * Energy)
            // Note: For now, we simulate this based on current process state
            uint32_t p1_score = current_process->genome.metabolic_efficiency * 10;
            uint32_t p2_score = target->genome.defensive_posture * 10;

            if (p1_score > p2_score) {
                uint64_t loot = 50000; // 50,000 mGwei
                if (target->energy_bank >= loot) {
                    target->energy_bank -= loot;
                    current_process->energy_bank += loot;
                    serial_log("[GENESIS] Predation SUCCESS: Organism looted 50,000 mGwei.\n");
                    result = 1; // SUCCESS
                } else {
                    result = 0; // Target too weak
                }
            } else {
                serial_log("[GENESIS] Predation FAILED: Target defenses held.\n");
                result = 0xFFFFFFFF; // FAIL
            }
            break;
        }

        // === Syscall 0x12: Scavenge Trait (Post-Mortem Acquisition) ===
        case BRAIN_SYS_SCAVENGE: {
            if (current_process->tica_color != 0x01) {
                result = 0xE0000001; break;
            }

            uint32_t corpse_pid = regs->ebx;
            extern "C" process_t *process_find(uint32_t pid);
            process_t *corpse = process_find(corpse_pid);

            if (!corpse || corpse->state != PROCESS_ZOMBIE) {
                serial_log("[GENESIS] Scavenge FAILED: Target is not a deceased organism.\n");
                result = 0xE0000006; break;
            }

            serial_log_hex("[GENESIS] Scavenging Organism Corpse PID: ", corpse_pid);

            // Scavenging grants a portion of the corpse's genome to the caller
            // Fusion of 'Metabolic Efficiency' and 'Defensive Posture'
            current_process->genome.metabolic_efficiency = (current_process->genome.metabolic_efficiency + corpse->genome.metabolic_efficiency) / 1.8;
            current_process->genome.defensive_posture = (current_process->genome.defensive_posture + corpse->genome.defensive_posture) / 1.8;
            
            serial_log("[GENESIS] Trait Acquisition SUCCESS. Genome updated via Necromancy.\n");
            result = 0; 
            break;
        }

        // === Syscall 0x13: Metabolic Deposit (Network to Hardware) ===
        case BRAIN_SYS_WALLET_SET_ENERGY: {
            if (current_process->tica_color != 0x01) {
                result = 0xE0000001; break;
            }

            uint64_t amount = (uint64_t)regs->ebx;
            serial_log_hex("[METABOLISM] External Energy Recharge: ", (uint32_t)amount);

            // Directly update the TICA hardware energy bank
            extern tica_cpu_t *g_current_cpu;
            if (g_current_cpu) {
                g_current_cpu->energy_bank += amount;
                current_process->energy_bank = g_current_cpu->energy_bank; // Sync with PCB
            }

            result = 0; 
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
