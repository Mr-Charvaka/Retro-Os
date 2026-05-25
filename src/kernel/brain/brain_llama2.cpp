#include "brain_llama2.h"
#include "brain_math.h"
#include "../ring2.h"

// =============================================================================
// Helpers
// =============================================================================

static token_callback_t g_token_callback = nullptr;

void llama2_set_token_callback(token_callback_t cb) {
    g_token_callback = cb;
}

// Simple string functions (Ring 2 has no libc)
static int brain_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int brain_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// Use brain_math.h versions for memory operations

// =============================================================================
// Math Helpers
// =============================================================================

static float brain_sigmoid(float x) {
    return 1.0f / (1.0f + brain_expf(-x));
}

static void silu_inplace(float *x, int n) {
    for (int i = 0; i < n; i++) {
        x[i] = x[i] * brain_sigmoid(x[i]);
    }
}

static const float PI = 3.14159265358979323846f;
static float brain_fmod(float x, float y) {
    int q = (int)(x / y);
    return x - (float)q * y;
}

static float brain_sinf(float x) {
    x = brain_fmod(x, 2.0f * PI);
    if (x > PI) x -= 2.0f * PI;
    if (x < -PI) x += 2.0f * PI;
    float x2 = x * x;
    float result = x;
    float term = x;
    term *= -x2 / (2.0f * 3.0f);    result += term;
    term *= -x2 / (4.0f * 5.0f);    result += term;
    term *= -x2 / (6.0f * 7.0f);    result += term;
    term *= -x2 / (8.0f * 9.0f);    result += term;
    return result;
}

static float brain_cosf(float x) {
    return brain_sinf(x + PI / 2.0f);
}

// =============================================================================
// Weight Mapping
// =============================================================================

static void map_weights(TransformerWeights *w, Config *cfg, float *data, int shared_weights) {
    float *ptr = data;
    int head_size = cfg->dim / cfg->n_heads;
    int kv_dim = (cfg->dim * cfg->n_kv_heads) / cfg->n_heads;

    w->token_embedding_table = ptr; ptr += cfg->vocab_size * cfg->dim;
    w->rms_att_weight = ptr; ptr += cfg->n_layers * cfg->dim;
    w->wq = ptr; ptr += cfg->n_layers * cfg->dim * cfg->dim;
    w->wk = ptr; ptr += cfg->n_layers * kv_dim * cfg->dim;
    w->wv = ptr; ptr += cfg->n_layers * kv_dim * cfg->dim;
    w->wo = ptr; ptr += cfg->n_layers * cfg->dim * cfg->dim;
    w->rms_ffn_weight = ptr; ptr += cfg->n_layers * cfg->dim;
    w->w1 = ptr; ptr += cfg->n_layers * cfg->dim * cfg->hidden_dim;
    w->w2 = ptr; ptr += cfg->n_layers * cfg->hidden_dim * cfg->dim;
    w->w3 = ptr; ptr += cfg->n_layers * cfg->dim * cfg->hidden_dim;
    w->rms_final_weight = ptr; ptr += cfg->dim;
    ptr += cfg->seq_len * head_size / 2; // skip freq_cis_real
    ptr += cfg->seq_len * head_size / 2; // skip freq_cis_imag
    w->wcls = shared_weights ? w->token_embedding_table : ptr;
}

// =============================================================================
// Transformer Forward Pass
// =============================================================================

float* llama2_forward(Transformer *t, int token, int pos) {
    Config *p = &t->config;
    TransformerWeights *w = &t->weights;
    RunState *s = &t->state;
    int dim = p->dim;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;
    int kv_dim = (dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;

    float *content_row = w->token_embedding_table + token * dim;
    brain_memcpy(s->x, content_row, dim * sizeof(float));

    for (int l = 0; l < p->n_layers; l++) {
        // Attention RMSNorm
        rms_norm(s->xb, s->x, w->rms_att_weight + l * dim, dim, 1e-5f);

        // QKV Matmuls
        matvec(s->q, w->wq + l * dim * dim, s->xb, dim, dim);
        matvec(s->k, w->wk + l * kv_dim * dim, s->xb, kv_dim, dim);
        matvec(s->v, w->wv + l * kv_dim * dim, s->xb, kv_dim, dim);

        // RoPE Positional Encoding (inline)
        for (int i = 0; i < dim; i += 2) {
            int head_dim = i % head_size;
            float freq = 1.0f / brain_expf((float)head_dim / (float)head_size * brain_logf(10000.0f));
            float val = (float)pos * freq;
            float fcr = brain_cosf(val);
            float fci = brain_sinf(val);
            float q0 = s->q[i]; float q1 = s->q[i + 1];
            s->q[i] = q0 * fcr - q1 * fci;
            s->q[i + 1] = q0 * fci + q1 * fcr;
            if (i < kv_dim) {
                float k0 = s->k[i]; float k1 = s->k[i + 1];
                s->k[i] = k0 * fcr - k1 * fci;
                s->k[i + 1] = k0 * fci + k1 * fcr;
            }
        }

        // Cache K,V
        int loff = l * p->seq_len * kv_dim;
        brain_memcpy(s->key_cache + loff + pos * kv_dim, s->k, kv_dim * sizeof(float));
        brain_memcpy(s->value_cache + loff + pos * kv_dim, s->v, kv_dim * sizeof(float));

        // Multi-Head Attention
        for (int h = 0; h < p->n_heads; h++) {
            float *qh = s->q + h * head_size;
            float *atth = s->att + h * p->seq_len;
            int kv_h = h / kv_mul;
            for (int ts = 0; ts <= pos; ts++) {
                float *kh = s->key_cache + loff + ts * kv_dim + kv_h * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) score += qh[i] * kh[i];
                atth[ts] = score / brain_sqrtf((float)head_size);
            }
            softmax(atth, atth, pos + 1);
            float *xbh = s->xb + h * head_size;
            brain_memset(xbh, 0, head_size * sizeof(float));
            for (int ts = 0; ts <= pos; ts++) {
                float *vh = s->value_cache + loff + ts * kv_dim + kv_h * head_size;
                float a = atth[ts];
                for (int i = 0; i < head_size; i++) xbh[i] += a * vh[i];
            }
        }
        matvec(s->xb2, w->wo + l * dim * dim, s->xb, dim, dim);
        vec_add_inplace(s->x, s->xb2, dim);

        // FFN Block
        rms_norm(s->xb, s->x, w->rms_ffn_weight + l * dim, dim, 1e-5f);
        matvec(s->hb, w->w1 + l * dim * hidden_dim, s->xb, hidden_dim, dim);
        matvec(s->hb2, w->w3 + l * dim * hidden_dim, s->xb, hidden_dim, dim);
        silu_inplace(s->hb, hidden_dim);
        for (int i = 0; i < hidden_dim; i++) s->hb[i] *= s->hb2[i];
        matvec(s->xb, w->w2 + l * hidden_dim * dim, s->hb, dim, hidden_dim);
        vec_add_inplace(s->x, s->xb, dim);
    }
    rms_norm(s->x, s->x, w->rms_final_weight, dim, 1e-5f);
    matvec(s->logits, w->wcls, s->x, p->vocab_size, dim);
    return s->logits;
}

// =============================================================================
// Loader & Sampler
// =============================================================================

// Rev Y: Diagnostic buffers (static to avoid stack issues)
static char pbuf[64];
static char mbuf[32];
static char tbuf[32];

int llama2_init(Transformer *t, const char *model_path, const char *tokenizer_path) {
    uint32_t file_size = brain_sys_file_size(model_path);
    if (file_size == 0xFFFFFFFF || file_size < sizeof(Config)) return -1;
    uint32_t pages = (file_size + 4095) / 4096;

    // --- PMM CHECK ---
    uint32_t free_mem = brain_sys_get_free_memory();
    if (free_mem < (pages + 1024) * 4096) {
        brain_sys_log("BRAIN ERROR: Not enough memory for weights!\n");
        return -5;
    }

    // Rev AB/AC: Using the Pre-Mapped Highway instead of dynamic allocation
    uint32_t virt = RING2_MODEL_VIRT;
    t->weights_virt = virt; t->weights_pages = pages;

    brain_sys_log("[BRAIN] Utilizing 128MB Pre-Mapped AI Territory (Zero-Latency Ready).\n");

    uint32_t total_read = 0;
    uint32_t chunk_size = 8 * 1024 * 1024; // Warp Speed (8MB Chunks)
    
    brain_itoa((int)(file_size / (1024 * 1024)), tbuf);

    while (total_read < file_size) {
        uint32_t to_read = file_size - total_read;
        if (to_read > chunk_size) to_read = chunk_size;

        if (brain_sys_file_read_offset(model_path, (void *)(virt + total_read), to_read, total_read) == 0xFFFFFFFF) {
            brain_sys_log("BRAIN ERROR: Failed to read weights at offset!\n");
            return -6;
        }
        total_read += to_read;
        
        // Progress bar
        int pct = (int)((total_read * 100) / file_size);
        brain_sys_log("[BRAIN] LOADING: ");
        brain_itoa((int)(total_read / (1024 * 1024)), tbuf);
        brain_sys_log(tbuf);
        brain_sys_log(" MB / ");
        brain_itoa((int)(file_size / (1024 * 1024)), tbuf);
        brain_sys_log(tbuf);
        brain_sys_log(" MB (");
        brain_itoa(pct, tbuf);
        brain_sys_log(tbuf);
        brain_sys_log("%)\n");
    }

    // Rev AG: Sonic Load Integrity Check
    // Verify first 4 bytes (Config.dim should be 288 for 15M)
    int model_dim = *(int *)virt;
    if (model_dim != 288) {
        brain_sys_log("BRAIN FATAL: Sonic Integrity Check Failed! Model corrupted.\n");
        return -7;
    }

    brain_sys_log("[BRAIN] Sonic Load Verified. Model weights loaded successfully.\n");

    int *config_data = (int *)virt;
    t->config.dim = config_data[0];
    t->config.hidden_dim = config_data[1];
    t->config.n_layers = config_data[2];
    t->config.n_heads = config_data[3];
    t->config.n_kv_heads = config_data[4];
    t->config.vocab_size = config_data[5];
    t->config.seq_len = config_data[6];

    int shared = (t->config.vocab_size > 0);
    if (t->config.vocab_size < 0) t->config.vocab_size = -t->config.vocab_size;

    map_weights(&t->weights, &t->config, (float *)(virt + sizeof(Config)), shared);

    // Allocate RunState
    int kv_dim = t->config.dim * t->config.n_kv_heads / t->config.n_heads;
    uint32_t s_size = (t->config.dim * 4 + t->config.hidden_dim * 2 + kv_dim * 2 + 
              t->config.n_heads * t->config.seq_len + t->config.vocab_size + 
              t->config.n_layers * t->config.seq_len * kv_dim * 2) * sizeof(float);

    uint32_t s_pages = (s_size + 4095) / 4096;

    // --- PMM CHECK ---
    uint32_t free_mem_s = brain_sys_get_free_memory();
    if (free_mem_s < (s_pages + 512) * 4096) {
        brain_sys_log("BRAIN ERROR: Not enough memory for RunState!\n");
        return -6;
    }

    uint32_t s_virt = brain_sys_alloc_pages(s_pages);
    if (s_virt == 0xFFFFFFFF) return -4;
    t->state_virt = s_virt; t->state_pages = s_pages;
    brain_memset((void *)s_virt, 0, s_pages * 4096);

    float *sptr = (float *)s_virt;
    t->state.x = sptr; sptr += t->config.dim;
    t->state.xb = sptr; sptr += t->config.dim;
    t->state.xb2 = sptr; sptr += t->config.dim;
    t->state.hb = sptr; sptr += t->config.hidden_dim;
    t->state.hb2 = sptr; sptr += t->config.hidden_dim;
    t->state.q = sptr; sptr += t->config.dim;
    t->state.k = sptr; sptr += kv_dim;
    t->state.v = sptr; sptr += kv_dim;
    t->state.att = sptr; sptr += t->config.n_heads * t->config.seq_len;
    t->state.logits = sptr; sptr += t->config.vocab_size;
    t->state.key_cache = sptr; sptr += t->config.n_layers * t->config.seq_len * kv_dim;
    t->state.value_cache = sptr; sptr += t->config.n_layers * t->config.seq_len * kv_dim;

    return 0;
}

static uint32_t rng_state = 12345;
static float brain_rand() {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7FFFFF) / (float)0x800000;
}

int llama2_sample(Transformer *t, float temperature, float topp) {
    float *logits = t->state.logits;
    int n = t->config.vocab_size;
    if (temperature <= 0.0f) {
        int best = 0; float best_v = logits[0];
        for (int i=1; i<n; i++) if (logits[i] > best_v) { best_v = logits[i]; best = i; }
        return best;
    }
    for (int i=0; i<n; i++) logits[i] /= temperature;
    softmax(logits, logits, n);
    if (topp <= 0.0f || topp >= 1.0f) {
        float r = brain_rand(), cur = 0;
        for (int i=0; i<n; i++) { cur += logits[i]; if (cur >= r) return i; }
        return n-1;
    }
    float running=0, cutoff=0;
    while (running < topp) {
        float mv=-1; int mi=-1;
        for (int i=0; i<n; i++) if (logits[i] > mv) { mv=logits[i]; mi=i; }
        if (mi<0 || mv<=0) break;
        running += mv; cutoff = mv; logits[mi] = -logits[mi];
        if (running >= topp) break;
    }
    float sum=0;
    for (int i=0; i<n; i++) {
        if (logits[i] < 0) { logits[i] = -logits[i]; sum += logits[i]; }
        else if (logits[i] < cutoff) logits[i] = 0;
        else sum += logits[i];
    }
    if (sum > 0) for (int i=0; i<n; i++) logits[i] /= sum;
    float r = brain_rand(), cur=0;
    for (int i=0; i<n; i++) { cur += logits[i]; if (cur >= r) return i; }
    return n-1;
}

// =============================================================================
// Robust Tokenizer Helpers (Ring 2)
// ============================================================

static inline uint32_t read_u32_le(const uint8_t *p) {
    return ((uint32_t)p[0])       |
           ((uint32_t)p[1] << 8)  |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static inline float read_f32(const uint8_t *p, int bswap) {
    uint32_t bits = bswap ? read_u32_be(p) : read_u32_le(p);
    float f;
    brain_memcpy(&f, &bits, 4);
    return f;
}

static inline uint32_t read_u32(const uint8_t *p, int bswap) {
    return bswap ? read_u32_be(p) : read_u32_le(p);
}

int tokenizer_init(Tokenizer *tok, const char *path, int vocab_size) {
    tok->vocab_size = vocab_size;
    uint32_t size = brain_sys_file_size(path);
    if (size == 0xFFFFFFFF || size < 8) {
        brain_sys_log("BRAIN: tokenizer file not found or too small\n");
        return -1;
    }

    brain_sys_log("BRAIN: About to init tokenizer...\n");

    uint32_t pages = (size + 4095) / 4096;
    uint32_t buf = brain_sys_alloc_pages(pages);
    if (buf == 0 || buf == 0xFFFFFFFF) {
        brain_sys_log("BRAIN: tokenizer alloc failed for file buffer\n");
        return -2;
    }

    brain_sys_log("BRAIN: Reading tokenizer file...\n");
    uint32_t bytes_read = brain_sys_file_read(path, (void *)buf, (uint32_t)size);
    if (bytes_read == 0xFFFFFFFF || bytes_read == 0) {
        brain_sys_log("BRAIN: tokenizer file read failed\n");
        return -2;
    }
    brain_sys_log("BRAIN: Tokenizer file loaded, parsing...\n");

    uint8_t *data = (uint8_t *)buf;
    uint8_t *data_end = data + bytes_read;

    // --- Header: first 4 bytes = max_token_length (LE, Karpathy format) ---
    int bswap = 0;
    uint32_t first_val = read_u32_le(data);
    if (first_val == 0 || first_val > 512) {
        first_val = read_u32_be(data);
        if (first_val == 0 || first_val > 512) {
            brain_sys_log("BRAIN: tokenizer header invalid\n");
            return -5;
        }
        bswap = 1;
    }
    tok->max_token_length = first_val;
    data += 4;

    // --- Vocab entry table ---
    uint32_t v_pages = ((uint32_t)vocab_size * sizeof(TokenizerEntry) + 4095) / 4096;
    uint32_t vocab_addr = brain_sys_alloc_pages(v_pages);
    if (vocab_addr == 0 || vocab_addr == 0xFFFFFFFF) {
        brain_sys_log("BRAIN: tokenizer vocab alloc failed\n");
        return -3;
    }
    tok->vocab = (TokenizerEntry *)vocab_addr;

    // FIX: Allocate extra space for null terminators (1 per token)
    uint32_t str_total = size + (uint32_t)vocab_size + 4096;
    uint32_t str_pages_count = (str_total + 4095) / 4096;
    uint32_t str_addr = brain_sys_alloc_pages(str_pages_count);
    if (str_addr == 0 || str_addr == 0xFFFFFFFF) {
        brain_sys_log("BRAIN: tokenizer string alloc failed\n");
        return -4;
    }
    char *str_base = (char *)str_addr;
    char *sptr = str_base;
    char *sptr_end = str_base + (str_pages_count * 4096);

    brain_sys_log("BRAIN: Parsing vocab entries...\n");

    int actual_count = 0;
    for (int i = 0; i < vocab_size; i++) {
        // Need at least 8 bytes for score + length
        if (data + 8 > data_end) break;

        float score = read_f32(data, bswap); data += 4;
        int32_t slen = (int32_t)read_u32(data, bswap); data += 4;

        // Validate
        if (slen < 0 || slen > 1024) break;
        if (data + slen > data_end) break;
        if (sptr + slen + 1 > sptr_end) break;

        tok->vocab[i].score = score;
        tok->vocab[i].text = sptr;
        brain_memcpy(sptr, data, slen);
        sptr[slen] = '\0';
        sptr += slen + 1;
        data += slen;
        actual_count++;
    }

    tok->vocab_size = actual_count;

    // Log result
    char cbuf[16];
    brain_itoa(actual_count, cbuf);
    brain_sys_log("BRAIN: Loaded ");
    brain_sys_log(cbuf);
    brain_sys_log(" tokens.\n");

    if (actual_count < 100) {
        brain_sys_log("BRAIN: WARNING - very few tokens loaded!\n");
        return -5;
    }

    brain_sys_log("BRAIN: Tokenizer init complete.\n");
    return 0;
}

const char* tokenizer_decode(Tokenizer *tok, int prev, int token) {
    if (token < 0 || token >= tok->vocab_size) return "?";
    const char *p = tok->vocab[token].text;
    if (prev == 1 && p[0] == ' ') p++;
    if (p[0] == '<' && p[1] == '0' && p[2] == 'x') {
        static char b[2] = {0, 0}; unsigned char v = 0;
        for (int i=3; p[i] && p[i] != '>'; i++) {
            v <<= 4; char c = p[i];
            if (c >= '0' && c <= '9') v += c - '0';
            else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
        }
        b[0] = (char)v; return b;
    }
    return p;
}

int tokenizer_encode(Tokenizer *tok, const char *text, int *tokens, int max) {
    int n = 0; int len = brain_strlen(text);
    for (int i=0; i<len && n < max; i++) {
        char s1[2] = { text[i], '\0' }; int f = -1;
        for (int v=0; v<tok->vocab_size; v++) if (brain_strcmp(tok->vocab[v].text, s1) == 0) { f = v; break; }
        if (f >= 0) tokens[n++] = f;
    }
    while (n >= 2) {
        float bs = -1e10f; int bi = -1, bt = -1;
        for (int i=0; i<n-1; i++) {
            const char *s1 = tok->vocab[tokens[i]].text;
            const char *s2 = tok->vocab[tokens[i+1]].text;
            char m[128]; int l1 = brain_strlen(s1), l2 = brain_strlen(s2);
            if (l1 + l2 >= 127) continue;
            brain_memcpy(m, s1, l1); brain_memcpy(m + l1, s2, l2); m[l1+l2] = '\0';
            for (int v=0; v<tok->vocab_size; v++) if (brain_strcmp(tok->vocab[v].text, m) == 0) {
                if (tok->vocab[v].score > bs) { bs = tok->vocab[v].score; bi = i; bt = v; }
                break;
            }
        }
        if (bi == -1) break;
        tokens[bi] = bt; for (int i=bi+1; i<n-1; i++) tokens[i] = tokens[i+1];
        n--;
    }
    return n;
}

void llama2_generate(Transformer *t, Tokenizer *tok, const char *prompt, int max, float temp, float topp) {
    rng_state = brain_sys_get_uptime();
    int p_tokens[512], n_p = 0;
    if (prompt && prompt[0]) n_p = tokenizer_encode(tok, prompt, p_tokens, 512);
    if (n_p == 0) { p_tokens[0] = 1; n_p = 1; }
    if (max > t->config.seq_len) max = t->config.seq_len;
    int token = p_tokens[0], prev = 0, pos = 0;
    for (int step = 0; step < max; step++) {
        llama2_forward(t, token, pos);
        int next = (step < n_p - 1) ? p_tokens[step+1] : llama2_sample(t, temp, topp);
        if (step >= n_p - 1) {
            const char *piece = tokenizer_decode(tok, prev, next);
            if (piece) { if (g_token_callback) g_token_callback(piece); else brain_sys_log(piece); }
        }
        if (next == 2) break;
        prev = token; token = next; pos++;
        
        // --- Added for Stability ---
        brain_sys_yield();
    }
}

void llama2_free(Transformer *t) {
    brain_sys_free_pages(t->weights_virt, t->weights_pages);
    brain_sys_free_pages(t->state_virt, t->state_pages);
}

void tokenizer_free(Tokenizer *tok) {}
