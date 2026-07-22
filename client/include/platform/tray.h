#pragma once

// Minimal system tray / menu-bar icon, shown while the GUI window is closed
// so the user can bring it back or quit outright. One implementation
// compiled per OS (flat API, not a vtable -- there is only ever one tray per
// process lifetime):
//   - src/platform/tray/tray_windows.c  (Shell_NotifyIcon)
//   - src/platform/tray/tray_macos.m    (NSStatusItem, Objective-C)
//   - src/platform/tray/tray_linux.c    (GtkStatusIcon)

typedef enum {
    TRAY_RESULT_SHOW,  // user picked "Show" -- caller should recreate the UI window
    TRAY_RESULT_QUIT,  // user picked "Quit" -- caller should shut down entirely
    TRAY_RESULT_ERROR, // tray unavailable (init failure, no systray support, ...)
} tray_result_t;

// Shows a tray/menu-bar icon with "Show" and "Quit" items and blocks the
// calling thread -- which must be the main thread, since native tray/menu
// event loops require it on macOS and most Linux desktops -- running its
// native event loop until one is chosen. The icon is removed before this
// returns; safe to call again for the next backgrounding cycle.
tray_result_t tray_run(void);
