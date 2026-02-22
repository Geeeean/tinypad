#include "gui.h"
#include "command.h"
#include "components/nav_bar.h"
#include "components/style.h"
#include "components/volume_item.h"
#include "raylib.h"
#include "state.h"
#include <pthread.h>
#include <unistd.h>
#include <zmq.h>

// UI Constants
const int WINDOW_WIDTH = 1222;
const int WINDOW_HEIGHT = 650;
const int WINDOW_PADDING = 20;
const int ITEM_GAP = 10;
const int ITEM_PADDING_X = 15;
const int ITEM_PADDING_Y = 25;
const int ITEM_HEIGHT = 75;
const int FPS = 30;

Font font = {0};

void gui_init()
{
    ZmqConnection zmq = command_connection_init_gui();
    if (!zmq.is_active) {
        return;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "velvet");
    SetTargetFPS(FPS);

    font = LoadFontEx("assets/fonts/JetBrainsMono.ttf", 20, 0, 250);

    // Assets loading
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Image icon_img = LoadImage("assets/icons/volume-2.png");
    ImageColorInvert(&icon_img); // Convert black to white for tinting
    Texture2D volume_icon = LoadTextureFromImage(icon_img);
    GenTextureMipmaps(&volume_icon);
    SetTextureFilter(volume_icon, TEXTURE_FILTER_BILINEAR);
    UnloadImage(icon_img);

    Window window = CONFIG;

    while (!WindowShouldClose()) {
        int screen_w = GetScreenWidth();

        BeginDrawing();
        ClearBackground(BACKGROUND);

        int start_y = 20;
        DrawNavBar(screen_w, &start_y, &window);

        DrawText(TextFormat("%i FPS", GetFPS()), 10, 10, 20, LIME);

        if (window == MIXER) {
            int drawn_count = 0;

            pthread_mutex_lock(&shared_state.lock);
            for (int i = 0; i < MAX_NODES; i++) {
                if (!shared_state.nodes[i].active)
                    continue;

                float current_y =
                    (float)start_y + (drawn_count * (ITEM_HEIGHT + ITEM_GAP));
                float line1 = current_y + ITEM_PADDING_Y;
                float line2 = current_y + ITEM_HEIGHT - ITEM_PADDING_Y;

                float row_width = (float)screen_w - (WINDOW_PADDING * 2);
                DrawRectangleRounded((Rectangle){(float)WINDOW_PADDING, current_y,
                                                 row_width, (float)ITEM_HEIGHT},
                                     0.4f, 8, ITEM_BACKGROUND);

                float icon_pos_x = WINDOW_PADDING + ITEM_PADDING_X;
                DrawTexture(volume_icon, icon_pos_x,
                            line1 - (float)volume_icon.height / 2, MUTE);

                // 3. Text (Aligned after icon)
                int font_size = 20;
                Vector2 text_pos = {icon_pos_x + volume_icon.width + 12,
                                    line1 - (float)font_size / 2};
                DrawTextEx(font, shared_state.nodes[i].name, text_pos, font_size, 1.5f,
                           WHITE);

                float volume = atomic_load(&shared_state.nodes[i].volume);
                Rectangle volume_props = {
                    .height = 15, .width = 200, .x = icon_pos_x, .y = line2 - 10.0 / 2};
                DrawVolumeItem(volume_props, volume, shared_state.nodes[i].id, &zmq);

                drawn_count++;
            }
            pthread_mutex_unlock(&shared_state.lock);
        } else if (window == CONFIG) {

        } else {
        }

        EndDrawing();
    }

    // Cleanup
    UnloadFont(font);
    UnloadTexture(volume_icon);
    CloseWindow();
}
