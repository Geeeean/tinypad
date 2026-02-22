#include "components/style.h"
#include "components/volume_item.h"
#include "state.h"

void DrawMixerScreen(Texture2D volume_icon, int start_y, ZmqConnection *connection)
{
    int drawn_count = 0;
    int screen_w = GetScreenWidth();

    pthread_mutex_lock(&shared_state.lock);
    for (int i = 0; i < MAX_NODES; i++) {
        if (!shared_state.nodes[i].active) {
            continue;
        }

        float current_y = (float)start_y + (drawn_count * (ITEM_HEIGHT + ITEM_GAP));
        float line1 = current_y + ITEM_PADDING_Y;
        float line2 = current_y + ITEM_HEIGHT - ITEM_PADDING_Y;

        float row_width = (float)screen_w - (WINDOW_PADDING * 2);
        DrawRectangleRounded(
            (Rectangle){(float)WINDOW_PADDING, current_y, row_width, (float)ITEM_HEIGHT},
            0.4f, 8, ITEM_BACKGROUND);

        float icon_pos_x = WINDOW_PADDING + ITEM_PADDING_X;
        DrawTexture(volume_icon, icon_pos_x, line1 - (float)volume_icon.height / 2, MUTE);

        // 3. Text (Aligned after icon)
        int font_size = 20;
        Vector2 text_pos = {icon_pos_x + volume_icon.width + 12,
                            line1 - (float)font_size / 2};
        DrawTextEx(font, shared_state.nodes[i].name, text_pos, font_size, 1.5f,
                   ITEM_FOREGROUND);

        float volume = atomic_load(&shared_state.nodes[i].volume);
        Rectangle volume_props = {
            .height = 15, .width = 200, .x = icon_pos_x, .y = line2 - 10.0 / 2};

        DrawVolumeItem(volume_props, volume, shared_state.nodes[i].id, connection);

        drawn_count++;
    }
    pthread_mutex_unlock(&shared_state.lock);
}
