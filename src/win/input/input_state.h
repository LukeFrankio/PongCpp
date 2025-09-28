/**
 * @file input_state.h
 * @brief Input state tracking for keyboard and mouse events
 * 
 * This file defines the InputState structure that tracks the current
 * state of keyboard keys and mouse input, including edge detection
 * for press/release events.
 */

#pragma once

/**
 * @brief Input state tracker for keyboard and mouse events
 * 
 * InputState maintains the current and previous frame state of all
 * input devices, enabling detection of key press/release events
 * and mouse interactions. The structure supports:
 * - All 256 virtual key codes
 * - Mouse position and button state
 * - Mouse wheel delta
 * - Edge detection for key press/release events
 */
struct InputState {
    bool keys[256] = {false};    ///< Current frame key states (indexed by virtual key code)
    bool prev[256] = {false};    ///< Previous frame key states for edge detection
    int mx = 0;                  ///< Mouse X position in window coordinates
    int my = 0;                  ///< Mouse Y position in window coordinates
    bool mdown = false;          ///< Left mouse button is currently pressed
    int wheel = 0;               ///< Mouse wheel delta this frame
    bool click = false;          ///< Mouse was clicked this frame (edge event)
    
    /**
     * @brief Advance to next frame by saving current state as previous
     * 
     * Copies current key states to previous state array and resets
     * per-frame events like click and wheel delta.
     */
    void advance() { 
        for (int i = 0; i < 256; i++) 
            prev[i] = keys[i]; 
        click = false; 
        wheel = 0; 
    }
    
    /**
     * @brief Check if a key is currently pressed
     * 
     * @param vk Virtual key code to check
     * @return true if key is currently pressed, false otherwise
     */
    bool is_pressed(int vk) const { 
        return vk >= 0 && vk < 256 && keys[vk]; 
    }
    
    /**
     * @brief Check if a key was just pressed this frame
     * 
     * Detects the rising edge of a key press (pressed now but not last frame).
     * 
     * @param vk Virtual key code to check
     * @return true if key was just pressed, false otherwise
     */
    bool just_pressed(int vk) const { 
        return vk >= 0 && vk < 256 && keys[vk] && !prev[vk]; 
    }
    
    /**
     * @brief Check if a key was just released this frame
     * 
     * Detects the falling edge of a key press (not pressed now but was last frame).
     * 
     * @param vk Virtual key code to check
     * @return true if key was just released, false otherwise
     */
    bool just_released(int vk) const { 
        return vk >= 0 && vk < 256 && !keys[vk] && prev[vk]; 
    }
};
