#ifndef MSG_H
#define MSG_H

#include "../include/types.h"

// Message Types
#define MSG_GFX_CREATE_WINDOW 1
#define MSG_GFX_WINDOW_CREATED 2
#define MSG_GFX_INVALIDATE_RECT 3
#define MSG_GFX_MOUSE_EVENT 4
#define MSG_GFX_KEY_EVENT 5
#define MSG_GFX_CLOSE_WINDOW 6

// Request: Create Window
typedef struct {
  int width;
  int height;
  char title[64];
} msg_gfx_create_window_t;

// Response: Window Created
typedef struct {
  int window_id;
  int shm_id; // Shared memory ID for the framebuffer
} msg_gfx_window_created_t;

// Command: Invalidate Rect (App -> WindowServer)
typedef struct {
  int window_id;
  int x, y, w, h;
} msg_gfx_invalidate_rect_t;

// Event: Mouse (WindowServer -> App)
typedef struct {
  int window_id;
  int x, y;
  int buttons;
} msg_gfx_mouse_event_t;

// Event: Key (WindowServer -> App)
typedef struct {
  int window_id;
  char key;
} msg_gfx_key_event_t;

// Generic Message Envelope
typedef struct {
  uint32_t type;
  uint32_t size;
  union {
    msg_gfx_create_window_t create;
    msg_gfx_window_created_t created;
    msg_gfx_invalidate_rect_t invalidate;
    msg_gfx_mouse_event_t mouse;
    msg_gfx_key_event_t key;
  } data;
} gfx_msg_t;

#endif // MSG_H
