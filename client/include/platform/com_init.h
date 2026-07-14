#pragma once

// Windows-only: establishes the process's COM apartment model (STA) once,
// up front in main(), before either WASAPI (audio_windows_wasapi.c) or
// webview's own internal init gets a chance to pick one for itself.
// Necessary because only one of the two tolerates finding a different
// model already active on the thread -- WASAPI's own CoInitializeEx call
// treats RPC_E_CHANGED_MODE as fine and reuses whatever is there, but
// webview's throws. Whichever runs first otherwise wins the race and
// silently breaks the other, depending on init order. No-op on
// macOS/Linux (no COM there).
void platform_com_init(void);
void platform_com_uninit(void);
