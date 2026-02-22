#ifndef STYLE_H
#define STYLE_H

#include "raylib.h"

// Colors
#define BACKGROUND CLITERAL(Color){15, 15, 17, 255}
#define ITEM_BACKGROUND CLITERAL(Color){29, 30, 34, 255}
#define ITEM_FOREGROUND CLITERAL(Color){245, 245, 245, 255}
#define MUTE CLITERAL(Color){53, 57, 60, 255}

#define WINDOW_WIDTH 1222
#define WINDOW_HEIGHT 650
#define WINDOW_PADDING 20
#define ITEM_GAP 10
#define ITEM_PADDING_X 15
#define ITEM_PADDING_Y 25
#define ITEM_HEIGHT 75

extern Font font;
extern Font font_logo;

void style_init();
void style_cleanup();

#endif
