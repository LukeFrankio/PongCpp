/**
 * @file platform.h
 * @brief Platform abstraction layer for console I/O operations
 * 
 * This file defines the Platform interface that abstracts console input/output
 * operations across different operating systems (Windows, POSIX/Linux).
 */

#pragma once

#include <memory>

/**
 * @brief Abstract interface for platform-specific console operations
 * 
 * The Platform interface provides a common API for console I/O operations
 * that have different implementations on Windows and POSIX systems.
 * This allows the game logic to remain platform-independent while
 * supporting native console behavior on each platform.
 */
struct Platform {
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~Platform() = default;
    
    /**
     * @brief Check if a keyboard key has been pressed
     * 
     * Non-blocking check for keyboard input. Returns true if a key
     * is available to be read with getch().
     * 
     * @return true if key is pressed and waiting, false otherwise
     */
    virtual bool kbhit() = 0;
    
    /**
     * @brief Get a character from keyboard input
     * 
     * Reads a single character from the keyboard. Should be used
     * in conjunction with kbhit() for non-blocking input.
     * 
     * @return ASCII code of the pressed key
     */
    virtual int getch() = 0;
    
    /**
     * @brief Clear the console screen and move cursor to home
     * 
     * Clears all text from the console and positions the cursor
     * at the top-left corner (0,0).
     */
    virtual void clear_screen() = 0;
    
    /**
     * @brief Set cursor visibility
     * 
     * Controls whether the text cursor is visible in the console.
     * Typically disabled during gameplay for cleaner appearance.
     * 
     * @param visible true to show cursor, false to hide it
     */
    virtual void set_cursor_visible(bool visible) = 0;
    
    /**
     * @brief Enable ANSI escape sequence support
     * 
     * Enables support for ANSI escape sequences for text formatting
     * and cursor control. On Windows, this may enable virtual terminal
     * processing in the console.
     */
    virtual void enable_ansi() = 0;
};

/**
 * @brief Factory function to create platform-specific implementation
 * 
 * Creates and returns a Platform implementation appropriate for the
 * current operating system. On Windows, returns a Win32 console
 * implementation. On POSIX systems, returns a termios-based implementation.
 * 
 * @return std::unique_ptr<Platform> Platform implementation for current OS
 */
std::unique_ptr<Platform> createPlatform();
