#ifndef FONT_UTILS_H
#define FONT_UTILS_H

#include <stdint.h>
#include "../include/os/ipc.hpp"

class FontUtils {
public:
    static void init();
    static int get_text_width(const char* text, int size);
    static void draw_text(OS::IPCClient* ipc, int x, int y, const char* text, uint32_t color, int size);

private:
    static uint8_t font8x8[128][8];
    static bool initialized;
};

#endif
