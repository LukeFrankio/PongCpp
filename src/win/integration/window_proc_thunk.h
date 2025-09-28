/**
 * @file window_proc_thunk.h
 * @brief Windows message procedure thunk for object-oriented window handling
 * 
 * This file provides a message procedure thunk that enables object-oriented
 * window message handling in the Win32 environment.
 */

#pragma once
#include <windows.h>

/**
 * @brief Refactored Windows procedure for object-oriented message handling
 * 
 * This window procedure serves as a thunk that enables object-oriented
 * message handling by routing Windows messages to appropriate class methods.
 * It handles the typical Win32 message routing pattern where the window
 * procedure needs to call methods on a C++ object.
 * 
 * @param hwnd Handle to the window receiving the message
 * @param msg Message identifier (WM_* constants)
 * @param wParam Additional message information (message-dependent)
 * @param lParam Additional message information (message-dependent)
 * @return Result of message processing (message-dependent)
 */
LRESULT CALLBACK RefactorWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
