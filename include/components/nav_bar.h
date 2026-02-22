#ifndef NAV_BAR_H
#define NAV_BAR_H

typedef enum { CONFIG, MIXER } Screen;

void DrawNavBar(int *y, Screen *screen);

#endif
