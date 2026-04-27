// =============================================================================
// brain_math.cpp — Math Foundation for AI Inference Engine
// =============================================================================
// Ring 2 AI Brain — Step 2A
// No stdlib, no math.h — pure software floating point from scratch
//
// IMPORTANT: This runs at CPL=2 (Ring 2). It cannot use:
//   - serial_log() (Ring 0 only, uses I/O ports)
//   - Any function that does IN/OUT instructions
//   - HLT, CLI, STI
//
// For logging, we use brain_sys_log() which triggers INT 0x81
// to ask the Ring 0 kernel to print on our behalf.
// =============================================================================

#include "brain_math.h"

// =============================================================================
// LOGGING
// =============================================================================
// brain_sys_log is defined in ring2.h / brain_syscall.cpp
// It uses INT 0x81 (DPL=2) to ask Ring 0 kernel to print to serial.
// This is the ONLY way Ring 2 can produce output.
// =============================================================================

#include "../ring2.h"

// Helper: integer to decimal string for test output
static void brain_int_to_str(int val, char* buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }

    char tmp[16];
    int i = 0;
    while (val > 0 && i < 15) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }

    int pos = 0;
    if (neg) buf[pos++] = '-';
    for (int j = i - 1; j >= 0; j--)
        buf[pos++] = tmp[j];
    buf[pos] = '\0';
}

// =============================================================================
// SECTION 1: SCALAR MATH
// =============================================================================

float brain_fabsf(float x) {
    union { float f; uint32_t u; } v;
    v.f = x;
    v.u &= 0x7FFFFFFF;
    return v.f;
}

float brain_floorf(float x) {
    int i = (int)x;
    if (x < 0.0f && (float)i != x) {
        i -= 1;
    }
    return (float)i;
}

float brain_fminf(float a, float b) {
    return (a < b) ? a : b;
}

float brain_fmaxf(float a, float b) {
    return (a > b) ? a : b;
}

float brain_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;

    // IEEE 754 bit hack for initial estimate
    union { float f; uint32_t u; } v;
    v.f = x;
    v.u = (v.u >> 1) + 0x1FC00000;
    float guess = v.f;

    // Newton-Raphson: 8 iterations for full float precision
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);

    return guess;
}

float brain_expf(float x) {
    // Clamp to prevent overflow/underflow
    if (x > 88.0f) return BRAIN_FLT_MAX;
    if (x < -88.0f) return 0.0f;

    // Range reduction: exp(x) = 2^n * exp(r)
    float n = brain_floorf(x * BRAIN_INV_LN2 + 0.5f);
    float r = x - n * BRAIN_LN2;

    // Taylor series for exp(r), |r| <= ln2/2
    float r2 = r * r;
    float r3 = r2 * r;
    float r4 = r2 * r2;
    float r5 = r4 * r;
    float r6 = r3 * r3;

    float exp_r = 1.0f + r
                + r2 * 0.5f
                + r3 * 0.16666666f
                + r4 * 0.04166666f
                + r5 * 0.00833333f
                + r6 * 0.00138889f;

    // Multiply by 2^n via IEEE 754 exponent manipulation
    int ni = (int)n;
    union { float f; uint32_t u; } scale;
    scale.u = (uint32_t)((127 + ni) << 23);

    return exp_r * scale.f;
}

float brain_tanhf(float x) {
    if (x > 10.0f) return 1.0f;
    if (x < -10.0f) return -1.0f;

    float e2x = brain_expf(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}

float brain_logf(float x) {
    if (x <= 0.0f) return BRAIN_NEG_INF;

    // Decompose: x = m * 2^e where 1 <= m < 2
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 0xFF) - 127;

    // Extract mantissa into [1, 2) range
    v.u = (v.u & 0x007FFFFF) | 0x3F800000;
    float m = v.f;

    // log(m) for m in [1,2) via polynomial: log(1+t)
    float t = m - 1.0f;
    float t2 = t * t;
    float t3 = t2 * t;
    float t4 = t2 * t2;
    float t5 = t4 * t;

    float log_m = t - 0.5f * t2
                + 0.33333333f * t3
                - 0.25f * t4
                + 0.2f * t5;

    return log_m + (float)e * BRAIN_LN2;
}

// =============================================================================
// SECTION 2: MEMORY OPERATIONS
// =============================================================================

void brain_memset(void* ptr, int value, uint32_t size) {
    uint8_t* p = (uint8_t*)ptr;
    uint8_t val = (uint8_t)value;

    // Align to 4 bytes
    while (size > 0 && ((uint32_t)(uintptr_t)p & 3)) {
        *p++ = val;
        size--;
    }

    // 4-byte fills
    if (size >= 4) {
        uint32_t val32 = (uint32_t)val | ((uint32_t)val << 8)
                       | ((uint32_t)val << 16) | ((uint32_t)val << 24);
        uint32_t* p32 = (uint32_t*)p;
        while (size >= 4) {
            *p32++ = val32;
            size -= 4;
        }
        p = (uint8_t*)p32;
    }

    while (size > 0) {
        *p++ = val;
        size--;
    }
}

void brain_memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;

    // Handle overlap
    if (d > s && d < s + size) {
        d += size;
        s += size;
        while (size > 0) {
            *(--d) = *(--s);
            size--;
        }
        return;
    }

    // Forward copy — align to 4 bytes
    while (size > 0 && ((uint32_t)(uintptr_t)d & 3)) {
        *d++ = *s++;
        size--;
    }

    if (((uint32_t)(uintptr_t)s & 3) == 0) {
        uint32_t* d32 = (uint32_t*)d;
        const uint32_t* s32 = (const uint32_t*)s;
        while (size >= 4) {
            *d32++ = *s32++;
            size -= 4;
        }
        d = (uint8_t*)d32;
        s = (const uint8_t*)s32;
    }

    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

void brain_memset_float(float* ptr, float value, int count) {
    for (int i = 0; i < count; i++) {
        ptr[i] = value;
    }
}

void brain_memcpy_float(float* dst, const float* src, int count) {
    brain_memcpy(dst, src, (uint32_t)count * sizeof(float));
}

// =============================================================================
// SECTION 3: VECTOR OPERATIONS
// =============================================================================

void vec_add(float* out, const float* a, const float* b, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = a[i] + b[i];
    }
}

void vec_add_inplace(float* a, const float* b, int n) {
    int i = 0;
    for (; i + 3 < n; i += 4) {
        a[i]     += b[i];
        a[i + 1] += b[i + 1];
        a[i + 2] += b[i + 2];
        a[i + 3] += b[i + 3];
    }
    for (; i < n; i++) {
        a[i] += b[i];
    }
}

void vec_mul(float* out, const float* a, const float* b, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = a[i] * b[i];
    }
}

void vec_scale(float* out, const float* a, float scalar, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = a[i] * scalar;
    }
}

void vec_scale_inplace(float* a, float scalar, int n) {
    for (int i = 0; i < n; i++) {
        a[i] *= scalar;
    }
}

float vec_dot(const float* a, const float* b, int n) {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;

    int i = 0;
    for (; i + 3 < n; i += 4) {
        s0 += a[i]     * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }

    float tail = 0.0f;
    for (; i < n; i++) {
        tail += a[i] * b[i];
    }

    return (s0 + s1) + (s2 + s3) + tail;
}

float vec_max(const float* a, int n) {
    if (n <= 0) return BRAIN_NEG_INF;
    float mx = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i] > mx) mx = a[i];
    }
    return mx;
}

int vec_argmax(const float* a, int n) {
    if (n <= 0) return -1;
    int idx = 0;
    float mx = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i] > mx) {
            mx = a[i];
            idx = i;
        }
    }
    return idx;
}

void vec_copy(float* dst, const float* src, int n) {
    brain_memcpy_float(dst, src, n);
}

float vec_sum(const float* a, int n) {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int i = 0;
    for (; i + 3 < n; i += 4) {
        s0 += a[i];
        s1 += a[i + 1];
        s2 += a[i + 2];
        s3 += a[i + 3];
    }
    float tail = 0.0f;
    for (; i < n; i++) {
        tail += a[i];
    }
    return (s0 + s1) + (s2 + s3) + tail;
}

// =============================================================================
// SECTION 4: MATRIX OPERATIONS
// =============================================================================

void matmul(float* out, const float* a, const float* b, int m, int k, int n) {
    brain_memset(out, 0, (uint32_t)(m * n) * sizeof(float));

    for (int i0 = 0; i0 < m; i0 += BRAIN_TILE_SIZE) {
        int i_end = i0 + BRAIN_TILE_SIZE;
        if (i_end > m) i_end = m;

        for (int j0 = 0; j0 < k; j0 += BRAIN_TILE_SIZE) {
            int j_end = j0 + BRAIN_TILE_SIZE;
            if (j_end > k) j_end = k;

            for (int l0 = 0; l0 < n; l0 += BRAIN_TILE_SIZE) {
                int l_end = l0 + BRAIN_TILE_SIZE;
                if (l_end > n) l_end = n;

                for (int i = i0; i < i_end; i++) {
                    for (int j = j0; j < j_end; j++) {
                        float a_ij = a[i * k + j];
                        for (int l = l0; l < l_end; l++) {
                            out[i * n + l] += a_ij * b[j * n + l];
                        }
                    }
                }
            }
        }
    }
}

void matvec(float* out, const float* mat, const float* vec, int m, int k) {
    for (int i = 0; i < m; i++) {
        out[i] = vec_dot(&mat[i * k], vec, k);
    }
}

void mat_transpose(float* out, const float* in, int m, int n) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            out[j * m + i] = in[i * n + j];
        }
    }
}

// =============================================================================
// SECTION 5: ACTIVATION FUNCTIONS
// =============================================================================

void gelu(float* out, const float* x, int n) {
    for (int i = 0; i < n; i++) {
        float xi = x[i];
        float x3 = xi * xi * xi;
        float inner = BRAIN_SQRT2_OVER_PI * (xi + 0.044715f * x3);
        out[i] = 0.5f * xi * (1.0f + brain_tanhf(inner));
    }
}

void gelu_inplace(float* x, int n) {
    for (int i = 0; i < n; i++) {
        float xi = x[i];
        float x3 = xi * xi * xi;
        float inner = BRAIN_SQRT2_OVER_PI * (xi + 0.044715f * x3);
        x[i] = 0.5f * xi * (1.0f + brain_tanhf(inner));
    }
}

void relu(float* out, const float* x, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = (x[i] > 0.0f) ? x[i] : 0.0f;
    }
}

void sigmoid(float* out, const float* x, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = 1.0f / (1.0f + brain_expf(-x[i]));
    }
}

// =============================================================================
// SECTION 6: NORMALIZATION
// =============================================================================

void layer_norm(float* out, const float* x, const float* weight,
                const float* bias, int n, float eps) {
    float mean = 0.0f;
    for (int i = 0; i < n; i++) {
        mean += x[i];
    }
    mean /= (float)n;

    float var = 0.0f;
    for (int i = 0; i < n; i++) {
        float diff = x[i] - mean;
        var += diff * diff;
    }
    var /= (float)n;

    float inv_std = 1.0f / brain_sqrtf(var + eps);
    for (int i = 0; i < n; i++) {
        out[i] = weight[i] * ((x[i] - mean) * inv_std) + bias[i];
    }
}

void rms_norm(float* out, const float* x, const float* weight,
              int n, float eps) {
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++) {
        sum_sq += x[i] * x[i];
    }
    float inv_rms = 1.0f / brain_sqrtf(sum_sq / (float)n + eps);
    for (int i = 0; i < n; i++) {
        out[i] = weight[i] * x[i] * inv_rms;
    }
}

// =============================================================================
// SECTION 7: SOFTMAX
// =============================================================================

void softmax(float* out, const float* x, int n) {
    if (n <= 0) return;

    float mx = vec_max(x, n);

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        out[i] = brain_expf(x[i] - mx);
        sum += out[i];
    }

    if (sum > 0.0f) {
        float inv_sum = 1.0f / sum;
        for (int i = 0; i < n; i++) {
            out[i] *= inv_sum;
        }
    }
}

void softmax_inplace(float* x, int n) {
    if (n <= 0) return;

    float mx = vec_max(x, n);

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] = brain_expf(x[i] - mx);
        sum += x[i];
    }

    if (sum > 0.0f) {
        float inv_sum = 1.0f / sum;
        for (int i = 0; i < n; i++) {
            x[i] *= inv_sum;
        }
    }
}

// =============================================================================
// SECTION 8: TESTS
// =============================================================================

static int approx_eq(float a, float b, float tol) {
    return brain_fabsf(a - b) < tol;
}

int brain_math_test(void) {
    int failures = 0;

    brain_sys_log("\n=== BRAIN MATH TESTS ===\n");

    // TEST 1: matmul 2x2
    {
        float a[4] = {1, 2, 3, 4};
        float b[4] = {5, 6, 7, 8};
        float out[4] = {0};
        matmul(out, a, b, 2, 2, 2);

        if (approx_eq(out[0], 19.0f, 0.01f) &&
            approx_eq(out[1], 22.0f, 0.01f) &&
            approx_eq(out[2], 43.0f, 0.01f) &&
            approx_eq(out[3], 50.0f, 0.01f)) {
            brain_sys_log("  TEST 1 [matmul 2x2]: PASS\n");
        } else {
            brain_sys_log("  TEST 1 [matmul 2x2]: FAIL\n");
            failures++;
        }
    }

    // TEST 2: vec_add
    {
        float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        float b[4] = {10.0f, 20.0f, 30.0f, 40.0f};
        float out[4];
        vec_add(out, a, b, 4);

        if (approx_eq(out[0], 11.0f, 0.01f) &&
            approx_eq(out[1], 22.0f, 0.01f) &&
            approx_eq(out[2], 33.0f, 0.01f) &&
            approx_eq(out[3], 44.0f, 0.01f)) {
            brain_sys_log("  TEST 2 [vec_add]: PASS\n");
        } else {
            brain_sys_log("  TEST 2 [vec_add]: FAIL\n");
            failures++;
        }
    }

    // TEST 3: softmax sums to 1.0
    {
        float x[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        float out[5];
        softmax(out, x, 5);
        float sum = vec_sum(out, 5);

        if (approx_eq(sum, 1.0f, 0.01f)) {
            brain_sys_log("  TEST 3 [softmax sum=1]: PASS\n");
        } else {
            brain_sys_log("  TEST 3 [softmax sum=1]: FAIL\n");
            failures++;
        }
    }

    // TEST 4: layer_norm mean ~ 0
    {
        float x[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        float w[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        float bi[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        float out[8];
        layer_norm(out, x, w, bi, 8, BRAIN_EPSILON);
        float mean = vec_sum(out, 8) / 8.0f;

        if (approx_eq(mean, 0.0f, 0.02f)) {
            brain_sys_log("  TEST 4 [layer_norm mean~0]: PASS\n");
        } else {
            brain_sys_log("  TEST 4 [layer_norm mean~0]: FAIL\n");
            failures++;
        }
    }

    // TEST 5: sqrtf(16) = 4
    {
        float r = brain_sqrtf(16.0f);
        if (approx_eq(r, 4.0f, 0.01f)) {
            brain_sys_log("  TEST 5 [sqrtf(16)=4]: PASS\n");
        } else {
            brain_sys_log("  TEST 5 [sqrtf(16)=4]: FAIL\n");
            failures++;
        }
    }

    // TEST 6: expf(1) = e
    {
        float r = brain_expf(1.0f);
        if (approx_eq(r, BRAIN_E, 0.01f)) {
            brain_sys_log("  TEST 6 [expf(1)=e]: PASS\n");
        } else {
            brain_sys_log("  TEST 6 [expf(1)=e]: FAIL\n");
            failures++;
        }
    }

    // TEST 7: logf(e) = 1
    {
        float r = brain_logf(BRAIN_E);
        if (approx_eq(r, 1.0f, 0.01f)) {
            brain_sys_log("  TEST 7 [logf(e)=1]: PASS\n");
        } else {
            brain_sys_log("  TEST 7 [logf(e)=1]: FAIL\n");
            failures++;
        }
    }

    // TEST 8: tanhf
    {
        float t0 = brain_tanhf(0.0f);
        float t_big = brain_tanhf(100.0f);
        if (approx_eq(t0, 0.0f, 0.01f) && approx_eq(t_big, 1.0f, 0.01f)) {
            brain_sys_log("  TEST 8 [tanhf]: PASS\n");
        } else {
            brain_sys_log("  TEST 8 [tanhf]: FAIL\n");
            failures++;
        }
    }

    // TEST 9: gelu
    {
        float x[3] = {0.0f, -10.0f, 3.0f};
        float out[3];
        gelu(out, x, 3);

        if (approx_eq(out[0], 0.0f, 0.01f) &&
            approx_eq(out[1], 0.0f, 0.1f) &&
            out[2] > 2.9f && out[2] < 3.1f) {
            brain_sys_log("  TEST 9 [gelu]: PASS\n");
        } else {
            brain_sys_log("  TEST 9 [gelu]: FAIL\n");
            failures++;
        }
    }

    // TEST 10: matvec
    {
        float mat[6] = {1, 2, 3, 4, 5, 6};
        float v[3] = {1, 2, 3};
        float out[2];
        matvec(out, mat, v, 2, 3);

        if (approx_eq(out[0], 14.0f, 0.01f) &&
            approx_eq(out[1], 32.0f, 0.01f)) {
            brain_sys_log("  TEST 10 [matvec]: PASS\n");
        } else {
            brain_sys_log("  TEST 10 [matvec]: FAIL\n");
            failures++;
        }
    }

    // TEST 11: vec_dot
    {
        float a[4] = {1, 2, 3, 4};
        float b[4] = {5, 6, 7, 8};
        float r = vec_dot(a, b, 4);

        if (approx_eq(r, 70.0f, 0.01f)) {
            brain_sys_log("  TEST 11 [vec_dot]: PASS\n");
        } else {
            brain_sys_log("  TEST 11 [vec_dot]: FAIL\n");
            failures++;
        }
    }

    // TEST 12: softmax numerical stability
    {
        float x[3] = {1000.0f, 1000.0f, 1000.0f};
        float out[3];
        softmax(out, x, 3);
        float sum = vec_sum(out, 3);

        if (approx_eq(sum, 1.0f, 0.01f) &&
            approx_eq(out[0], 0.333f, 0.02f)) {
            brain_sys_log("  TEST 12 [softmax stability]: PASS\n");
        } else {
            brain_sys_log("  TEST 12 [softmax stability]: FAIL\n");
            failures++;
        }
    }

    // SUMMARY
    brain_sys_log("\n=== MATH RESULTS: ");
    if (failures == 0) {
        brain_sys_log("ALL 12 PASSED ===\n\n");
    } else {
        char buf[16];
        brain_int_to_str(failures, buf);
        brain_sys_log(buf);
        brain_sys_log(" FAILURES ===\n\n");
    }

    return failures;
}
