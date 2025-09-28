/**
 * @file widgets.h
 * @brief Basic UI widget components for settings panels
 * 
 * This file defines simple UI widgets used for building settings
 * and configuration interfaces in the Windows GUI version.
 */

#pragma once

/**
 * @brief Slider widget for adjusting numeric values
 * 
 * SliderWidget provides a simple integer-based slider control
 * for adjusting settings values within a specified range.
 * The widget maintains a pointer to the value it controls,
 * allowing direct manipulation of settings.
 */
struct SliderWidget { 
    int* value = nullptr;  ///< Pointer to the value being controlled
    int min = 0;           ///< Minimum allowed value
    int max = 100;         ///< Maximum allowed value
    
    /**
     * @brief Draw the slider widget
     * 
     * Renders the slider control using the provided drawing context.
     * The exact type of the context parameter depends on the rendering system.
     * 
     * @param context Drawing context (typically HDC cast to void*)
     */
    void draw(void* context); 
};

/**
 * @brief Simple button widget for user interactions
 * 
 * ButtonWidget provides a clickable button control with text label.
 * The widget tracks its clicked state which can be checked by the
 * application after rendering.
 */
struct ButtonWidget { 
    const char* label = "";   ///< Text label displayed on the button
    bool clicked = false;     ///< True if button was clicked this frame
    
    /**
     * @brief Draw the button widget
     * 
     * Renders the button control with its label using the provided
     * drawing context. Updates the clicked state based on mouse interaction.
     * 
     * @param context Drawing context (typically HDC cast to void*)
     */
    void draw(void* context); 
};
