// Windows system tray icon via Shell_NotifyIcon, hosted on a hidden
// message-only window. Not build-verified in this repo's dev environment
// (no Windows toolchain available) -- same caveat as serial_windows.c.

#include "platform/tray.h"
#include <shellapi.h>
#include <windows.h>

#define WM_TRAY_CALLBACK (WM_APP + 1)
#define ID_TRAY_SHOW 1001
#define ID_TRAY_QUIT 1002

typedef struct {
    tray_result_t result;
} tray_context_t;

static void show_popup_menu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show");
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    // Required so the menu dismisses correctly if the user clicks away from
    // it instead of picking an item (documented TrackPopupMenu quirk).
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    tray_context_t *ctx = (tray_context_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_TRAY_CALLBACK:
        if (lparam == WM_LBUTTONDBLCLK) {
            ctx->result = TRAY_RESULT_SHOW;
            PostQuitMessage(0);
        } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            show_popup_menu(hwnd);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_TRAY_SHOW) {
            ctx->result = TRAY_RESULT_SHOW;
            PostQuitMessage(0);
        } else if (LOWORD(wparam) == ID_TRAY_QUIT) {
            ctx->result = TRAY_RESULT_QUIT;
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

tray_result_t tray_run(void)
{
    tray_context_t ctx = {.result = TRAY_RESULT_ERROR};

    HINSTANCE instance = GetModuleHandleW(NULL);
    static const wchar_t *CLASS_NAME = L"TinypadTrayWindow";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = tray_wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = CLASS_NAME;
    // Harmlessly fails with ERROR_CLASS_ALREADY_EXISTS on the 2nd+ call
    // (once per backgrounding cycle); ignored since that's expected.
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"TinyPad", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                instance, NULL);
    if (!hwnd) {
        return TRAY_RESULT_ERROR;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&ctx);

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION); // generic icon, no bundled asset needed yet
    wcscpy(nid.szTip, L"TinyPad");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        DestroyWindow(hwnd);
        return TRAY_RESULT_ERROR;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);

    return ctx.result;
}
