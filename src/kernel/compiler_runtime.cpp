#include <stdint.h>

extern "C" {

uint64_t __udivdi3(uint64_t n, uint64_t d) {
  if (d == 0)
    return 0;
  uint64_t q = 0, r = 0;
  for (int i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) {
      r -= d;
      q |= (1ULL << i);
    }
  }
  return q;
}

uint64_t __umoddi3(uint64_t n, uint64_t d) {
  if (d == 0)
    return 0;
  uint64_t r = 0;
  for (int i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) {
      r -= d;
    }
  }
  return r;
}

// Add signed versions just in case
int64_t __divdi3(int64_t n, int64_t d) {
  int64_t s_n = n >> 63;
  int64_t s_d = d >> 63;
  uint64_t un = (n ^ s_n) - s_n;
  uint64_t ud = (d ^ s_d) - s_d;
  uint64_t uq = __udivdi3(un, ud);
  int64_t s_q = s_n ^ s_d;
  return (uq ^ s_q) - s_q;
}
}
