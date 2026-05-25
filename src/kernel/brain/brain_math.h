#ifndef BRAIN_MATH_H
#define BRAIN_MATH_H

#include <stdint.h>

// =============================================================================
// CONSTANTS
// =============================================================================

#define BRAIN_PI            3.14159265358979323846f
#define BRAIN_E             2.71828182845904523536f
#define BRAIN_SQRT2_OVER_PI 0.7978845608028654f
#define BRAIN_LN2           0.6931471805599453f
#define BRAIN_INV_LN2       1.4426950408889634f
#define BRAIN_FLT_MAX       3.402823466e+38f
#define BRAIN_NEG_INF       (-BRAIN_FLT_MAX)
#define BRAIN_EPSILON       1e-5f
#define BRAIN_TILE_SIZE     32

// =============================================================================
// SCALAR MATH
// =============================================================================

float brain_fabsf(float x);
float brain_sqrtf(float x);
float brain_expf(float x);
float brain_tanhf(float x);
float brain_logf(float x);
float brain_floorf(float x);
float brain_fminf(float a, float b);
float brain_fmaxf(float a, float b);

// =============================================================================
// MEMORY OPS
// =============================================================================

void brain_memset(void* ptr, int value, uint32_t size);
void brain_memcpy(void* dst, const void* src, uint32_t size);
void brain_memset_float(float* ptr, float value, int count);
void brain_memcpy_float(float* dst, const float* src, int count);

// =============================================================================
// VECTOR OPS
// =============================================================================

void   vec_add(float* out, const float* a, const float* b, int n);
void   vec_add_inplace(float* a, const float* b, int n);
void   vec_mul(float* out, const float* a, const float* b, int n);
void   vec_scale(float* out, const float* a, float scalar, int n);
void   vec_scale_inplace(float* a, float scalar, int n);
float  vec_dot(const float* a, const float* b, int n);
float  vec_max(const float* a, int n);
int    vec_argmax(const float* a, int n);
void   vec_copy(float* dst, const float* src, int n);
float  vec_sum(const float* a, int n);

// =============================================================================
// MATRIX OPS
// =============================================================================

void matmul(float* out, const float* a, const float* b, int m, int k, int n);
void matvec(float* out, const float* mat, const float* vec, int m, int k);
void mat_transpose(float* out, const float* in, int m, int n);

// =============================================================================
// ACTIVATIONS
// =============================================================================

void gelu(float* out, const float* x, int n);
void gelu_inplace(float* x, int n);
void relu(float* out, const float* x, int n);
void sigmoid(float* out, const float* x, int n);

// =============================================================================
// NORMALIZATION
// =============================================================================

void layer_norm(float* out, const float* x, const float* weight,
                const float* bias, int n, float eps);
void rms_norm(float* out, const float* x, const float* weight,
              int n, float eps);

// =============================================================================
// SOFTMAX
// =============================================================================

void softmax(float* out, const float* x, int n);
void softmax_inplace(float* x, int n);

// =============================================================================
// TEST
// =============================================================================

int brain_math_test(void);

#endif
