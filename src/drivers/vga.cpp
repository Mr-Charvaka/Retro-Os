#include "vga.h"
#include "../include/string.h"

uint16_t *video_memory = (uint16_t *)VGA_MEMORY;
int cursor_x = 0;
int cursor_y = 0;
uint8_t default_color = WHITE | (BLACK << 4);

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
  // TODO: Implement using outb to 0x3D4/0x3D5
}

void vga_update_cursor(int x, int y) {
  // TODO: Implement using outb to 0x3D4/0x3D5
}

uint16_t vga_entry(unsigned char uc, uint8_t color) {
  return (uint16_t)uc | (uint16_t)color << 8;
}

void vga_clear_screen() {
  for (int y = 0; y < VGA_HEIGHT; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      const int index = y * VGA_WIDTH + x;
      video_memory[index] = vga_entry(' ', default_color);
    }
  }
  cursor_x = 0;
  cursor_y = 0;
}

void vga_scroll() {
  // Determine if we need to scroll (if cursor_y >= VGA_HEIGHT)
  if (cursor_y >= VGA_HEIGHT) {
    int i;
    // Move lines up
    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
      video_memory[i] = video_memory[i + VGA_WIDTH];
    }
    // Clear last line
    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
      video_memory[i] = vga_entry(' ', default_color);
    }
    cursor_y = VGA_HEIGHT - 1;
  }
}

void vga_print_char(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
  } else {
    const int index = cursor_y * VGA_WIDTH + cursor_x;
    video_memory[index] = vga_entry(c, default_color);
    cursor_x++;
  }

  if (cursor_x >= VGA_WIDTH) {
    cursor_x = 0;
    cursor_y++;
  }

  vga_scroll();
}

void vga_print(const char *str) {
  for (int i = 0; i < strlen(str); i++) {
    vga_print_char(str[i]);
  }
}

void vga_print_color(const char *str, uint8_t color) {
  uint8_t old_color = default_color;
  default_color = color;
  vga_print(str);
  default_color = old_color;
}
