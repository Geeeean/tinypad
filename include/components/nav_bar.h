#ifndef NAV_BAR_H
#define NAV_BAR_H

typedef enum { CONFIG, MIXER } Window;

void DrawNavBar(int screen_w, int *y, Window *window);

#endif
