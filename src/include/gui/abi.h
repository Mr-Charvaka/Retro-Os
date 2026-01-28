/*
 * gui/abi.h - GUI Application Binary Interface Version
 */

#ifndef GUI_ABI_H
#define GUI_ABI_H

#include <stdint.h>

// ABI Version: Major.Minor (0x0001.0000 = v1.0)
#define GUI_ABI_VERSION 0x00010000

// Pixel Formats
#define GUI_FMT_ARGB32 1
#define GUI_FMT_RGB24 2

#endif // GUI_ABI_H
