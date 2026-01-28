/*
 * gui/window.h - GUI Window Descriptor
 */

#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <stdint.h>

#define WINDOW_FLAG_VISIBLE 0x01
#define WINDOW_FLAG_DECORATED 0x02
#define WINDOW_FLAG_RESIZABLE 0x04

// Window Creation Descriptor
typedef struct {
  char title[64];
  int32_t x, y;
  uint32_t w, h;
  uint32_t flags;
} gui_window_desc_t;

#endif // GUI_WINDOW_H
