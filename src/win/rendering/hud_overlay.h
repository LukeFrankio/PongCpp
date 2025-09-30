/**
 * @file hud_overlay.h
 * @brief HUD overlay rendering for game statistics and score display
 * 
 * This file provides the HudOverlay class responsible for rendering
 * game statistics, performance metrics, and scores on top of the game area.
 */

#pragma once
#include <windows.h>

struct GameState; 
struct SRStats; 
struct UIState;

/**
 * @brief HUD overlay renderer for game information display
 * 
 * HudOverlay handles the rendering of text-based information overlaid
 * on the game area, including:
 * - Current game scores
 * - High score information  
 * - Performance statistics (when using path tracer)
 * - Frame timing information
 * 
 * The overlay uses GDI text rendering functions and automatically
 * scales with DPI settings for proper display on high-resolution monitors.
 */
class HudOverlay {
public:
    /**
     * @brief Draw the HUD overlay on the specified device context
     * 
     * Renders all HUD elements including scores, performance statistics,
     * and other game information using the provided game state and metrics.
     * 
     * @param gs Current game state containing scores and game data
     * @param stats Optional performance statistics from software renderer (can be nullptr)
     * @param dc Windows device context for drawing operations
     * @param w Width of the drawing area in pixels
     * @param h Height of the drawing area in pixels  
     * @param dpi Current DPI setting for scaling calculations
     * @param highScore Current high score to display
     * @param isGPU True if using GPU acceleration, false for CPU rendering
     */
    void draw(const GameState& gs, const SRStats* stats, HDC dc, int w, int h, int dpi, int highScore, bool isGPU = false);
};
