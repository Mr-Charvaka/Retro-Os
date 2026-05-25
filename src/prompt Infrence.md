You're thinking of **PAE (Physical Address Extension)**. Here is the breakdown of how it works and why it's a big deal for 32-bit operating systems like Retro-OS:

### 1. The 32-Bit Problem (The 4GB Ceiling)

In a standard 32-bit OS, everything uses 32-bit numbers. Since $2^{32} = 4,294,967,296$, the CPU can only "address" or "point to" 4 Gigabytes of memory.

- Even if you plug in 16GB of RAM, a standard 32-bit kernel literally doesn't have enough bits to represent addresses higher than 4GB.

### 2. How PAE Fixes This

Introduced by Intel with the Pentium Pro, **PAE** rewrites the rules for how the CPU talks to memory:

- **64-Bit Entries**: It expands the size of page table entries from 32 bits to **64 bits**. This adds "extra lanes" to the memory highway.
- **Physical Address: 36-bit (or more)**: Instead of a 32-bit physical address, PAE typically uses **36 bits**.
- **The Result**: $2^{36} = 64$ Gigabytes. The CPU can now physically "see" and use up to **64GB of RAM**.

### 3. The Catch: Virtual vs. Physical

There is an important distinction you have to remember:

- **Physical RAM**: Can be **64GB** with PAE.
- **Virtual Space (Per Process)**: Still only **4GB**.
  Even with PAE, a single application can still only use 4GB of memory at a time. However, the OS can now run **many** 4GB applications at once without them "stepping on each other" in the physical RAM.

### 4. How the CPU "Walks" PAE Memory

Standard 32-bit paging uses **2 levels** of tables to find memory. PAE adds a **3rd level**:

1. **PDPT** (Page Directory Pointer Table) ← *The new "VIP" level*
2. **Page Directory**
3. **Page Table**

### Why this matters for Retro-OS?

Currently, Retro-OS uses standard **2-level 32-bit paging**.

- If you were to run a huge "Brain" model (Ring 2) that needed 8GB of neural weights, you would **need** PAE for the kernel to even load those weights into physical RAM.
- **The Implementation Cost**: To enable it, we would have to:
  1. Rewrite `init_paging()` to create 64-bit table entries instead of 32-bit ones.
  2. Set a specific bit in a CPU control register (**CR4.PAE**).
  3. Update the PMM (Physical Memory Manager) to handle addresses larger than 4GB.

**Do you want to explore implementing PAE, or are you happy with our current 4GB limit while we focus on the Brain Layer logic?** (Note: PAE is a complex, deep-kernel architectural change!)











































# Option A: AI Inference Engine — Complete Implementation Plan

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      RING 3 (User Apps)                      │
│                                                              │
│   Shell / GUI App / Browser / Any Process                    │
│       │                                                      │
│       │  "Hello, tell me about the weather"                  │
│       │                                                      │
│       ▼                                                      │
│   LCALL 0x43  (Call Gate — CPU enforced)                     │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                     RING 2 (AI Brain)                        │
│                                                              │
│  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌───────────┐  │
│  │ 2A: Math │→│2B:Tokenizer│→│2C: Weights│→│2D: Forward │  │
│  │Foundation│  │ BPE encode │  │  Loading  │  │   Pass    │  │
│  └──────────┘  │ BPE decode │  │  GPT-2    │  │ Attention │  │
│                └───────────┘  │  124M      │  │ FFN       │  │
│                               └──────────┘  │ LayerNorm │  │
│                                              └─────┬─────┘  │
│                                                    │         │
│                               ┌───────────┐  ┌────▼──────┐  │
│                               │2F:CallGate│←│2E: Text   │  │
│                               │Integration│  │ Generation│  │
│                               └───────────┘  │ Sampling  │  │
│                                              └───────────┘  │
│                                                              │
│  INT 0x81 (DPL=2) for kernel services:                      │
│    - Memory allocation (model weights)                       │
│    - Disk I/O (load model file)                              │
│    - Logging (debug output)                                  │
│    - Timer (performance measurement)                         │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                     RING 0 (Kernel)                          │
│  PMM / VFS / FAT32 / Timer / Serial — existing services     │
└─────────────────────────────────────────────────────────────┘
```

---

## File Structure (All New Files)

```
src/kernel/brain/
├── brain_math.h              ← Step 2A
├── brain_math.cpp            ← Step 2A
├── brain_tokenizer.h         ← Step 2B
├── brain_tokenizer.cpp       ← Step 2B
├── brain_model.h             ← Step 2C
├── brain_model.cpp           ← Step 2C
├── brain_forward.h           ← Step 2D
├── brain_forward.cpp         ← Step 2D
├── brain_generate.h          ← Step 2E
├── brain_generate.cpp        ← Step 2E
├── brain_api.h               ← Step 2F
├── brain_api.cpp             ← Step 2F
└── brain_test.cpp            ← Step 2G

assets/
├── vocab.bin                 ← Step 2B (BPE vocabulary)
├── merges.bin                ← Step 2B (BPE merge rules)
└── gpt2-small.bin            ← Step 2C (model weights ~500MB or quantized ~62-124MB)
```

---

## Step 2A: Math Foundation

### Purpose

Build every mathematical operation the neural network needs from scratch. No stdlib, no math.h — pure C running in Ring 2.

### Files

```
src/kernel/brain/brain_math.h
src/kernel/brain/brain_math.cpp
```

### Functions Required

```
CATEGORY            FUNCTION                    WHY NEEDED
──────────          ──────────────              ──────────────────────────────
Utility Math        brain_fabsf(x)              Absolute value
                    brain_sqrtf(x)              Layer normalization
                    brain_expf(x)               Softmax, GELU
                    brain_tanhf(x)              GELU activation
                    brain_logf(x)               Loss computation, sampling

Memory Ops          brain_memset(ptr,val,size)  Zero-fill buffers
                    brain_memcpy(dst,src,size)  Copy weight blocks
                    brain_memset_float(p,v,n)   Initialize float arrays

Matrix Multiply     matmul(out,a,b,m,k,n)       95% of compute time
                    matvec(out,mat,vec,m,k)      Weight × activation (most common)

Vector Ops          vec_add(out,a,b,n)           Bias addition
                    vec_add_inplace(a,b,n)       Residual connections
                    vec_mul(out,a,b,n)           Element-wise gating
                    vec_scale(out,a,s,n)         Attention scaling
                    vec_dot(a,b,n)               Similarity computation
                    vec_max(a,n)                 Softmax stability
                    vec_argmax(a,n)              Greedy token selection
                    vec_copy(dst,src,n)          Buffer management

Activations         gelu(out,x,n)               GPT-2 activation function
                    gelu_inplace(x,n)           In-place variant
                    relu(out,x,n)               Fallback activation
                    sigmoid(out,x,n)            Gating mechanisms

Normalization       layer_norm(out,x,w,b,n,eps) Every transformer layer uses this
                    rms_norm(out,x,w,n,eps)     LLaMA-style alternative

Softmax             softmax(out,x,n)            Attention + final token probs
                    softmax_inplace(x,n)        In-place variant

Matrix Utils        mat_transpose(out,in,m,n)   Attention computation

Testing             brain_math_test()           Self-validation (6+ tests)
```

### Key Implementation Details

```
- matmul uses LOOP TILING (32×32 blocks) for cache efficiency
- vec_dot uses 4-way accumulator to break dependency chains
- brain_expf uses range reduction: exp(x) = 2^n × exp(r)
- softmax subtracts max value FIRST for numerical stability
- brain_sqrtf uses Newton's method (10 iterations)
- All functions handle n=0 gracefully (no-op)
- NO heap allocation — all buffers passed by caller
```

### Test Criteria

```
TEST 1: 2×2 matmul → exact integer result
TEST 2: vec_add → exact result
TEST 3: softmax → sum ≈ 1.0 (within 0.01)
TEST 4: layer_norm → output mean ≈ 0.0 (within 0.01)
TEST 5: brain_sqrtf(16) ≈ 4.0 (within 0.01)
TEST 6: brain_expf(1.0) ≈ 2.718 (within 0.01)
```

### Dependencies

```
Depends on:  Nothing (self-contained)
Used by:     Steps 2D, 2E (forward pass, generation)
```

---

## Step 2B: Tokenizer (BPE)

### Purpose

Convert human text to token IDs (numbers) and back. GPT-2 uses Byte Pair Encoding (BPE) with a vocabulary of 50,257 tokens.

### Files

```
src/kernel/brain/brain_tokenizer.h
src/kernel/brain/brain_tokenizer.cpp
assets/vocab.bin
assets/merges.bin
```

### Data Structures

```cpp
// Single vocabulary entry
struct VocabEntry {
    char text[32];      // Token text (e.g., "Hello", " the", "Ġworld")
    uint16_t len;       // Length of text
};

// Merge rule for BPE
struct MergeRule {
    uint16_t token_a;   // First token ID
    uint16_t token_b;   // Second token ID
    uint16_t result;    // Merged token ID
};

// Tokenizer state
struct Tokenizer {
    VocabEntry* vocab;       // 50257 entries
    MergeRule*  merges;      // ~50000 merge rules
    int         vocab_size;  // 50257
    int         merge_count; // Number of merge rules
    uint16_t    bos_token;   // Beginning of sequence (50256)
    uint16_t    eos_token;   // End of sequence (50256)
    uint16_t    pad_token;   // Padding token
};
```

### Functions Required

```
FUNCTION                                    PURPOSE
──────────────────────────                  ─────────────────────────
tokenizer_init(tok, vocab_data, merge_data) Load vocab + merges from buffers
tokenizer_encode(tok, text, out_ids, max)   Text → token IDs
tokenizer_decode(tok, ids, count, out_buf)  Token IDs → text
tokenizer_encode_len(tok, text)             Count tokens without encoding
tokenizer_free(tok)                         Release memory
```

### Encoding Algorithm (BPE)

```
INPUT:  "Hello world"

STEP 1: Convert to bytes
  → ['H','e','l','l','o',' ','w','o','r','l','d']

STEP 2: Initialize each byte as its own token
  → [72, 101, 108, 108, 111, 32, 119, 111, 114, 108, 100]

STEP 3: Apply merge rules in priority order
  Rule 1: ('l','l') → 'll'     → merge positions 2,3
  Rule 2: ('He','ll') → 'Hell' → merge positions 0,1
  Rule 3: ('Hell','o') → 'Hello' → merge
  ... continue until no more merges apply

STEP 4: Output final token IDs
  → [15496, 995]   (Hello = 15496, Ġworld = 995)
```

### File Formats

```
vocab.bin:
  [4 bytes] magic: "VCB1"
  [4 bytes] vocab_size (50257)
  [4 bytes] max_token_len (32)
  For each entry:
    [2 bytes] text length
    [N bytes] text data (variable length, max 32)

merges.bin:
  [4 bytes] magic: "MRG1"
  [4 bytes] merge_count
  For each merge:
    [2 bytes] token_a
    [2 bytes] token_b
    [2 bytes] result_token
```

### Test Criteria

```
TEST 1: encode("Hello") → contains token 15496
TEST 2: decode(encode("Hello world")) → "Hello world"
TEST 3: encode("") → 0 tokens
TEST 4: Round-trip 10 different strings lossless
TEST 5: Special characters handled (newlines, unicode bytes)
```

### Dependencies

```
Depends on:  brain_math.h (brain_memcpy, brain_memset)
             INT 0x81 (to load vocab.bin and merges.bin from disk)
Used by:     Steps 2E, 2F (generation, API)
```

---

## Step 2C: Model Format and Weight Loading

### Purpose

Define the GPT-2 model structure and load 124 million parameters from disk into Ring 2 memory.

### Files

```
src/kernel/brain/brain_model.h
src/kernel/brain/brain_model.cpp
assets/gpt2-small.bin
```

### GPT-2 Small Architecture

```
PARAMETER               VALUE       WHAT IT MEANS
─────────────           ─────       ──────────────────────────────
n_vocab                 50257       Vocabulary size
n_ctx                   1024        Maximum sequence length
n_embd                  768         Embedding dimension (hidden size)
n_head                  12          Number of attention heads
n_layer                 12          Number of transformer layers
head_dim                64          768 / 12 = dimension per head
```

### Model Data Structure

```cpp
struct GPT2Config {
    int n_vocab;    // 50257
    int n_ctx;      // 1024
    int n_embd;     // 768
    int n_head;     // 12
    int n_layer;    // 12
};

struct GPT2Layer {
    // Attention
    float* ln1_weight;     // [768]         layer norm 1
    float* ln1_bias;       // [768]
    float* attn_qkv_w;    // [768 × 2304]  Q,K,V projection (combined)
    float* attn_qkv_b;    // [2304]
    float* attn_proj_w;   // [768 × 768]   output projection
    float* attn_proj_b;   // [768]

    // Feed-Forward Network
    float* ln2_weight;     // [768]         layer norm 2
    float* ln2_bias;       // [768]
    float* ffn_up_w;      // [768 × 3072]  expand to 4× hidden
    float* ffn_up_b;      // [3072]
    float* ffn_down_w;    // [3072 × 768]  project back down
    float* ffn_down_b;    // [768]
};

struct GPT2Model {
    GPT2Config config;

    // Embeddings
    float* wte;            // [50257 × 768]  token embeddings
    float* wpe;            // [1024 × 768]   position embeddings

    // Transformer layers
    GPT2Layer layers[12];

    // Final layer norm
    float* ln_f_weight;    // [768]
    float* ln_f_bias;      // [768]

    // Total parameter count
    int total_params;
};
```

### Memory Budget

```
COMPONENT                     SIZE (float32)    SIZE (INT8)
──────────────────────        ──────────────    ───────────
Token embeddings (wte)        50257×768×4=148MB  37MB
Position embeddings (wpe)     1024×768×4=3MB     0.75MB
12 Transformer layers:
  Layer norm (×4)             12×768×4×4=144KB   36KB
  Attention QKV weights       12×768×2304×4=81MB 20MB
  Attention QKV bias          12×2304×4=108KB    27KB
  Attention projection        12×768×768×4=27MB  6.75MB
  Attention proj bias         12×768×4=36KB      9KB
  FFN up weights              12×768×3072×4=108MB 27MB
  FFN up bias                 12×3072×4=144KB    36KB
  FFN down weights            12×3072×768×4=108MB 27MB
  FFN down bias               12×768×4=36KB      9KB
Final layer norm              768×4×2=6KB        1.5KB
─────────────────────────────────────────────────────────
TOTAL (float32):              ~497MB
TOTAL (INT8 quantized):       ~124MB
TOTAL (INT4 quantized):       ~62MB

Available RAM:                383MB
→ INT8 fits comfortably ✅
→ float32 may require streaming or careful allocation
```

### Weight File Format

```
gpt2-small.bin:
  HEADER (64 bytes):
    [4 bytes]  magic: "GPT2"
    [4 bytes]  version: 1
    [4 bytes]  n_vocab
    [4 bytes]  n_ctx
    [4 bytes]  n_embd
    [4 bytes]  n_head
    [4 bytes]  n_layer
    [4 bytes]  quantization: 0=f32, 1=f16, 2=int8, 3=int4
    [4 bytes]  total_params
    [28 bytes] reserved (zeros)

  DATA (sequential, no padding):
    wte:           [n_vocab × n_embd] values
    wpe:           [n_ctx × n_embd] values
    For each layer 0..11:
      ln1_weight:  [n_embd]
      ln1_bias:    [n_embd]
      attn_qkv_w:  [n_embd × 3*n_embd]
      attn_qkv_b:  [3*n_embd]
      attn_proj_w: [n_embd × n_embd]
      attn_proj_b: [n_embd]
      ln2_weight:  [n_embd]
      ln2_bias:    [n_embd]
      ffn_up_w:    [n_embd × 4*n_embd]
      ffn_up_b:    [4*n_embd]
      ffn_down_w:  [4*n_embd × n_embd]
      ffn_down_b:  [n_embd]
    ln_f_weight:   [n_embd]
    ln_f_bias:     [n_embd]
```

### Functions Required

```
FUNCTION                              PURPOSE
────────────────────────              ──────────────────────────
model_init(model, config)             Allocate all weight buffers
model_load(model, file_data, size)    Parse header + load weights
model_free(model)                     Release all memory
model_param_count(model)              Return total parameter count
model_memory_usage(model)             Return bytes used
model_validate(model)                 Check weights are reasonable
```

### Loading Process

```
1. Ring 2 calls INT 0x81 → BRAIN_SYS_FILE_READ to load gpt2-small.bin
2. Kernel (Ring 0) reads from FAT32 via VFS
3. Returns data pointer to Ring 2
4. Ring 2 parses header, validates magic/version
5. Ring 2 allocates weight buffers via INT 0x81 → BRAIN_SYS_ALLOC_PAGE
6. Ring 2 copies weights from file buffer into structured model
7. File buffer can be released
```

### Test Criteria

```
TEST 1: model_init allocates all buffers (no NULL pointers)
TEST 2: model_load parses header correctly
TEST 3: model_param_count returns ~124M
TEST 4: model_validate checks weight ranges (no NaN/Inf)
TEST 5: First 10 values of wte match known reference values
```

### Dependencies

```
Depends on:  brain_math.h (brain_memcpy)
             INT 0x81 (file I/O, memory allocation)
Used by:     Step 2D (forward pass)
```

---

## Step 2D: Forward Pass (The Actual Inference)

### Purpose

Wire the math operations into the GPT-2 transformer architecture. Given a sequence of token IDs, compute the probability distribution over the next token.

### Files

```
src/kernel/brain/brain_forward.h
src/kernel/brain/brain_forward.cpp
```

### Forward Pass Pipeline

```
INPUT: token_ids = [15496, 995]  ("Hello world")

STEP 1: TOKEN + POSITION EMBEDDING
  For each position i, token t:
    hidden[i] = wte[t] + wpe[i]
  Result: hidden[seq_len × 768]

STEP 2: FOR EACH TRANSFORMER LAYER (×12):

  2a. LAYER NORM 1
      normed = layer_norm(hidden, ln1_w, ln1_b)

  2b. MULTI-HEAD SELF-ATTENTION
      QKV = normed × attn_qkv_w + attn_qkv_b    [seq × 2304]
      Split into Q[seq×768], K[seq×768], V[seq×768]
      Reshape to [n_head × seq × head_dim] = [12 × seq × 64]

      For each head h:
        scores = Q_h × K_h^T / sqrt(64)           [seq × seq]
        Apply causal mask (future = -infinity)
        probs = softmax(scores)                    [seq × seq]
        attn_out_h = probs × V_h                  [seq × 64]

      Concatenate heads → attn_out[seq × 768]
      projected = attn_out × attn_proj_w + attn_proj_b

  2c. RESIDUAL CONNECTION 1
      hidden = hidden + projected

  2d. LAYER NORM 2
      normed2 = layer_norm(hidden, ln2_w, ln2_b)

  2e. FEED-FORWARD NETWORK
      up = normed2 × ffn_up_w + ffn_up_b          [seq × 3072]
      activated = gelu(up)
      down = activated × ffn_down_w + ffn_down_b   [seq × 768]

  2f. RESIDUAL CONNECTION 2
      hidden = hidden + down

STEP 3: FINAL LAYER NORM
  hidden = layer_norm(hidden, ln_f_w, ln_f_b)

STEP 4: LOGITS COMPUTATION
  logits = hidden[last_token] × wte^T             [50257]
  (Reuse token embeddings as output projection)

OUTPUT: logits[50257]  (unnormalized log-probabilities)
```

### Scratch Memory (Working Buffers)

```
BUFFER              SIZE                PURPOSE
──────              ────                ───────
hidden              seq_len × 768      Main activation tensor
normed              seq_len × 768      After layer norm
qkv                 seq_len × 2304     Q, K, V concatenated
attn_scores         n_head × seq × seq Attention weights
attn_out            seq_len × 768      Attention output
ffn_hidden          seq_len × 3072     FFN intermediate
logits              50257              Output probabilities

KV Cache (for generation):
  key_cache         n_layer × seq × 768    Cached keys
  value_cache       n_layer × seq × 768    Cached values
```

### Functions Required

```
FUNCTION                                    PURPOSE
────────────────────────────────            ──────────────────────
forward_init(state, model, max_seq)         Allocate scratch buffers
forward_pass(state, model, tokens, n_tok)   Full forward pass
forward_step(state, model, new_token, pos)  Single token (with KV cache)
forward_free(state)                         Release scratch memory
attention_single_head(Q,K,V,out,seq,dim)    One attention head
attention_multi_head(...)                    All heads + projection
ffn_block(...)                              Feed-forward network block
```

### KV Cache Optimization

```
WITHOUT KV CACHE (naive):
  Every new token recomputes attention for ALL previous tokens
  Token 100 recomputes tokens 0-99
  O(n²) per token, O(n³) total

WITH KV CACHE:
  Store K and V from previous tokens
  New token only computes its own Q, uses cached K,V
  O(n) per token, O(n²) total
  10-100× faster for long sequences
```

### Test Criteria

```
TEST 1: Embedding lookup produces correct dimensions
TEST 2: Layer norm output has mean ≈ 0
TEST 3: Attention scores sum to 1.0 per row (after softmax)
TEST 4: Causal mask blocks future positions
TEST 5: Full forward pass produces 50257 logits
TEST 6: Logits are finite (no NaN/Inf)
TEST 7: Top-1 token for "The capital of France is" → " Paris"
```

### Dependencies

```
Depends on:  brain_math.h (ALL operations)
             brain_model.h (weight access)
Used by:     Step 2E (generation)
```

---

## Step 2E: Text Generation Loop

### Purpose

Generate text autoregressively — one token at a time, feeding each output back as input.

### Files

```
src/kernel/brain/brain_generate.h
src/kernel/brain/brain_generate.cpp
```

### Generation Algorithm

```
INPUT:  prompt = "The meaning of life is"
OUTPUT: "The meaning of life is to find your purpose and..."

ALGORITHM:

1. Tokenize prompt → token_ids = [464, 3616, 286, 1204, 318]

2. Run forward pass on all prompt tokens
   → logits[50257] (prediction for next token)

3. LOOP (until max_tokens or EOS):
   a. Apply temperature:  logits[i] /= temperature
   b. Apply top-k:        zero out all but top K logits
   c. Apply softmax:      logits → probabilities
   d. Sample next token from distribution
      - Greedy: argmax(probabilities)
      - Random: weighted random sample
      - Top-p (nucleus): sample from smallest set summing to p
   e. Append sampled token to sequence
   f. Run forward_step with KV cache (just new token)
   g. Decode token to text, output to caller

4. Detokenize full sequence → output text
```

### Sampling Strategies

```
STRATEGY      HOW IT WORKS                        QUALITY
──────────    ──────────────────                   ───────
Greedy        Always pick highest probability      Repetitive but deterministic
Temperature   Scale logits by T before softmax     T<1 = more focused, T>1 = more random
Top-K         Zero out all but K highest logits    Prevents unlikely tokens
Top-P         Keep smallest set summing to P prob  Adaptive cutoff
Combined      Temperature + Top-K + Top-P          Best quality
```

### Functions Required

```
FUNCTION                                        PURPOSE
──────────────────────────────────              ──────────────────────
generate_init(gen, model, tokenizer)            Setup generation state
generate_text(gen, prompt, out, max_tokens)     Main entry point
generate_step(gen)                              Generate one token
sample_greedy(logits, n)                        Argmax sampling
sample_topk(logits, n, k, temperature)          Top-K + temperature
sample_topp(logits, n, p, temperature)          Nucleus sampling
generate_free(gen)                              Cleanup
```

### Generation State

```cpp
struct GenerateState {
    GPT2Model*    model;
    Tokenizer*    tokenizer;
    ForwardState* fwd;

    uint16_t*     tokens;       // Full sequence so far
    int           n_tokens;     // Current length
    int           max_tokens;   // Maximum generation length

    // Sampling parameters
    float         temperature;  // 0.7 default
    int           top_k;        // 40 default
    float         top_p;        // 0.9 default

    // RNG state (simple xorshift)
    uint32_t      rng_state;
};
```

### Test Criteria

```
TEST 1: Greedy decode of "Hello" produces reasonable continuation
TEST 2: Temperature=0 gives deterministic output
TEST 3: Top-K=1 equals greedy
TEST 4: Generation stops at max_tokens
TEST 5: Generation stops at EOS token
TEST 6: Output is valid UTF-8 / ASCII
```

### Dependencies

```
Depends on:  brain_math.h (softmax, vec_scale, vec_argmax)
             brain_tokenizer.h (encode/decode)
             brain_forward.h (forward_pass, forward_step)
Used by:     Step 2F (API)
```

---

## Step 2F: Call Gate Integration (Ring 3 → Ring 2 API)

### Purpose

Create the public API that Ring 3 applications use to talk to the AI Brain. Ring 3 sends a text prompt through the call gate, Ring 2 processes it, and returns the AI response.

### Files

```
src/kernel/brain/brain_api.h
src/kernel/brain/brain_api.cpp
```

### API Design

```
RING 3 APPLICATION:
  brain_request_t req = {
      .prompt = "What is 2+2?",
      .max_tokens = 100,
      .temperature = 0.7
  };
  brain_response_t resp;
  LCALL 0x43 → ring2_gate_handler(&req, &resp);
  printf("AI says: %s\n", resp.text);
```

### Request/Response Structures

```cpp
// Shared between Ring 3 and Ring 2 (in shared memory page)
struct brain_request_t {
    uint32_t magic;          // 0xBRA1N001
    uint32_t command;        // BRAIN_CMD_GENERATE, BRAIN_CMD_STATUS, etc.
    char     prompt[2048];   // Input text
    uint32_t max_tokens;     // Max tokens to generate
    float    temperature;    // Sampling temperature
    int      top_k;          // Top-K value
    float    top_p;          // Top-P value
};

struct brain_response_t {
    uint32_t magic;          // 0xBRA1NRSP
    uint32_t status;         // 0 = success, nonzero = error
    char     text[4096];     // Generated text
    uint32_t tokens_used;    // How many tokens were generated
    uint32_t time_ms;        // How long inference took
    uint32_t error_code;     // Error details if status != 0
};

// Commands
#define BRAIN_CMD_GENERATE   1    // Generate text
#define BRAIN_CMD_STATUS     2    // Get brain status
#define BRAIN_CMD_RESET      3    // Clear context
#define BRAIN_CMD_TOKENIZE   4    // Just tokenize (debug)
#define BRAIN_CMD_BENCHMARK  5    // Run speed test
```

### Call Gate Handler Flow

```
RING 3 → LCALL 0x43 → CPU switches to Ring 2

ring2_gate_handler:
  1. Read request from shared memory page
  2. Validate magic, command, prompt length
  3. Switch on command:
     GENERATE:
       a. tokenizer_encode(prompt) → token_ids
       b. generate_text(token_ids) → output_ids
       c. tokenizer_decode(output_ids) → text
       d. Copy text to response buffer
     STATUS:
       a. Return model loaded, memory used, etc.
     RESET:
       a. Clear KV cache
       b. Reset generation state
  4. Write response to shared memory page
  5. LRET → back to Ring 3
```

### Functions Required

```
FUNCTION                                PURPOSE
────────────────────────────────        ──────────────────────
brain_api_init(model, tokenizer)        Initialize API layer
ring2_gate_handler()                    Call gate entry point
brain_handle_generate(req, resp)        Process generate command
brain_handle_status(req, resp)          Return brain status
brain_handle_reset(req, resp)           Clear context
brain_api_get_shared_page()             Get shared memory address
```

### Shared Memory Design

```
Ring 3 and Ring 2 need a shared memory page to pass data.
  - Kernel maps one physical page at a known virtual address
  - Ring 3 can write request, read response
  - Ring 2 can read request, write response
  - Page permissions: Ring 2 R/W, Ring 3 R/W

Address: 0xD3000000 (in Ring 2's region)
  Also mapped at a Ring 3 address (e.g., 0xBFF00000)
```

### Test Criteria

```
TEST 1: LCALL from Ring 3 reaches Ring 2 handler
TEST 2: STATUS command returns model info
TEST 3: GENERATE command returns text
TEST 4: Invalid command returns error
TEST 5: Oversized prompt handled gracefully
TEST 6: LRET returns cleanly to Ring 3
```

### Dependencies

```
Depends on:  brain_generate.h (text generation)
             brain_tokenizer.h (encode/decode)
             brain_model.h (model state)
             Call Gate (Step 1E — already working)
Used by:     Ring 3 applications
```

---

## Step 2G: End-to-End Test

### Purpose

Prove the entire pipeline works from user input to AI response.

### Files

```
src/kernel/brain/brain_test.cpp
apps/brain_demo.c (Ring 3 test app)
```

### Test Sequence

```
TEST 1: UNIT — Math library (brain_math_test)
  All 6+ math tests pass from Ring 2

TEST 2: UNIT — Tokenizer round-trip
  encode("Hello world") → decode → "Hello world"

TEST 3: UNIT — Model loading
  Load weights, validate param count = 124M

TEST 4: INTEGRATION — Forward pass
  Input tokens → 50257 logits, all finite

TEST 5: INTEGRATION — Generation
  "The capital of France is" → " Paris" (or similar)

TEST 6: E2E — Ring 3 to Ring 2 to Ring 0 and back
  Ring 3 app sends prompt via call gate
  Ring 2 tokenizes, runs inference, detokenizes
  Response returns to Ring 3
  Ring 3 prints AI text to console

TEST 7: STRESS — Multiple requests
  10 sequential generate requests without crash

TEST 8: SECURITY — Ring 3 cannot bypass brain
  Ring 3 cannot call INT 0x81 (#GP)
  Ring 3 can only reach brain via call gate
```

### Expected Demo Output

```
$ brain "What is the speed of light?"

[BRAIN] Tokenizing prompt... 8 tokens
[BRAIN] Running inference... 12 layers × 45 tokens
[BRAIN] Generation complete in 3200ms
[BRAIN] Response:

The speed of light in a vacuum is approximately 299,792,458 
meters per second, often rounded to 300,000 km/s. This is a 
fundamental constant in physics, denoted as 'c'.

$ brain --status
Model:       GPT-2 Small (124M params)
Memory:      127MB (INT8 quantized)
Context:     53/1024 tokens used
Ring:        2 (Supervisor)
Uptime:      142 seconds
Requests:    1 served
```

---

## Implementation Order & Timeline

```
STEP  NAME                    DEPENDS ON    EFFORT     RISK
────  ────────────────────    ──────────    ──────     ────
2A    Math Foundation         Nothing       Medium     Low
2B    Tokenizer (BPE)        2A            Medium     Medium
2C    Model Weight Loading   2A, 2B        High       High
2D    Forward Pass            2A, 2C        High       High
2E    Text Generation         2B, 2D        Medium     Medium
2F    Call Gate Integration   2E, 1E        Medium     Medium
2G    End-to-End Test         ALL           Low        Low
```

```
CRITICAL PATH:
  2A → 2C → 2D → 2E → 2G
         ↗
  2A → 2B ─────────↗

2A is the foundation. 2B and 2C can be done in parallel.
2D needs both 2A and 2C. 2E needs 2B and 2D.
2F needs 2E and existing call gate. 2G tests everything.
```

---

## Risk Mitigation

```
RISK                              MITIGATION
──────────────────                ──────────────────────────────
Model too large for RAM           Use INT8 quantization (~124MB)
                                  Use INT4 if needed (~62MB)

Inference too slow                Tiled matmul (Step 2A)
                                  KV cache (Step 2D)
                                  SSE/AVX later (post-2G optimization)

Tokenizer incorrect               Test with known GPT-2 outputs
                                  Compare first 100 tokens against reference

Forward pass produces garbage      Validate against reference implementation
                                  Test each layer independently
                                  Compare logits for known inputs

File I/O from Ring 2              Add BRAIN_SYS_FILE_READ to INT 0x81
                                  Kernel reads, passes buffer to Ring 2

Out of memory during inference     Pre-calculate memory budget
                                  Fail gracefully with error response

Ring 2 crash during inference      Watchdog timer in Ring 0
                                  Kill and restart brain on timeout
```

---

## New INT 0x81 Syscalls Needed

```
EXISTING (Step 1F):
  BRAIN_SYS_LOG              0    Print to serial
  BRAIN_SYS_GET_PROCESS_COUNT 1   Process count
  BRAIN_SYS_GET_FREE_MEMORY  2    Free RAM
  BRAIN_SYS_GET_UPTIME       3    Tick count
  BRAIN_SYS_ALLOC_PAGE       4    Allocate 4KB page

NEW (needed for Step 2):
  BRAIN_SYS_FILE_OPEN        5    Open file via VFS
  BRAIN_SYS_FILE_READ        6    Read file data
  BRAIN_SYS_FILE_CLOSE       7    Close file
  BRAIN_SYS_FILE_SIZE        8    Get file size
  BRAIN_SYS_ALLOC_PAGES      9    Allocate N contiguous pages
  BRAIN_SYS_FREE_PAGES       10   Release pages
  BRAIN_SYS_GET_TIME_MS      11   Millisecond timer
  BRAIN_SYS_MAP_SHARED       12   Map shared page for Ring 3 comms
```

---

## Summary

```
TOTAL NEW FILES:           14 files
TOTAL NEW FUNCTIONS:       ~60 functions
TOTAL NEW LINES (est):     ~4000-5000 lines
LARGEST FILE:              brain_forward.cpp (~1200 lines)
MOST CRITICAL FILE:        brain_math.cpp (everything depends on it)
FIRST FILE TO BUILD:       brain_math.h + brain_math.cpp (Step 2A)
```

**Start with Step 2A. Every other step depends on it. Shall I deliver the complete code?**
