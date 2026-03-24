
import os

original_path = r'C:\Users\Deepali\Desktop\OS DEV 18-03-2026\Retro-Os-MATTERS HTTPS ACCESSIBLE\edge264_src\src\edge264_internal.h'
port_path = r'C:\Users\Deepali\Desktop\OS DEV 18-03-2026\Retro-Os-MATTERS HTTPS ACCESSIBLE\edge264_port\edge264_internal.h'

with open(original_path, 'r') as f:
    lines = f.readlines()

# 1. Update Header
header = [
    '#ifndef edge264_COMMON_H\n',
    '#define edge264_COMMON_H\n',
    '\n',
    '#undef __SSE2__\n',
    '#undef __SSE__\n',
    '#undef __MMX__\n',
    '#undef __ARM_NEON\n',
    '#undef __wasm_simd128__\n',
    '\n',
    '#include "retro_os_compat.h"\n',
    '#include "edge264.h"\n',
    '\n',
    '#define SIZE_BIT (sizeof(size_t) * 8)\n',
    '\n',
    '/* Core vector typedefs */\n',
    'typedef int8_t i8x4 __attribute__((vector_size(4)));\n',
    'typedef int8_t i8x8 __attribute__((vector_size(8)));\n',
    'typedef int8_t i8x16 __attribute__((vector_size(16)));\n',
    'typedef uint8_t u8x16 __attribute__((vector_size(16)));\n',
    'typedef int16_t i16x8 __attribute__((vector_size(16)));\n',
    'typedef uint16_t u16x8 __attribute__((vector_size(16)));\n',
    'typedef int32_t i32x4 __attribute__((vector_size(16)));\n',
    'typedef uint32_t u32x4 __attribute__((vector_size(16)));\n',
    'typedef int64_t i64x2 __attribute__((vector_size(16)));\n',
    'typedef uint64_t u64x2 __attribute__((vector_size(16)));\n',
    'typedef int16_t i16x16 __attribute__((vector_size(32)));\n',
    'typedef uint16_t u16x16 __attribute__((vector_size(32)));\n',
    'typedef int32_t i32x8 __attribute__((vector_size(32)));\n',
    'typedef int32_t i32x16 __attribute__((vector_size(64)));\n',
    '\n'
]

# 2. Part A (preserving structs and offsets)
# Skip original typedefs (lines 29-50) and includes (lines 1-28)
part_a = lines[50:588]
part_a += [
    '#define INIT_PX(p, stride) size_t _stride = (stride); uint8_t * _p = (uint8_t*)(p);\n',
    '#define PX(x, y) (_p + (x) + (int)(y) * (int)_stride)\n'
]
part_a += lines[597:680]
part_a += [
    '#define loada32x4(p0, p1, p2, p3) (i32x4){*(int32_t *)(p0), *(int32_t *)(p1), *(int32_t *)(p2), *(int32_t *)(p3)}\n',
    '#define loada64x2(p0, p1) (i64x2){*(int64_t *)(p0), *(int64_t *)(p1)}\n',
    '#define loadu32x4 loada32x4\n',
    '#define loadu64x2 loada64x2\n'
]

# 3. Scalar Replacement with Unions
# Note: all interleavers return i8x16 but can return other types if needed.
# We cast to i8x16 at the end to satisfy assignment to i8x16.
scalar_block = [
    '#define HAVE_SIMD 0\n',
    '#define set8(x) ((i8x16){(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x)})\n',
    '#define set16(x) ((i16x8){(x),(x),(x),(x),(x),(x),(x),(x)})\n',
    '#define set32(x) ((i32x4){(x),(x),(x),(x)})\n',
    '#define set64(x) ((i64x2){(x),(x)})\n',
    '\n',
    'typedef union { i8x16 v8; u8x16 u8; i16x8 v16; u16x8 u16; i32x4 v32; u32x4 u32; i64x2 v64; u64x2 u64; } edge264_vec;\n',
    '\n',
    '#define abs8(a) ({ edge264_vec _va; _va.v8 = (a); i8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.v8[_i] < 0) ? -_va.v8[_i] : _va.v8[_i]; _r; })\n',
    '#define abs16(a) ({ edge264_vec _va; _va.v16 = (a); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = (_va.v16[_i] < 0) ? -_va.v16[_i] : _va.v16[_i]; _r; })\n',
    '#define broadcast8(a, i) ({ edge264_vec _va; _va.v8 = (a); ((i8x16)set8(_va.v8[(i)])); })\n',
    '#define broadcast16(a, i) ({ edge264_vec _va; _va.v16 = (a); ((i16x8)set16(_va.v16[(i)])); })\n',
    '#define broadcast32(a, i) ({ edge264_vec _va; _va.v32 = (a); ((i32x4)set32(_va.v32[(i)])); })\n',
    '\n',
    '#define min8(a, b) ({ edge264_vec _va, _vb; _va.v8 = (a); _vb.v8 = (b); i8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.v8[_i] < _vb.v8[_i]) ? _va.v8[_i] : _vb.v8[_i]; _r; })\n',
    '#define max8(a, b) ({ edge264_vec _va, _vb; _va.v8 = (a); _vb.v8 = (b); i8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.v8[_i] > _vb.v8[_i]) ? _va.v8[_i] : _vb.v8[_i]; _r; })\n',
    '#define min16(a, b) ({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = (_va.v16[_i] < _vb.v16[_i]) ? _va.v16[_i] : _vb.v16[_i]; _r; })\n',
    '#define max16(a, b) ({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = (_va.v16[_i] > _vb.v16[_i]) ? _va.v16[_i] : _vb.v16[_i]; _r; })\n',
    '#define minu8(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.u8 = (u8x16)(b); u8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.u8[_i] < _vb.u8[_i]) ? _va.u8[_i] : _vb.u8[_i]; _r; }))\n',
    '#define maxu8(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.u8 = (u8x16)(b); u8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.u8[_i] > _vb.u8[_i]) ? _va.u8[_i] : _vb.u8[_i]; _r; }))\n',
    '\n',
    '#define loadu32(p) ({ i32x4 _r = {}; memcpy(&_r, (p), 4); _r; })\n',
    '#define loadu64(p) ({ i64x2 _r = {}; memcpy(&_r, (p), 8); _r; })\n',
    '#define loadu128(p) ({ i8x16 _r = {}; memcpy(&_r, (p), 16); _r; })\n',
    '\n',
    '#define shuffle(a, m) ((i8x16)({ edge264_vec _va, _vm; _va.v8 = (a); _vm.v8 = (m); i8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = _va.v8[_vm.v8[_i] & 15]; _r; }))\n',
    '#define packs32(a, b) ((i16x8)({ edge264_vec _va, _vb; _va.v32 = (a); _vb.v32 = (b); i16x8 _r; for(int _i=0; _i<4; _i++) { \\\n',
    '  _r[_i] = (_va.v32[_i] < -32768) ? -32768 : (_va.v32[_i] > 32767) ? 32767 : _va.v32[_i]; \\\n',
    '  _r[_i+4] = (_vb.v32[_i] < -32768) ? -32768 : (_vb.v32[_i] > 32767) ? 32767 : _vb.v32[_i]; \\\n',
    '} _r; }))\n',
    '#define packs16(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i8x16 _r; for(int _i=0; _i<8; _i++) { \\\n',
    '  _r[_i] = (_va.v16[_i] < -128) ? -128 : (_va.v16[_i] > 127) ? 127 : _va.v16[_i]; \\\n',
    '  _r[_i+8] = (_vb.v16[_i] < -128) ? -128 : (_vb.v16[_i] > 127) ? 127 : _vb.v16[_i]; \\\n',
    '} _r; }))\n',
    '#define packus16(a, b) ((u8x16)({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); u8x16 _r; for(int _i=0; _i<8; _i++) { \\\n',
    '  _r[_i] = (_va.v16[_i] < 0) ? 0 : (_va.v16[_i] > 255) ? 255 : _va.v16[_i]; \\\n',
    '  _r[_i+8] = (_vb.v16[_i] < 0) ? 0 : (_vb.v16[_i] > 255) ? 255 : _vb.v16[_i]; \\\n',
    '} _r; }))\n',
    '\n',
    '#define ifelse_mask(v, t, f) ({ \\\n',
    '  typeof(t) _t1 = (t); typeof(f) _f1 = (f); typeof(v) _v1 = (v); typeof(t) _r; \\\n',
    '  uint8_t *_rt = (uint8_t *)&_r, *_tt = (uint8_t *)&_t1, *_ft = (uint8_t *)&_f1, *_vt = (uint8_t *)&_v1; \\\n',
    '  for(size_t _i=0; _i<sizeof(t); _i++) _rt[_i] = (_tt[_i] & _vt[_i]) | (_ft[_i] & ~_vt[_i]); _r; \\\n',
    '})\n',
    '#define ifelse_msb(v, t, f) ifelse_mask((i8x16)(v) >> 7, t, f)\n',
    '\n',
    '#define broadcasthi32(a) ((i16x8)({ edge264_vec _va; _va.v32 = (a); i16x8 _r; for(int _i=0; _i<4; _i++) { _r[_i] = _r[_i+4] = (int16_t)(_va.v32[_i] >> 16); } _r; }))\n',
    '#define broadcastlo32(a) ((i16x8)({ edge264_vec _va; _va.v32 = (a); i16x8 _r; for(int _i=0; _i<4; _i++) { _r[_i] = _r[_i+4] = (int16_t)_va.v32[_i]; } _r; }))\n',
    '#define cvtlo16u32(a) ((u32x4)({ edge264_vec _va; _va.u16 = (u16x8)(a); u32x4 _r; for(int _i=0; _i<4; _i++) _r[_i] = _va.u16[_i]; _r; }))\n',
    '#define cvthi16u32(a) ((u32x4)({ edge264_vec _va; _va.u16 = (u16x8)(a); u32x4 _r; for(int _i=0; _i<4; _i++) _r[_i] = _va.u16[_i+4]; _r; }))\n',
    '#define cvtuf32(v) cvtlo16u32(v)\n',
    '#define cvtlo8u16(a) ((u16x8)({ edge264_vec _va; _va.u8 = (u8x16)(a); u16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = _va.u8[_i]; _r; }))\n',
    '#define cvthi8u16(a) ((u16x8)({ edge264_vec _va; _va.u8 = (u8x16)(a); u16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = _va.u8[_i+8]; _r; }))\n',
    '#define cvtlo8s16(a) ((i16x8)({ edge264_vec _va; _va.v8 = (i8x16)(a); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = _va.v8[_i]; _r; }))\n',
    '\n',
    '#define shrrs32(a, sh, i) ({ edge264_vec _va; _va.v32 = (a); i32x4 _r; for(int _i=0; _i<4; _i++) _r[_i] = (_va.v32[_i] + (1 << ((sh) - 1))) >> (sh); _r; })\n',
    '#define shr16(a, i) (((i8x16)(a)) >> (i))\n',
    '#define shrrs16(a, i) ({ edge264_vec _va; _va.v16 = (a); i16x8 _r; int _sh = (int)(i); int32_t _add = (1 << (_sh - 1)); for(int _j=0; _j<8; _j++) _r[_j] = (((int32_t)_va.v16[_j] + _add) >> _sh); _r; })\n',
    '#define addlou8s16(a, b) ({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.v16 = (i16x8)(b); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = _va.u8[_i] + _vb.v16[_i]; _r; })\n',
    '#define addhiu8s16(a, b) ({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.v16 = (i16x8)(b); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = _va.u8[_i+8] + _vb.v16[_i]; _r; })\n',
    '\n',
    '#define adds16(a, b) ({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i16x8 _r; for(int _i=0; _i<8; _i++) { \\\n',
    '  int32_t _v = _va.v16[_i] + _vb.v16[_i]; \\\n',
    '  _r[_i] = (_v < -32768) ? -32768 : (_v > 32767) ? 32767 : _v; \\\n',
    '} _r; })\n',
    '#define subs16(a, b) ({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i16x8 _r; for(int _i=0; _i<8; _i++) { \\\n',
    '  int32_t _v = _va.v16[_i] - _vb.v16[_i]; \\\n',
    '  _r[_i] = (_v < -32768) ? -32768 : (_v > 32767) ? 32767 : _v; \\\n',
    '} _r; })\n',
    '#define addu8(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.u8 = (u8x16)(b); u8x16 _r; for(int _i=0; _i<16; _i++) { \\\n',
    '  uint16_t _v = (uint16_t)_va.u8[_i] + (uint16_t)_vb.u8[_i]; \\\n',
    '  _r[_i] = (_v > 255) ? 255 : _v; \\\n',
    '} _r; }))\n',
    '#define subu8(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.u8 = (u8x16)(b); u8x16 _r; for(int _i=0; _i<16; _i++) { \\\n',
    '  int16_t _v = (int16_t)_va.u8[_i] - (int16_t)_vb.u8[_i]; \\\n',
    '  _r[_i] = (_v < 0) ? 0 : _v; \\\n',
    '} _r; }))\n',
    '\n',
    '#define avgu8(a, b) ((i8x16)({ edge264_vec _va, _vb; _va.u8 = (u8x16)(a); _vb.u8 = (u8x16)(b); u8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = ((uint16_t)_va.u8[_i] + (uint16_t)_vb.u8[_i] + 1) >> 1; _r; }))\n',
    '#define sum8(a) ({ edge264_vec _va; _va.u8 = (u8x16)(a); uint16_t _s = 0; for(int _i=0; _i<16; _i++) _s += _va.u8[_i]; (u16x8){_s}; })\n',
    '#define sum8h8(a) ({ edge264_vec _va; _va.u8 = (u8x16)(a); uint16_t _s = 0; for(int _i=8; _i<16; _i++) _s += _va.u8[_i]; (u16x8){_s}; })\n',
    '#define hadd16(a, b) ({ edge264_vec _va, _vb; _va.v16 = (a); _vb.v16 = (b); i16x8 _r; for(int _i=0; _i<4; _i++) { _r[_i] = _va.v16[_i*2] + _va.v16[_i*2+1]; _r[_i+4] = _vb.v16[_i*2] + _vb.v16[_i*2+1]; } _r; })\n',
    '#define shrru16(a, i) ({ edge264_vec _va; _va.u16 = (u16x8)(a); u16x8 _r; uint16_t _add = (1 << ((int)(i) - 1)); for(int _j=0; _j<8; _j++) _r[_j] = (_va.u16[_j] + _add) >> (int)(i); _r; })\n',
    '\n',
    '#define trnlo32(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v32 = (a); _vb.v32 = (b); _r.v32[0]=_va.v32[0]; _r.v32[1]=_vb.v32[0]; _r.v32[2]=_va.v32[2]; _r.v32[3]=_vb.v32[2]; _r.v8; }))\n',
    '#define trnhi32(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v32 = (a); _vb.v32 = (b); _r.v32[0]=_va.v32[1]; _r.v32[1]=_vb.v32[1]; _r.v32[2]=_va.v32[3]; _r.v32[3]=_vb.v32[3]; _r.v8; }))\n',
    '#define unziplo32(a, b) trnlo32(a, b)\n',
    '#define unziphi32(a, b) trnhi32(a, b)\n',
    '\n',
    '#define ziplo8(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v8 = (a); _vb.v8 = (b); for(int _i=0; _i<8; _i++) { _r.v8[_i*2] = _va.v8[_i]; _r.v8[_i*2+1] = _vb.v8[_i]; } _r.v8; }))\n',
    '#define ziphi8(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v8 = (a); _vb.v8 = (b); for(int _i=0; _i<8; _i++) { _r.v8[_i*2] = _va.v8[_i+8]; _r.v8[_i*2+1] = _vb.v8[_i+8]; } _r.v8; }))\n',
    '\n',
    '#define ziplo16(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v16 = (a); _vb.v16 = (b); for(int _i=0; _i<4; _i++) { _r.v16[_i*2] = _va.v16[_i]; _r.v16[_i*2+1] = _vb.v16[_i]; } _r.v8; }))\n',
    '#define ziphi16(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v16 = (a); _vb.v16 = (b); for(int _i=0; _i<4; _i++) { _r.v16[_i*2] = _va.v16[_i+4]; _r.v16[_i*2+1] = _vb.v16[_i+4]; } _r.v8; }))\n',
    '\n',
    '#define ziplo32(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v32 = (a); _vb.v32 = (b); _r.v32[0]=_va.v32[0]; _r.v32[1]=_vb.v32[0]; _r.v32[2]=_va.v32[1]; _r.v32[3]=_vb.v32[1]; _r.v8; }))\n',
    '#define ziphi32(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v32 = (a); _vb.v32 = (b); _r.v32[0]=_va.v32[2]; _r.v32[1]=_vb.v32[2]; _r.v32[2]=_va.v32[3]; _r.v32[3]=_vb.v32[3]; _r.v8; }))\n',
    '#define ziplo64(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v64 = (a); _vb.v64 = (b); _r.v64[0]=_va.v64[0]; _r.v64[1]=_vb.v64[0]; _r.v8; }))\n',
    '#define ziphi64(a, b) ((i8x16)({ edge264_vec _va, _vb, _r; _va.v64 = (a); _vb.v64 = (b); _r.v64[0]=_va.v64[1]; _r.v64[1]=_vb.v64[1]; _r.v8; }))\n',
    '\n',
    '#define zipper8 ziplo8\n',
    '#define zipper16 ziplo16\n',
    '\n',
    '#define cmpeq8(a, b) ({ edge264_vec _va, _vb; _va.v8 = (a); _vb.v8 = (b); i8x16 _r; for(int _i=0; _i<16; _i++) _r[_i] = (_va.v8[_i] == _vb.v8[_i]) ? -1 : 0; _r; })\n',
    '#define shr128(a, i) ({ edge264_vec _va; _va.v8 = (a); i8x16 _r; int _bytes = (i); for(int _i=0; _i<16; _i++) _r[_i] = (_i + _bytes < 16) ? _va.v8[_i + _bytes] : 0; _r; })\n',
    '#define shl128(a, i) ({ edge264_vec _va; _va.v8 = (a); i8x16 _r; int _bytes = (i); for(int _i=0; _i<16; _i++) _r[_i] = (_i >= _bytes) ? _va.v8[_i - _bytes] : 0; _r; })\n',
    '#define shrd128(l, h, i) ({ edge264_vec _vl, _vh; _vl.v8 = (l); _vh.v8 = (h); i8x16 _r; int _bytes = (i); for(int _i=0; _i<16; _i++) { \\\n',
    '  int _off = _i + _bytes; \\\n',
    '  _r[_i] = (_off < 16) ? _vl.v8[_off] : _vh.v8[_off - 16]; \\\n',
    '} _r; })\n',
    '#define shlv128(a, i) ({ edge264_vec _va; _va.v8 = (a); i8x16 _r; int _bytes = (i); for(int _i=0; _i<16; _i++) _r[_i] = (_i >= _bytes) ? _va.v8[_i - _bytes] : 0; _r; })\n',
    '#define shrv128(a, i) ({ edge264_vec _va; _va.v8 = (a); i8x16 _r; int _bytes = (i); for(int _i=0; _i<16; _i++) _r[_i] = (_i + _bytes < 16) ? _va.v8[_i + _bytes] : 0; _r; })\n',
    '#define shrz128(a, i) shr128(a, i)\n',
    '\n',
    '#define temporal_scale(v, scale) ({ edge264_vec _va; _va.v16 = (v); int16_t _s = (scale); i16x8 _r; for(int _i=0; _i<8; _i++) _r[_i] = (_va.v16[_i] * _s + 128) >> 8; _r; })\n',
    '\n',
    '#define movemask(a) ({ edge264_vec _va; _va.v8 = (a); int _r = 0; for(int _i=0; _i<16; _i++) if(_va.v8[_i] < 0) _r |= (1 << _i); _r; })\n',
    '\n',
    '#define shufflen(a, m) ({ i8x16 _va = (a); i8x16 _vm = (m); shuffle(_va, _vm) | (i8x16)(_vm < 0); })\n',
    '#define shufflez(a, m) ({ i8x16 _va = (a); i8x16 _vm = (m); shuffle(_va, _vm) & (i8x16)~(_vm < 0); })\n',
    '#define shuffle2(a, b, m) ({ i8x16 _va = (a); i8x16 _vb = (b); i8x16 _vm = (m); ifelse_mask(_vm < 16, shuffle(_va, _vm), shuffle(_vb, _vm - 16)); })\n',
    '#define shuffle2z(a, b, m) (shuffle2(a, b, m) & (i8x16)~((m) < 0))\n',
    '#define shuffle3(p, m) ({ const i8x16 *_vp = (p); i8x16 _vm = (m); ifelse_mask(_vm < 16, shuffle(_vp[0], _vm), ifelse_mask(_vm < 32, shuffle(_vp[1], _vm - 16), shuffle(_vp[2], _vm - 32))); })\n',
    '\n',
    '#define sumh8 sum8\n',
    '#define padd16(a, b) ((a) + (b))\n',
    '#define maddubs(a, b) hadd16(cvtlo8u16(a)*cvtlo8s16(b), cvthi8u16(a)*cvthi8s16(b))\n',
    '#define maddxbs(a, b) hadd16(cvtlo8s16(a)*cvtlo8s16(b), cvthi8s16(a)*cvthi8s16(b))\n',
    '#define shld(hi, lo, sh) (((hi) << (sh)) | ((lo) >> (SIZE_BIT - (sh))))\n',
    '#define sstride2 (sstride * 2)\n',
    '#define sstride4 (sstride * 4)\n',
    '#define nstride2 (nstride * 2)\n',
    '#define dstride2 (dstride * 2)\n',
    '#define dstride4 (dstride * 4)\n',
    '#define premul_strides(sstride, nstride, dstride) ssize_t sstride2 = sstride * 2, sstride4 = sstride * 4, nstride2 = nstride * 2, dstride2 = dstride * 2, dstride4 = dstride * 4\n'
]

# 4. Part B
part_b = lines[1034:]

with open(port_path, 'w') as f:
    f.writelines(header)
    f.writelines(part_a)
    f.writelines(['\n/* SCALAR FALLBACK BLOCK */\n\n'])
    f.writelines(scalar_block)
    f.writelines(part_b)

print("Final surgical reconstruction v8 complete.")
