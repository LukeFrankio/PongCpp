/**
 * @file game_win.h
 * @brief Windows GUI interface for PongCpp
 * 
 * This file declares the main entry point for the Windows GUI version
 * of Pong using native Win32 APIs and GDI for rendering.
 */

#pragma once

#include <windows.h>

/**
 * @brief Main entry point for Windows GUI Pong game
 * 
 * Creates and runs a windowed version of Pong using Win32 APIs and GDI
 * for rendering. The implementation is completely self-contained with no
 * external library dependencies beyond standard Windows system libraries.
 * 
 * Features include:
 * - Native Win32 window with proper message handling
 * - GDI-based rendering for smooth graphics
 * - Mouse and keyboard input support
 * - DPI-aware scaling for high-resolution displays
 * - Settings persistence and high score tracking
 * - Right-click context menu for configuration
 * 
 * @param hInstance Handle to the application instance
 * @param nCmdShow Window display state (SW_SHOW, SW_MAXIMIZE, etc.)
 * @return Application exit code (0 for success)
 */
int run_win_pong(HINSTANCE hInstance, int nCmdShow);
