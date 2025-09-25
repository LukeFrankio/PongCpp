/**
 * @file game.h
 * @brief Console-based game interface for PongCpp
 * 
 * This file defines the Game class that handles the console version
 * of Pong, including ASCII rendering, keyboard input processing,
 * and the main game loop.
 */

#pragma once

#include "platform.h"
#include "core/game_core.h"

/**
 * @brief Console-based Pong game implementation
 * 
 * The Game class provides a text-based interface for playing Pong
 * in a console/terminal environment. It uses ASCII characters to
 * render the game field, paddles, and ball, and processes keyboard
 * input for player control.
 * 
 * Features:
 * - ASCII art rendering of paddles and ball
 * - Real-time keyboard input processing
 * - Score display
 * - Cross-platform console support via Platform abstraction
 */
class Game {
public:
    /**
     * @brief Construct a new Game object
     * 
     * Initializes the console game with specified dimensions and
     * a reference to the platform abstraction layer.
     * 
     * @param w Width of the game area in characters
     * @param h Height of the game area in characters  
     * @param platform Reference to platform-specific console interface
     */
    Game(int w, int h, Platform &platform);
    
    /**
     * @brief Run the main game loop
     * 
     * Starts the game loop which continues until the player quits
     * or an error occurs. Handles timing, input processing, game
     * state updates, and rendering.
     * 
     * @return Exit code (0 for normal exit, non-zero for error)
     */
    int run();

private:
    /**
     * @brief Update game state for one frame
     * 
     * Calls the core game logic to advance the simulation by
     * the specified time delta.
     * 
     * @param core Reference to game core logic
     * @param dt Time delta in seconds since last frame
     */
    void update(GameCore &core, double dt);
    
    /**
     * @brief Render current game state to console
     * 
     * Draws the game field, paddles, ball, and score using
     * ASCII characters. Clears the screen and redraws the
     * entire game state each frame.
     * 
     * @param core Reference to game core for state access
     */
    void render(GameCore &core);
    
    /**
     * @brief Process keyboard input
     * 
     * Checks for keyboard input and translates key presses
     * into game actions like paddle movement or quitting.
     * 
     * Supported keys:
     * - W/S: Move left paddle up/down
     * - Arrow keys: Alternative paddle controls
     * - Q/Escape: Quit game
     * 
     * @param core Reference to game core for control input
     */
    void process_input(GameCore &core);

    int width, height;        ///< Console dimensions in characters
    Platform &platform;       ///< Reference to platform abstraction
    int paddle_h = 5;         ///< Paddle height (legacy compatibility)
    bool running = true;      ///< Game loop control flag
};
