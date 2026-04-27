// =============================================================================
// brain_llama2.h — LLaMA-2 Transformer (ported from Karpathy's llama2.c)
// Runs in Ring 2 of Retro-OS. No stdlib, no libc.
// =============================================================================

#ifndef BRAIN_LLAMA2_H
#define BRAIN_LLAMA2_H

#include <stdint.h>

// =============================================================================
// Model Configuration (matches llama2.c binary header: 7 ints = 28 bytes)
// =============================================================================
struct __attribute__((packed)) Config {
    int dim;          // transformer dimension (e.g., 288)
    int hidden_dim;   // FFN hidden dimension (e.g., 768)
    int n_layers;     // number of layers (e.g., 6)
    int n_heads;      // number of query heads (e.g., 6)
    int n_kv_heads;   // number of key/value heads (can be < n_heads for GQA)
    int vocab_size;   // vocabulary size (e.g., 32000)
    int seq_len;      // max sequence length (e.g., 256)
};

// =============================================================================
// Transformer Weights
// =============================================================================
struct TransformerWeights {
    float *token_embedding_table;
    float *rms_att_weight;    // [n_layers, dim]
    float *rms_ffn_weight;    // [n_layers, dim]
    float *wq;                // [n_layers, dim, dim]
    float *wk;                // [n_layers, dim, dim]
    float *wv;                // [n_layers, dim, dim]
    float *wo;                // [n_layers, dim, dim]
    float *w1;                // [n_layers, hidden_dim, dim]
    float *w2;                // [n_layers, dim, hidden_dim]
    float *w3;                // [n_layers, hidden_dim, dim]
    float *rms_final_weight;  // [dim]
    float *wcls;              // [vocab_size, dim]
};

// =============================================================================
// Run State — Mutable buffers
// =============================================================================
struct RunState {
    float *x;          // [dim]
    float *xb;         // [dim]
    float *xb2;        // [dim]
    float *hb;         // [hidden_dim]
    float *hb2;        // [hidden_dim]
    float *q;          // [dim]
    float *k;          // [dim]
    float *v;          // [dim]
    float *att;        // [n_heads, seq_len]
    float *logits;     // [vocab_size]
    float *key_cache;   // [n_layers, seq_len, dim]
    float *value_cache; // [n_layers, seq_len, dim]
};

// =============================================================================
// Tokenizer
// =============================================================================
struct TokenizerEntry {
    char *text;
    float score;
};

struct Tokenizer {
    TokenizerEntry *vocab;
    int vocab_size;
    int max_token_length;
};

// =============================================================================
// Full Transformer
// =============================================================================
struct Transformer {
    Config config;
    TransformerWeights weights;
    RunState state;

    uint32_t weights_virt;
    uint32_t weights_pages;
    uint32_t state_virt;
    uint32_t state_pages;
};

// =============================================================================
// Public API
// =============================================================================
int llama2_init(Transformer *t, const char *model_path, const char *tokenizer_path);
float* llama2_forward(Transformer *t, int token, int pos);
int llama2_sample(Transformer *t, float temperature, float topp);
void llama2_generate(Transformer *t, Tokenizer *tok, const char *prompt,
                     int max_tokens, float temperature, float topp);
void llama2_free(Transformer *t);

int tokenizer_init(Tokenizer *tok, const char *path, int vocab_size);
void tokenizer_free(Tokenizer *tok);
const char* tokenizer_decode(Tokenizer *tok, int prev_token, int token);
int tokenizer_encode(Tokenizer *tok, const char *text, int *tokens, int max_tokens);

typedef void (*token_callback_t)(const char *piece);
void llama2_set_token_callback(token_callback_t cb);

#endif // BRAIN_LLAMA2_H
