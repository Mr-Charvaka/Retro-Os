#include "buddy.h"
#include "../drivers/serial.h"
#include "../include/string.h"

static int get_order(size_t size) {
  int order = 0;
  size_t s = 1;
  while (s < size) {
    s <<= 1;
    order++;
  }
  return order;
}

// Find largest power of 2 less than or equal to n
static int get_order_floor(size_t n) {
  int order = 0;
  size_t s = 1;
  while (s * 2 <= n) {
    s <<= 1;
    order++;
  }
  return order;
}

void buddy_init(buddy_t *b, uintptr_t start, uintptr_t end) {
  b->start_addr = start;
  b->end_addr = end;
  for (int i = 0; i < BUDDY_NUM_ORDERS; i++) {
    b->free_lists[i] = nullptr;
  }

  // Initialize with varying sized blocks to cover the entire range
  uintptr_t current = start;
  while (current < end) {
    size_t available = end - current;
    int order = get_order_floor(available);

    if (order > BUDDY_MAX_ORDER)
      order = BUDDY_MAX_ORDER;
    if (order < BUDDY_MIN_ORDER)
      break;

    size_t block_size = (size_t)1 << order;
    int list_idx = order - BUDDY_MIN_ORDER;
    if (list_idx >= 0 && list_idx < BUDDY_NUM_ORDERS) {
      buddy_block_t *block = (buddy_block_t *)current;
      block->next = b->free_lists[list_idx];
      b->free_lists[list_idx] = block;
    }

    current += block_size;
  }

  serial_log("BUDDY: Initialized pool over full range.");
}

void *buddy_alloc(buddy_t *b, size_t size) {
  int order = get_order(size);
  if (order < BUDDY_MIN_ORDER)
    order = BUDDY_MIN_ORDER;

  int list_idx = order - BUDDY_MIN_ORDER;
  if (list_idx >= BUDDY_NUM_ORDERS)
    return nullptr;

  for (int i = list_idx; i < BUDDY_NUM_ORDERS; i++) {
    if (b->free_lists[i]) {
      buddy_block_t *block = b->free_lists[i];
      b->free_lists[i] = block->next;

      while (i > list_idx) {
        i--;
        uintptr_t addr = (uintptr_t)block;
        buddy_block_t *buddy =
            (buddy_block_t *)(addr + ((uintptr_t)1 << (i + BUDDY_MIN_ORDER)));
        buddy->next = b->free_lists[i];
        b->free_lists[i] = buddy;
      }

      memset(block, 0, (size_t)1 << order);
      return (void *)block;
    }
  }

  return nullptr;
}

void buddy_free(buddy_t *b, void *ptr, size_t size) {
  if (!ptr)
    return;

  int current_order =
      get_order(size); // This is the order of the block being freed
  if (current_order < BUDDY_MIN_ORDER)
    current_order = BUDDY_MIN_ORDER;

  uintptr_t addr = (uintptr_t)ptr;

  // Loop to coalesce with buddies of increasing order
  // The loop variable 'current_order' here represents the *order* of the block
  // we are currently trying to free/coalesce It starts at the initial block's
  // order and goes up as blocks are coalesced.
  for (; current_order < BUDDY_MAX_ORDER; current_order++) {
    int list_idx = current_order - BUDDY_MIN_ORDER;

    if (list_idx >= BUDDY_NUM_ORDERS) {
      // This should ideally not be reached if BUDDY_MAX_ORDER is correctly
      // defined relative to BUDDY_NUM_ORDERS.
      break;
    }

    size_t block_size = (size_t)1 << current_order;
    uintptr_t relative_addr = addr - b->start_addr;
    uintptr_t buddy_addr = (relative_addr ^ block_size) + b->start_addr;

    // Search for the buddy in the free list for the current_order
    buddy_block_t **prev = &b->free_lists[list_idx];
    buddy_block_t *curr = b->free_lists[list_idx];
    bool found_buddy = false;

    while (curr) {
      if ((uintptr_t)curr == buddy_addr) {
        // Remove buddy from free list
        *prev = curr->next;
        found_buddy = true;
        break;
      }
      prev = &curr->next;
      curr = curr->next;
    }

    if (!found_buddy) {
      // Buddy not free, or not found in the current order's list.
      // Stop coalescing and add the current block (addr) to this order's list.
      break;
    }

    // Coalesce: new block starts at the lower of the two addresses
    if (buddy_addr < addr) {
      addr = buddy_addr;
    }
    // Continue to next order with the newly coalesced block (addr)
  }

  // After the loop, 'addr' is the base address of the largest coalesced block,
  // and 'current_order' is its order.
  int final_list_idx = current_order - BUDDY_MIN_ORDER;
  if (final_list_idx < 0 || final_list_idx >= BUDDY_NUM_ORDERS) {
    // This indicates an error in order calculation or bounds.
    // For safety, we might log an error or panic here in a real system.
    return;
  }

  buddy_block_t *block = (buddy_block_t *)addr;
  block->next = b->free_lists[final_list_idx];
  b->free_lists[final_list_idx] = block;
}
