/**
 * @file name_entry_modal.h
 * @brief Modal dialog for entering player names for high scores
 * 
 * This file defines the NameEntryModal class which provides a simple
 * text input interface for entering player names when achieving high scores.
 */

#pragma once
#include <string>

/**
 * @brief Modal dialog for high score name entry
 * 
 * NameEntryModal provides a simple text input interface that appears
 * when a player achieves a high score. It handles keyboard input,
 * text editing (including backspace), and validation.
 */
class NameEntryModal { 
public: 
    /**
     * @brief Start the name entry process
     * 
     * Initializes the modal for name entry, clearing any previous input.
     */
    void start(); 
    
    /**
     * @brief Feed a character to the input buffer
     * 
     * Adds a character to the current name being entered.
     * 
     * @param c Character to add to the name
     */
    void feed_char(wchar_t c); 
    
    /**
     * @brief Remove the last character from input
     * 
     * Handles backspace functionality by removing the last entered character.
     */
    void backspace(); 
    
    /**
     * @brief Check if name entry is complete
     * 
     * Returns true when the user has finished entering their name
     * and the input is ready to be submitted.
     * 
     * @return true if name entry is complete, false otherwise
     */
    bool ready() const; 
    
    /**
     * @brief Get the entered name
     * 
     * Returns the name currently entered by the user.
     * 
     * @return The entered name as a wide string
     */
    std::wstring name() const; 
    
    /**
     * @brief Draw the name entry modal
     * 
     * Renders the name entry interface including prompt text and
     * current input buffer.
     * 
     * @param context Drawing context (typically HDC cast to void*)
     */
    void draw(void* context); 
    
private: 
    std::wstring buf;  ///< Current input buffer
};
