/**
 * @file platform_posix.cpp
 * @brief POSIX/Linux-specific implementation of the Platform interface
 * 
 * This file implements console I/O operations using POSIX termios functions
 * and system calls for keyboard input detection and terminal control.
 */

#include "platform.h"
#ifndef _WIN32

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>

/**
 * @brief POSIX-specific Platform implementation
 * 
 * Implements the Platform interface using POSIX termios functions and
 * system calls. Configures the terminal for raw input mode to enable
 * immediate keyboard response without requiring Enter key.
 */
class PosixPlatform : public Platform {
public:
    /**
     * @brief Construct POSIX platform and configure terminal
     * 
     * Sets up raw terminal mode by disabling canonical input and echo.
     * Stores original terminal settings for restoration on destruction.
     */
    PosixPlatform() { 
        enable_ansi(); 
        orig = {}; 
        tcgetattr(STDIN_FILENO, &orig); 
        term = orig; 
        term.c_lflag &= ~(ICANON | ECHO); 
        tcsetattr(STDIN_FILENO, TCSANOW, &term); 
    }
    
    /**
     * @brief Destructor that restores original terminal settings
     */
    ~PosixPlatform() override { 
        tcsetattr(STDIN_FILENO, TCSANOW, &orig); 
        set_cursor_visible(true); 
    }
    
    /**
     * @brief Check for keyboard input using ioctl FIONREAD
     * @return true if bytes are available to read, false otherwise
     */
    bool kbhit() override {
        int bytes = 0;
        ioctl(STDIN_FILENO, FIONREAD, &bytes);
        return bytes > 0;
    }
    
    /**
     * @brief Read single character from stdin
     * @return ASCII code of character, or -1 on error
     */
    int getch() override {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
        return (int)c;
    }
    
    /**
     * @brief Clear screen using ANSI escape sequences
     */
    void clear_screen() override { 
        std::cout << "\x1b[2J\x1b[H"; 
    }
    
    /**
     * @brief Set cursor visibility using ANSI escape sequences
     * @param visible true to show cursor, false to hide
     */
    void set_cursor_visible(bool visible) override {
        if (visible) std::cout << "\x1b[?25h";
        else std::cout << "\x1b[?25l";
    }
    
    /**
     * @brief Enable ANSI support (no-op on POSIX)
     * 
     * POSIX terminals typically support ANSI escape sequences by default,
     * so no special setup is required.
     */
    void enable_ansi() override {
        // POSIX terminals usually support ANSI by default
    }
    
private:
    struct termios orig;  ///< Original terminal settings for restoration
    struct termios term;  ///< Modified terminal settings for raw mode
};

/**
 * @brief Factory function for POSIX platform
 * @return Unique pointer to POSIX Platform implementation
 */
std::unique_ptr<Platform> createPlatform() {
    return std::make_unique<PosixPlatform>();
}

#endif
