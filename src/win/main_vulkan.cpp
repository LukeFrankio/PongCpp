/**
 * @file main_vulkan.cpp  
 * @brief Entry point for Vulkan-based Windows GUI version of PongCpp
 * 
 * This file contains the WinMain entry point and DPI awareness setup
 * for the Vulkan version of Pong using Win32 APIs for windowing
 * and Vulkan API for hardware-accelerated rendering.
 */

#include <windows.h>
#include "game_vulkan.h"
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <cstdio>

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
    std::cout << "[DEBUG] Enabling DPI awareness..." << std::endl;
    // Windows 10+ exposes SetProcessDpiAwarenessContext in user32 (preferred)
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    std::cout << "[DEBUG] LoadLibraryW(user32.dll) result: " << (user32 ? "SUCCESS" : "FAILED") << std::endl;
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
 * Enables DPI awareness and launches the main Vulkan-based game window.
 * 
 * @param hInstance Handle to current application instance
 * @param hPrevInstance Handle to previous instance (always NULL)
 * @param lpCmdLine Command line arguments as string
 * @param nCmdShow Window display state (maximized, minimized, etc.)
 * @return Application exit code
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Try to attach to parent console (terminal) first
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        // If no parent console, allocate a new one
        AllocConsole();
        SetConsoleTitleA("Vulkan Pong Debug Console");
    }
    
    // Redirect standard streams to console
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
    freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
    
    std::cout << "[DEBUG] ========== VULKAN PONG STARTUP ==========" << std::endl;
    std::cout << "[DEBUG] WinMain called with hInstance=" << hInstance << ", nCmdShow=" << nCmdShow << std::endl;
    
    try {
        std::cout << "[DEBUG] Calling enable_dpi_awareness()..." << std::endl;
        enable_dpi_awareness();
        std::cout << "[DEBUG] DPI awareness setup complete." << std::endl;
        
        std::cout << "[DEBUG] About to call run_vulkan_pong()..." << std::endl;
        std::cout.flush(); // Force output before potential crash
        
        int result = run_vulkan_pong(hInstance, nCmdShow);
        
        std::cout << "[DEBUG] run_vulkan_pong() returned successfully: " << result << std::endl;
        
        // Keep console open for debugging
        std::cout << "[DEBUG] Press Enter to close console..." << std::endl;
        std::cin.get();
        
        // Cleanup console
        FreeConsole();
        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in WinMain: " << e.what() << std::endl;
        std::cerr << "[ERROR] Press Enter to close console..." << std::endl;
        std::cin.get();
        FreeConsole();
        return -1;
    }
    catch (...) {
        std::cerr << "[ERROR] Unknown exception in WinMain" << std::endl;
        std::cerr << "[ERROR] Press Enter to close console..." << std::endl;
        std::cin.get();
        FreeConsole();
        return -1;
    }
}