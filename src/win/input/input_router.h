/**
 * @file input_router.h
 * @brief Windows message routing for input events
 * 
 * This file defines the InputRouter class which processes Windows
 * messages and converts them into a unified input state representation.
 */

#pragma once
#include <windows.h>
#include "input_state.h"

/**
 * @brief Routes Windows messages to unified input state
 * 
 * InputRouter processes Windows input messages (keyboard, mouse)
 * and maintains a unified InputState that can be easily consumed
 * by game logic and UI systems. It handles the translation between
 * Windows-specific message formats and the application's input model.
 * 
 * The router maintains both current and previous frame state to
 * enable edge detection for key press/release events.
 */
class InputRouter {
public:
    /**
     * @brief Begin a new input frame
     * 
     * Advances the input state to a new frame by saving current
     * state as previous state and resetting per-frame events.
     * Should be called once per frame before processing messages.
     */
    void new_frame() { 
        state.advance(); 
    }
    
    /**
     * @brief Handle a Windows input message
     * 
     * Processes a Windows message and updates the input state accordingly.
     * Handles keyboard, mouse, and other input-related messages.
     * 
     * @param msg Windows message identifier (WM_KEYDOWN, WM_MOUSEMOVE, etc.)
     * @param w WPARAM value from the message
     * @param l LPARAM value from the message
     */
    void handle(UINT msg, WPARAM w, LPARAM l);
    
    /**
     * @brief Get read-only access to current input state
     * 
     * Returns the current input state for reading by game logic
     * and UI systems.
     * 
     * @return Const reference to current InputState
     */
    const InputState& get() const { 
        return state; 
    }
    
    /**
     * @brief Get mutable access to input state
     * 
     * Returns mutable access to the input state for advanced
     * manipulation or testing purposes.
     * 
     * @return Mutable reference to current InputState
     */
    InputState& mutable_state() { 
        return state; 
    }
    
private:
    InputState state;  ///< Current input state maintained by the router
};
