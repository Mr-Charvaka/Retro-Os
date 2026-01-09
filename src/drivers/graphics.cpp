/*
 * graphics.cpp - Core Double-Buffered Graphics Driver
 * Hardware-backed 1024x768x32 renderer
 */

#include "graphics.h"
#include "../include/string.h"
#include "serial.h"

// Screen dimensions
#define SCREEN_W 1024
#define SCREEN_H 768

// Buffers
uint32_t *screen_buffer = nullptr; // Hardware LFB
uint32_t *back_buffer = nullptr;   // Off-screen render target

// Forward declare heap function
extern "C" void *malloc(uint32_t size);

extern "C" void init_graphics(uint32_t lfb_address) {
  screen_buffer = (uint32_t *)(uintptr_t)lfb_address;

  // Allocate backbuffer (3MB for 1024x768x32)
  back_buffer = (uint32_t *)malloc(SCREEN_W * SCREEN_H * sizeof(uint32_t));

  if (!back_buffer) {
    serial_log("GRAPHICS: FATAL - Backbuffer allocation failed!");
    return;
  }

  // Clear to black
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    back_buffer[i] = 0xFF000000;
  }

  serial_log_hex("GRAPHICS: Backbuffer Addr: ",
                 (uint32_t)(uintptr_t)back_buffer);
}

extern "C" void swap_buffers() {
  if (!screen_buffer || !back_buffer)
    return;

  // Fast memcpy from back to front
  uint32_t *src = back_buffer;
  uint32_t *dst = screen_buffer;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    dst[i] = src[i];
  }
}

extern "C" void gfx_clear_screen(uint32_t color) {
  if (!back_buffer)
    return;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    back_buffer[i] = color;
  }
}

extern "C" void put_pixel(int x, int y, uint32_t color) {
  if (!back_buffer)
    return;
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
    return;
  back_buffer[y * SCREEN_W + x] = color;
}

extern "C" void draw_rect(int x, int y, int w, int h, uint32_t color) {
  for (int py = 0; py < h; py++) {
    for (int px = 0; px < w; px++) {
      put_pixel(x + px, y + py, color);
    }
  }
}

extern "C" void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
  int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
  int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int err = dx - dy;

  while (true) {
    put_pixel(x1, y1, color);
    if (x1 == x2 && y1 == y2)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}
