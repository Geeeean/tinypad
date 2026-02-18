#include "gui.h"
#include "raylib.h"
#include "state.h"

const int WINDOW_PADDING = 20;
const int WINDOW_WIDTH = 1000;
const int WINDOW_HEIGHT = 650;

const int ITEM_GAP = 10;
const int ITEM_PADDING = 15;

const int FPS = 10;

#define BACKGROUND CLITERAL(Color){30, 27, 29, 255}
#define ITEM_BACKGROUND CLITERAL(Color){33, 31, 58, 255}

void gui_init()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "velvet");

    Font jet_brains_mono = LoadFontEx("assets/fonts/JetBrainsMono.ttf", 20, 0, 250);
    SetTextureFilter(jet_brains_mono.texture, TEXTURE_FILTER_BILINEAR);

    SetTargetFPS(FPS);

    const int item_height = 40;
    int start_y = 20;

    while (!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(BACKGROUND);

        int drawn_count = 0;
        int current_width = GetScreenWidth();

        pthread_mutex_lock(&shared_state.lock);
        for (int i = 0; i < MAX_NODES; i++) {
            if (shared_state.nodes[i].active) {
                int current_y = start_y + (drawn_count * (item_height + ITEM_GAP));

                Rectangle item = {WINDOW_PADDING, current_y,
                                  current_width - WINDOW_PADDING * 2, item_height};
                DrawRectangleRounded(item, 0.5f, 10, ITEM_BACKGROUND);

                Vector2 position = {(float)(WINDOW_PADDING + ITEM_PADDING),
                                    (float)(current_y + 10)};
                DrawTextEx(jet_brains_mono, shared_state.nodes[i].name, position, 20,
                           1.5f, WHITE);

                float vol = atomic_load(&shared_state.nodes[i].volume);
                DrawRectangle(current_width - WINDOW_PADDING - ITEM_PADDING - 100,
                              current_y + 15, 100, 10, BACKGROUND);
                DrawRectangle(current_width - WINDOW_PADDING - ITEM_PADDING - 100,
                              current_y + 15, (int)(vol * 100.0), 10, BLUE);

                drawn_count++;
            }
        }
        pthread_mutex_unlock(&shared_state.lock);

        EndDrawing();
    }

    CloseWindow();
}
