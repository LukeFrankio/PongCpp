/**
 * @file game_vulkan.h
 * @brief Vulkan-based Windows GUI interface for PongCpp
 * 
 * This file declares the main entry point for the Vulkan-accelerated Windows
 * version of Pong using Win32 APIs for windowing and Vulkan for rendering.
 */

#pragma once

#include <windows.h>

/**
 * @brief Main entry point for Vulkan-accelerated Pong game
 * 
 * Creates and runs a windowed version of Pong using Win32 APIs for window
 * management and Vulkan API for hardware-accelerated rendering. The 
 * implementation matches the feature set of the GDI version while leveraging
 * modern graphics APIs for enhanced performance and visual effects.
 * 
 * Features include:
 * - Native Win32 window with proper message handling
 * - Vulkan-based hardware-accelerated rendering
 * - Mouse and keyboard input support matching GDI version
 * - DPI-aware scaling for high-resolution displays
 * - Settings persistence and high score tracking (reused from GDI version)
 * - Right-click context menu for configuration
 * - Enhanced visual effects using modern shaders
 * 
 * @param hInstance Handle to the application instance
 * @param nCmdShow Window display state (SW_SHOW, SW_MAXIMIZE, etc.)
 * @return Application exit code (0 for success)
 */
int run_vulkan_pong(HINSTANCE hInstance, int nCmdShow);