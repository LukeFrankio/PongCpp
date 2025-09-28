/**
 * @file frame_timer.h
 * @brief High-precision frame timing utility for smooth animation
 * 
 * This file provides the FrameTimer class used to measure frame durations
 * and calculate delta time for smooth, frame-rate independent animations.
 */

#pragma once
#include <chrono>

/**
 * @brief High-precision frame timing utility
 * 
 * FrameTimer provides accurate measurement of time elapsed between frames
 * using std::chrono::steady_clock. This enables smooth animation and 
 * physics calculations that are independent of frame rate variations.
 * 
 * The timer automatically handles initialization and provides delta time
 * in seconds as a floating-point value suitable for physics calculations.
 */
class FrameTimer {
public:
    /**
     * @brief Construct a new FrameTimer and initialize timing
     * 
     * Records the current time as the starting point for delta calculations.
     */
    FrameTimer();
    
    /**
     * @brief Update timer and return elapsed time since last tick
     * 
     * Calculates the time elapsed since the last call to tick() (or since
     * construction for the first call) and updates the internal timer state.
     * 
     * @return Time elapsed in seconds as a double-precision floating point value
     */
    double tick();
    
private:
    using clock = std::chrono::steady_clock;  ///< High-resolution monotonic clock type
    clock::time_point last;                   ///< Time point of last tick() call
};
