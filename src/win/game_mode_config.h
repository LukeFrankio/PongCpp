/**
 * @file game_mode_config.h
 * @brief Game mode configuration system
 * 
 * Defines a flexible game mode configuration structure that replaces
 * the old enum-based system with composable toggles.
 */

#pragma once

/**
 * @brief Game mode configuration flags
 * 
 * This structure allows combining different game mode features
 * instead of having a fixed enum of predefined modes.
 */
struct GameModeConfig {
    bool multiball = false;           ///< Enable multiple balls
    bool obstacles = false;           ///< Enable obstacle blocks
    bool obstacles_moving = false;    ///< Make obstacles move
    bool blackholes = false;          ///< Enable black holes
    bool blackholes_moving = false;   ///< Make black holes move
    bool three_enemies = false;       ///< Enable horizontal paddles (ThreeEnemies mode)
    bool obstacles_gravity = false;   ///< Obstacles affected by black hole gravity
    bool blackholes_destroy_balls = true; ///< Black holes destroy/reset balls
    int blackhole_count = 1;          ///< Number of black holes (1-5)
    int multiball_count = 3;          ///< Number of balls in multiball mode (2-5)
    
    /**
     * @brief Check if this is classic mode (all features disabled)
     */
    bool isClassic() const {
        return !multiball && !obstacles && !blackholes && !three_enemies;
    }
    
    /**
     * @brief Get a human-readable description of the mode
     */
    const char* getDescription() const;
};
