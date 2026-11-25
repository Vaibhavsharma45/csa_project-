/*
 * main.c - window setup and the main loop. All drawing and input handling
 * lives in gui.c; all cache logic lives in the other modules.
 */
#include "raylib.h"
#include "gui.h"

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT, GUI_WINDOW_TITLE);
    if (!IsWindowReady()) {
        return 1;
    }
    SetExitKey(KEY_NULL);        /* ESC must not close the app while typing */
    SetTargetFPS(60);

    Gui gui;
    if (!gui_init(&gui)) {
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose()) {
        gui_frame(&gui);
    }

    gui_shutdown(&gui);
    CloseWindow();
    return 0;
}
