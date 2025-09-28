/**
 * @file win_window.h
 * @brief Windows window management and creation
 * 
 * This file defines the WinWindow class and related structures for
 * creating and managing Windows application windows.
 */

#pragma once
#include <windows.h>

/**
 * @brief Parameters for window creation
 * 
 * Contains the basic parameters needed to create a Windows application window.
 */
struct WindowParams { 
    int width = 800;           ///< Window width in pixels
    int height = 600;          ///< Window height in pixels  
    int showCmd = SW_SHOW;     ///< Window display state (SW_SHOW, SW_MAXIMIZE, etc.)
};

/**
 * @brief Windows application window wrapper
 * 
 * WinWindow provides a C++ wrapper around Windows window creation and
 * management, handling the Win32 API calls and resource cleanup.
 */
class WinWindow {
public:
    /**
     * @brief Construct a new WinWindow
     * 
     * Creates an uninitialized window wrapper. Call create() to create the actual window.
     */
    WinWindow();
    
    /**
     * @brief Destroy the window wrapper and clean up resources
     * 
     * Automatically destroys the window if it exists.
     */
    ~WinWindow();
    
    /**
     * @brief Create the Windows application window
     * 
     * Creates the actual Win32 window using the specified parameters.
     * 
     * @param p Window creation parameters
     * @param hInst Application instance handle
     * @return true if window creation succeeded, false otherwise
     */
    bool create(const WindowParams& p, HINSTANCE hInst);
    
    /**
     * @brief Destroy the window
     * 
     * Destroys the Win32 window and releases associated resources.
     */
    void destroy();
    
    /**
     * @brief Get the window handle
     * 
     * Returns the Win32 window handle (HWND) for use in API calls.
     * 
     * @return Window handle, or nullptr if window not created
     */
    HWND hwnd() const { return hWnd; }
    
private:
    HWND hWnd = nullptr;  ///< Win32 window handle
};
