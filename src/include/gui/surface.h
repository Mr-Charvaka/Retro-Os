/*
 * gui/surface.h - GUI Shared Surface Buffer Structures
 */

#ifndef GUI_SURFACE_H
#define GUI_SURFACE_H

#include <stdint.h>

#define SURFACE_MAGIC 0x53555246 // 'SURF'

// Surface Buffer Header (must match layout for shared memory)
typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t stride;   // Pixels per row (may differ from width for alignment)
  uint32_t format;   // GUI_FMT_*
  uint32_t pixels[]; // Flexible array member
} gui_surface_buffer_t;

// Surface Control Block
typedef struct {
  uint32_t magic;              // Must be SURFACE_MAGIC
  volatile uint32_t committed; // Commit counter for sync
  gui_surface_buffer_t *back;  // Pointer to backing buffer
} gui_surface_ctrl_t;

#endif // GUI_SURFACE_H
