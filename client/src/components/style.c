#include "components/style.h"

Font font = {0};
Font font_logo = {0};

#define FONT_PATH "assets/fonts/JetBrainsMono.ttf"

void style_init()
{
    font = LoadFontEx(FONT_PATH, 20, 0, 250);
    font_logo = LoadFontEx(FONT_PATH, 30, 0, 250);

    // Assets loading
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font_logo.texture, TEXTURE_FILTER_BILINEAR);
}

void style_cleanup()
{
    UnloadFont(font);
    UnloadFont(font_logo);
}
