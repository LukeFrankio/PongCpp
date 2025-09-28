/**
 * @file game_session.h
 * @brief Game session management for Windows GUI version
 * 
 * This file defines the GameSession class which manages a single
 * game instance, including its core logic, state, and lifecycle.
 */

#pragma once

class GameCore;

/**
 * @brief Manages a single game session and its lifecycle
 * 
 * GameSession provides a higher-level wrapper around GameCore,
 * managing the lifecycle of a game instance including initialization,
 * updates, and cleanup. It serves as the interface between the
 * application controller and the platform-independent game logic.
 * 
 * The session manages dynamic allocation of the game core to provide
 * flexibility in game initialization and reset operations.
 */
class GameSession {
public:
    /**
     * @brief Construct a new GameSession
     * 
     * Creates a new game session but does not initialize the game core.
     * The core is created lazily when first accessed.
     */
    GameSession();
    
    /**
     * @brief Destroy the GameSession and clean up resources
     * 
     * Properly shuts down the game session and releases the game core.
     */
    ~GameSession();
    
    /**
     * @brief Update the game session for one frame
     * 
     * Advances the game simulation by the specified time delta.
     * Handles all game logic updates including physics, AI, and scoring.
     * 
     * @param dt Delta time in seconds since last update
     */
    void update(double dt);
    
    /**
     * @brief Get reference to the game core
     * 
     * Provides access to the underlying GameCore instance, creating
     * it if necessary. The core contains all game state and logic.
     * 
     * @return Reference to the GameCore instance
     */
    GameCore& core();
    
private:
    GameCore* corePtr = nullptr;  ///< Dynamically allocated game core instance
};
