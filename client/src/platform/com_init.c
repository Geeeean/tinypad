#include "platform/com_init.h"

#ifdef _WIN32
#include <windows.h>

void platform_com_init(void) { CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); }
void platform_com_uninit(void) { CoUninitialize(); }

#else

void platform_com_init(void) {}
void platform_com_uninit(void) {}

#endif
