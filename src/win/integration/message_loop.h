/**
 * @file message_loop.h
 * @brief Windows message loop integration
 * 
 * This file provides the main Windows message loop that integrates
 * the Windows GUI application with the native Win32 event system.
 */

#pragma once

class WinWindow; 
class AppController;

/**
 * @brief Run the main Windows message loop
 * 
 * Executes the standard Windows message loop, processing window messages
 * and dispatching them to the appropriate handlers. Integrates the 
 * application controller with the Windows event system.
 * 
 * The message loop continues running until the application receives
 * a quit message (WM_QUIT) or encounters an error.
 * 
 * @param window Reference to the main application window
 * @param controller Reference to the application controller
 * @return Application exit code (0 for success, non-zero for error)
 */
int run_message_loop(WinWindow& window, AppController& controller);
