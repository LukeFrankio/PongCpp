/**
 * @file ui_state.h
 * @brief UI state management for different interface modes
 * 
 * This file defines the UI state structures that track the current
 * interface mode and visual state of the Windows GUI application.
 */

#pragma once

/**
 * @brief User interface modes for the Windows GUI application
 * 
 * Defines the different states the UI can be in, each corresponding
 * to a different screen or interaction mode.
 */
enum class UIMode { 
    Gameplay,   ///< Normal gameplay mode with game rendering
    MainMenu,   ///< Main menu screen with game options
    NameEntry,  ///< High score name entry modal dialog
    Settings    ///< Settings/configuration panel
};

/**
 * @brief Current UI state and visual properties
 * 
 * Tracks the current UI mode, highlighted menu items, and whether
 * the display needs to be redrawn. Used by the rendering system
 * to determine what UI elements to display.
 */
struct UIState { 
    UIMode mode = UIMode::Gameplay;  ///< Current UI mode/screen
    int highlight = -1;              ///< Currently highlighted menu item (-1 for none)
    bool redraw = true;              ///< Whether the display needs to be redrawn
};
