/**
 * @file main_win.cpp  
 * @brief Entry point for Windows GUI version of PongCpp
 * 
 * This file contains the WinMain entry point and DPI awareness setup
 * for the Windows GUI version of Pong using Win32 APIs.
 */

#include <windows.h>
#include "game_win.h"

/**
 * @brief Enable DPI awareness for high-resolution displays
 * 
 * Attempts to enable per-monitor DPI awareness using the most modern
 * API available on the current Windows version. Falls back through
 * multiple API versions for maximum compatibility:
 * 
 * 1. Windows 10+ SetProcessDpiAwarenessContext (preferred)  
 * 2. Windows 8.1+ SetProcessDpiAwareness
 * 3. Windows Vista+ SetProcessDPIAware (fallback)
 * 
 * This ensures the game window scales properly on high-DPI displays
 * without appearing blurry or incorrectly sized.
 */
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
    
    // Fallback to Windows 8.1+ API
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI *SPD)(int);
        SPD spd = (SPD)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (spd) { spd(2); /* PROCESS_PER_MONITOR_DPI_AWARE */ }
        FreeLibrary(shcore);
        return;
    }
    
    // Final fallback for Windows Vista+
    SetProcessDPIAware();
}

/**
 * @brief Windows application entry point
 * 
 * Standard WinMain entry point for Windows GUI applications.
 * Enables DPI awareness and launches the main game window.
 * 
 * @param hInstance Handle to current application instance
 * @param nCmdShow Window display state (maximized, minimized, etc.)
 * @return Application exit code
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    enable_dpi_awareness();
    return run_win_pong(hInstance, nCmdShow);
}
