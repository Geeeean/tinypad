#include "gui.h"
#include "raylib.h"
#include "state.h"

const int WINDOW_PADDING = 20;

void gui_init()
{
    const int screenWidth = 1000;
    const int screenHeight = 650;

    InitWindow(screenWidth, screenHeight, "velvet");

    SetTargetFPS(20);

    int i = 0;
    const int item_height = 40;
    const int padding = WINDOW_PADDING;
    int start_y = 20;

    while (!WindowShouldClose()) {
        int drawn_count = 0;
        BeginDrawing();

        ClearBackground(RAYWHITE);

        pthread_mutex_lock(&shared_state.lock);
        for (int i = 0; i < MAX_NODES; i++) {
            if (shared_state.nodes[i].active) {
                int current_y = start_y + (drawn_count * (item_height + padding));

                DrawRectangle(WINDOW_PADDING, current_y, 400, item_height, LIGHTGRAY);

                DrawText(shared_state.nodes[i].name, 30, current_y + 10, 20, DARKGRAY);

                float vol = atomic_load(&shared_state.nodes[i].volume);
                DrawRectangle(300, current_y + 15, 80, 10, GRAY); // Sfondo barra
                DrawRectangle(300, current_y + 15, (int)(vol * 100.0), 10, BLUE);

                drawn_count++;
            }
        }
        pthread_mutex_unlock(&shared_state.lock);

        EndDrawing();
    }

    CloseWindow();
}
