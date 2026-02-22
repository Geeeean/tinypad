#include "components/loading.h"
#include "components/style.h"
#include "state.h"

void DrawLoadingScreen()
{
    double loadingStartTime = GetTime();

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BACKGROUND);

        int centerX = GetScreenWidth() / 2;
        int centerY = GetScreenHeight() / 2;

        float size = 35.0f;
        float spacing = 2.0f;

        const char *logo = "VELVET";
        Vector2 logo_size = MeasureTextEx(font, logo, size, spacing);

        DrawTextEx(font_logo, logo,
                   (Vector2){centerX - logo_size.x / 2, centerY - logo_size.y / 2}, size,
                   spacing, ITEM_FOREGROUND);

        EndDrawing();

        bool is_ready = atomic_load(&shared_state.is_ready);
        if (is_ready && (GetTime() - loadingStartTime >= 0.5)) {
            break;
        }
    }
}
