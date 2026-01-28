#ifndef BROWSER_H
#define BROWSER_H

#include "gui_common.h" // For Window struct and events

namespace Browser {

void init();
void draw(Window *w);
bool click(Window *w, int mx, int my);
void key(Window *w, int k, int state);
void navigate(const char *url);

} // namespace Browser

#endif
