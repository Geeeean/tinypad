#include "gui.h"
#include "command.h"
#include "components/app.h"
#include "components/loading.h"
#include "components/style.h"
#include "raylib.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <zmq.h>

const int FPS = 30;

void gui_init()
{
    ZmqConnection connection = command_connection_init_gui();
    if (!connection.is_active) {
        return;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "velvet");
    SetTargetFPS(FPS);

    style_init();

    DrawLoadingScreen();
    DrawAppScreen(&connection);

    // Cleanup
    style_cleanup();
    CloseWindow();
}
