// Temporary diagnostic: webview_create()'s plain-C API swallows the
// underlying exception's message on failure, returning only NULL. This
// calls the C++ layer directly so the real reason (not just "it failed")
// is visible. Not part of the app -- safe to delete once the Windows
// webview_create() investigation is done.
#include "webview/webview.h"

#include <cstdio>

int main()
{
    try {
        webview::webview w(true, nullptr);
        std::printf("webview created OK\n");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "webview_diag: exception: %s\n", e.what());
        return 1;
    }
    return 0;
}
