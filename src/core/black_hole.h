/**
 * @file black_hole.h
 * @brief Black hole physics and state for game modes
 * 
 * This file defines the BlackHole structure and physics calculations
 * for gravitational attraction effects in the game.
 */

#pragma once

/**
 * @brief Black hole state and physics
 * 
 * Represents a gravitational attractor that affects balls and paddles.
 * Uses simplified Newtonian gravity (F = strength / r^2) without
 * relativistic effects since we don't need actual black hole physics.
 */
struct BlackHole {
    double x = 0.0;              ///< Center X position
    double y = 0.0;              ///< Center Y position
    double vx = 0.0;             ///< Horizontal velocity (for moving black holes)
    double vy = 0.0;             ///< Vertical velocity (for moving black holes)
    double strength = 500.0;     ///< Gravitational strength (not actual mass)
    double radius = 2.0;         ///< Visual radius for rendering
    double influence = 100.0;    ///< Maximum distance for gravitational effect
    bool moving = false;         ///< Whether this black hole moves
    
    /**
     * @brief Calculate gravitational force on a point
     * 
     * Uses inverse square law: F = strength / r^2
     * Force is capped at close distances to prevent singularities.
     * 
     * @param px Point X coordinate
     * @param py Point Y coordinate
     * @param fx Output force X component
     * @param fy Output force Y component
     */
    void calculateForce(double px, double py, double& fx, double& fy) const;
    
    /**
     * @brief Update black hole position if moving
     * 
     * @param dt Time delta in seconds
     * @param bounds_w Arena width
     * @param bounds_h Arena height
     */
    void update(double dt, int bounds_w, int bounds_h);
};
