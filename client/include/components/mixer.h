#ifndef MIXER_H
#define MIXER_H

#include "command.h"
#include "raylib.h"

void DrawMixerScreen(Texture2D volume_icon, int start_y, ZmqConnection *connection);

#endif
