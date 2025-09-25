/**
 * @file platform_win.cpp
 * @brief Windows-specific implementation of the Platform interface
 * 
 * This file implements console I/O operations using Windows Console API
 * and Microsoft Visual C++ specific functions like _kbhit() and _getch().
 */

#include "platform.h"
#ifdef _WIN32

#include <windows.h>
#include <conio.h>
#include <iostream>

/**
 * @brief Windows-specific Platform implementation
 * 
 * Implements the Platform interface using Windows Console API functions.
 * Provides console clearing, cursor control, and keyboard input detection
 * using native Win32 functions and Microsoft C runtime extensions.
 */
class WinPlatform : public Platform {
public:
    /**
     * @brief Construct Windows platform with ANSI support enabled
     */
    WinPlatform() { enable_ansi(); }
    
    /**
     * @brief Destructor that restores cursor visibility
     */
    ~WinPlatform() override { set_cursor_visible(true); }
    
    /**
     * @brief Check for keyboard input using _kbhit()
     * @return true if key is waiting, false otherwise
     */
    bool kbhit() override { return _kbhit(); }
    
    /**
     * @brief Get character using _getch()
     * @return ASCII code of pressed key
     */
    int getch() override { return _getch(); }
    
    /**
     * @brief Clear screen using ANSI escape sequences
     */
    void clear_screen() override {
        // Use ANSI escape sequences for clearing screen
        std::cout << "\x1b[2J\x1b[H";
    }
    
    /**
     * @brief Set cursor visibility using Windows Console API
     * @param visible true to show cursor, false to hide
     */
    void set_cursor_visible(bool visible) override {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h == INVALID_HANDLE_VALUE) return;
        CONSOLE_CURSOR_INFO info;
        if (!GetConsoleCursorInfo(h, &info)) return;
        info.bVisible = visible ? TRUE : FALSE;
        SetConsoleCursorInfo(h, &info);
    }
    
    /**
     * @brief Enable ANSI escape sequence support in Windows console
     * 
     * Enables ENABLE_VIRTUAL_TERMINAL_PROCESSING mode which allows
     * ANSI escape sequences to work in Windows 10+ consoles.
     */
    void enable_ansi() override {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode)) return;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(h, mode);
    }
};

/**
 * @brief Factory function for Windows platform
 * @return Unique pointer to Windows Platform implementation
 */
std::unique_ptr<Platform> createPlatform() {
    return std::make_unique<WinPlatform>();
}

#endif
