#include <windows.h>
#include "game_win.h"

// Try to enable per-monitor DPI awareness where available.
static void enable_dpi_awareness() {
    // Windows 10+ exposes SetProcessDpiAwarenessContext in user32 (preferred)
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI *SPDAC)(DPI_AWARENESS_CONTEXT);
        SPDAC spdac = (SPDAC)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (spdac) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
            spdac((DPI_AWARENESS_CONTEXT)-4);
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }
    // fallback to older API
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI *SPD)(int);
        SPD spd = (SPD)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (spd) { spd(2); /* PROCESS_PER_MONITOR_DPI_AWARE */ }
        FreeLibrary(shcore);
        return;
    }
    // final fallback
    SetProcessDPIAware();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    enable_dpi_awareness();
    return run_win_pong(hInstance, nCmdShow);
}
