#include "components/volume_item.h"
#include "command.h"
#include "components/style.h"
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>

void DrawVolumeItem(Rectangle props, float volume, uint32_t node_id,
                    ZmqConnection *connection)
{
    Vector2 mouse = GetMousePosition();

    bool is_hover = CheckCollisionPointRec(mouse, props);

    if (is_hover && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        float currentVolume = (mouse.x - props.x) / (float)props.width;
        command_send(*connection, (Command){.magic = VELVET_MAGIC,
                                            .type = CMD_SET_VOLUME,
                                            .value = currentVolume,
                                            .node_id = node_id});
    }

    if (is_hover) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            // Incremento/Decremento del 5%
            float new_volume = volume + (wheel * 0.05f);

            // Clamp del valore tra 0.0 e 1.0
            if (new_volume < 0.0f)
                new_volume = 0.0f;
            if (new_volume > 1.0f)
                new_volume = 1.0f;

            command_send(*connection, (Command){.magic = VELVET_MAGIC,
                                                .type = CMD_SET_VOLUME,
                                                .value = new_volume,
                                                .node_id = node_id});
        }
    }

    // track
    DrawRectangleRounded(props, 0.8f, 20, BACKGROUND);

    Rectangle fill_props = props;
    fill_props.width = (int)(volume * props.width);

    // fill
    DrawRectangleRounded(fill_props, 0.8f, 20, ITEM_FOREGROUND);
}
