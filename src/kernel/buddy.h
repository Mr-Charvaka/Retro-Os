#ifndef BUDDY_H
#define BUDDY_H

#include "../include/types.h"

#define BUDDY_MIN_ORDER 12 // 4KB
#define BUDDY_MAX_ORDER 28 // 256MB
#define BUDDY_NUM_ORDERS (BUDDY_MAX_ORDER - BUDDY_MIN_ORDER + 1)

typedef struct buddy_block {
  struct buddy_block *next;
} buddy_block_t;

typedef struct {
  uintptr_t start_addr;
  uintptr_t end_addr;
  buddy_block_t *free_lists[BUDDY_NUM_ORDERS];
} buddy_t;

#ifdef __cplusplus
extern "C" {
#endif

void buddy_init(buddy_t *b, uintptr_t start, uintptr_t end);
void *buddy_alloc(buddy_t *b, size_t size);
void buddy_free(buddy_t *b, void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif
