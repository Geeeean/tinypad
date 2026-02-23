#ifndef VOLUME_ITEM_H
#define VOLUME_ITEM_H

#include "command.h"
#include "raylib.h"

typedef struct {
    Font font;
    Texture2D icon;
    Color bg_color;
    Color accent_color;
} ComponentStyle;

void DrawVolumeItem(Rectangle props, float volume, uint32_t node_id,
                    ZmqConnection *connection);

#endif
