// =============================================================================
// brain_api.cpp — Ring 2 AI Brain Entry & API
// =============================================================================

#include "brain_llama2.h"
#include "brain_mailbox.h"
#include "../ring2.h"
#include "../pmm.h"
#include "../paging.h"
#include "../../drivers/serial.h"

// Global transformer and tokenizer
static Transformer g_transformer;
static Tokenizer g_tokenizer;
static int g_model_loaded = 0;

// Output capture hook
static char *g_output_ptr = nullptr;
static int g_output_remaining = 0;

static void brain_output_token(const char *piece) {
    // Only log to serial if we are not in a quiet mode
    // (We'll use g_output_ptr to determine if we are capturing to buffer only)
    if (piece && !g_output_ptr) brain_sys_log(piece);
    
    if (g_output_ptr && g_output_remaining > 1) {
        while (*piece && g_output_remaining > 1) {
            *g_output_ptr++ = *piece++;
            g_output_remaining--;
        }
        *g_output_ptr = '\0';
    }
}

static int brain_load_model(void) {
    // PRIORITIZE DEDICATED DRIVE (/D) for Rev L Parallelism
    const char *model_path = "/D/model.bin";
    const char *token_path = "/D/token.bin";

    if (brain_sys_file_size(model_path) == 0xFFFFFFFF) {
        brain_sys_log("BRAIN: /D/model.bin not found, falling back to /C/model.bin...\n");
        model_path = "/C/model.bin";
        token_path = "/C/token.bin";
    }

    uint32_t size = brain_sys_file_size(model_path);
    if (size == 0xFFFFFFFF) {
        brain_sys_log("BRAIN FATAL: model.bin not found on /C or /D!\n");
        return -1;
    }

    char sbuf[32];
    brain_itoa((int)size, sbuf);
    brain_sys_log("Loading LLaMA-2 15M from ");
    brain_sys_log(model_path);
    brain_sys_log(" (Size: ");
    brain_sys_log(sbuf);
    brain_sys_log(" bytes)...\n");

    int ret = llama2_init(&g_transformer, model_path, token_path);
    if (ret != 0) {
        char buf[32];
        brain_itoa(ret, buf);
        brain_sys_log("Error in llama2_init: ");
        brain_sys_log(buf);
        brain_sys_log("\n");
        return ret;
    }
    ret = tokenizer_init(&g_tokenizer, token_path, g_transformer.config.vocab_size);
    brain_sys_log("BRAIN: tokenizer_init returned: ");
    char rbuf[16];
    brain_itoa(ret, rbuf);
    brain_sys_log(rbuf);
    brain_sys_log("\n");

    if (ret != 0) return ret;
    g_model_loaded = 1;
    brain_sys_log("Model and Tokenizer ready!\n");

    // CONFIG DUMP
    Config *cfg = &g_transformer.config;
    char buf[64];
    brain_sys_log("--- AI Brain Config ---\n");
    brain_sys_log("  Dim:        "); brain_itoa(cfg->dim, buf);        brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  Hidden:     "); brain_itoa(cfg->hidden_dim, buf); brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  Layers:     "); brain_itoa(cfg->n_layers, buf);   brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  Heads:      "); brain_itoa(cfg->n_heads, buf);    brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  KV Heads:   "); brain_itoa(cfg->n_kv_heads, buf); brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  Vocab:      "); brain_itoa(cfg->vocab_size, buf); brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("  Seq Len:    "); brain_itoa(cfg->seq_len, buf);    brain_sys_log(buf); brain_sys_log("\n");
    brain_sys_log("-----------------------\n");

    return 0;
}

extern "C" volatile int g_brain_activated;

// =============================================================================
// Ring 2 Entry (Called from ring2.cpp : brain_main)
// =============================================================================
extern "C" void ring2_brain_entry(void) {
    brain_sys_log("Retro-OS AI Brain Mobilized (Standby on Core-2)\n");
    
    // Rev S: Wait for user activation
    while (!g_brain_activated) {
        // Yield to let Core 2 be "quiet"
        for (volatile int i = 0; i < 10000; i++);
    }

    brain_sys_log("BRAIN: Signal Received! Loading Model...\n");

    if (brain_load_model() != 0) {
        brain_sys_log("BRAIN: Model load failed!\n");
        return;
    }
    
    uint32_t ring = brain_sys_verify_ring();
    if (ring != 2) {
        char buf[32];
        brain_itoa((int)ring, buf);
        brain_sys_log("WARNING: Not running in Ring 2! Current Ring: ");
        brain_sys_log(buf);
        brain_sys_log("\n");
    }

    brain_sys_log("[BRAIN] Utilizing 128MB Pre-Mapped AI Territory (Zero-Latency Ready).\n");

    BrainMailbox *mb = brain_get_mailbox();
    mb->status = BRAIN_STATUS_IDLE;
    llama2_set_token_callback(brain_output_token);

    // ============ INFERENCE IGNITION ============
    brain_sys_log("\n[BRAIN] Inference Engine Ignited. Starting Self-Test...\n");
    brain_sys_log("Prompt: 'Once upon a time'\n");

    mb->output[0] = '\0';
    g_output_ptr = mb->output;
    g_output_remaining = sizeof(mb->output);

    uint32_t t0 = brain_sys_get_time_ms();
    llama2_generate(&g_transformer, &g_tokenizer, "Once upon a time", 32, 0.8f, 0.9f);
    uint32_t t1 = brain_sys_get_time_ms();

    g_output_ptr = nullptr;

    brain_sys_log("\n\n=== SELF-TEST RESULT ===\n");
    brain_sys_log("Output: ");
    brain_sys_log(mb->output);
    brain_sys_log("\n");

    char tbuf[32];
    brain_itoa((int)(t1 - t0), tbuf);
    brain_sys_log("Time: ");
    brain_sys_log(tbuf);
    brain_sys_log("ms\n");
    brain_sys_log("========================\n\n");
    // ============ END TEST ============

    brain_sys_log("AI Brain Standing By (Mailbox Loop Active)\n");

    while (1) {
        if (mb->command == BRAIN_CMD_GENERATE) {
            mb->status = BRAIN_STATUS_BUSY;
            mb->output[0] = '\0';
            g_output_ptr = mb->output;
            g_output_remaining = sizeof(mb->output);

            float temp = (float)mb->temperature_x100 / 100.0f;
            float topp = (float)mb->topp_x100 / 100.0f;
            int max_tok = (int)mb->max_tokens;

            uint32_t t_start = brain_sys_get_time_ms();
            llama2_generate(&g_transformer, &g_tokenizer, mb->prompt, max_tok, temp, topp);
            uint32_t t_end = brain_sys_get_time_ms();
            mb->time_ms = t_end - t_start;

            g_output_ptr = nullptr;
            mb->command = BRAIN_CMD_NONE;
            mb->status = BRAIN_STATUS_DONE;
        }
        // Yield/Pause
        for(volatile int i=0; i<1000; i++);
        brain_sys_get_uptime(); 
    }
}

// =============================================================================
// Ring 0 Mailbox Setup (Called from Kernel.cpp)
// =============================================================================
extern "C" void brain_mailbox_init(void) {
    // Allocate one physical page for the mailbox
    void *phys = pmm_alloc_block();
    if (!phys) {
        serial_log("FATAL: Cannot allocate mailbox page\n");
        return;
    }

    // Map at fixed virtual address (0xD2010000), accessible from Ring 2 AND Ring 3
    // Flags: 0x01 (Present) | 0x02 (RW) | 0x04 (User)
    paging_map((uintptr_t)phys, BRAIN_MAILBOX_VIRT, 0x07);

    // Zero it out
    uint8_t *ptr = (uint8_t *)BRAIN_MAILBOX_VIRT;
    for (int i = 0; i < 4096; i++) {
        ptr[i] = 0;
    }

    serial_log("[KERNEL] Brain mailbox mapped at 0xD2010000\n");
}
