#include "components/config.h"
#include "components/mixer.h"
#include "components/nav_bar.h"
#include "components/style.h"

void DrawAppScreen(ZmqConnection *connection)
{
    Image icon_img = LoadImage("assets/icons/volume-2.png");
    ImageColorInvert(&icon_img); // Convert black to white for tinting
    Texture2D volume_icon = LoadTextureFromImage(icon_img);
    GenTextureMipmaps(&volume_icon);
    SetTextureFilter(volume_icon, TEXTURE_FILTER_BILINEAR);
    UnloadImage(icon_img);

    Screen screen = CONFIG;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BACKGROUND);

        int start_y = 20;
        DrawNavBar(&start_y, &screen);

        switch (screen) {
        case CONFIG:
            DrawConfigScreen(start_y, connection);
            break;
        case MIXER:
            DrawMixerScreen(volume_icon, start_y, connection);
            break;
        default:
            // todo handle unknown screen
            break;
        }

        EndDrawing();
    }

    UnloadTexture(volume_icon);
}
