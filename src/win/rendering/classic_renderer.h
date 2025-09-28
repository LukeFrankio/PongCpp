/**
 * @file classic_renderer.h
 * @brief Classic GDI-based renderer for Pong gameplay
 * 
 * This file defines the ClassicRenderer class which provides fast,
 * traditional 2D graphics rendering using Windows GDI APIs.
 */

#pragma once
#include <windows.h>

struct GameState;

/**
 * @brief Classic GDI renderer for Pong gameplay
 * 
 * ClassicRenderer provides fast, traditional 2D graphics rendering
 * using Windows GDI APIs. It is stateless with respect to game logic
 * but caches GDI resources (pens, brushes) sized according to DPI
 * for optimal performance and visual quality.
 * 
 * The renderer creates crisp, pixel-perfect graphics suitable for
 * all systems and provides excellent performance with minimal
 * CPU usage compared to software ray tracing alternatives.
 */
class ClassicRenderer {
public:
    /**
     * @brief Construct a new ClassicRenderer
     * 
     * Initializes the renderer but does not create GDI resources.
     * Resources are created lazily when first needed.
     */
    ClassicRenderer();
    
    /**
     * @brief Destroy the ClassicRenderer and clean up GDI resources
     * 
     * Releases all cached GDI objects including pens and brushes.
     */
    ~ClassicRenderer();
    
    /**
     * @brief Render the game state to the specified device context
     * 
     * Draws the complete game scene including paddles, ball, scores,
     * and playing field using GDI drawing operations. Automatically
     * scales rendering based on DPI settings.
     * 
     * @param gameState Current game state containing positions and scores
     * @param dc Windows device context for drawing operations
     * @param winW Window width in pixels
     * @param winH Window height in pixels
     * @param dpi Current DPI setting for scaling calculations
     */
    void render(const GameState& gameState, HDC dc, int winW, int winH, int dpi);
    
    /**
     * @brief Handle window resize events
     * 
     * Called when the window is resized. Currently a no-op but kept
     * for future enhancements that might require resize handling.
     * 
     * @param winW New window width in pixels
     * @param winH New window height in pixels
     */
    void onResize(int winW, int winH);
    
private:
    /**
     * @brief Ensure GDI resources are created for the specified DPI
     * 
     * Creates or recreates GDI resources (pens, brushes) if the DPI
     * has changed since last rendering. Resources are scaled appropriately
     * for the current DPI setting.
     * 
     * @param dpi Current DPI setting
     */
    void ensureResources(int dpi);
    
    int cachedDpi = 0;         ///< Last DPI setting for which resources were created
    HPEN penThin = nullptr;    ///< Thin pen for fine details and outlines
    HPEN penGlow = nullptr;    ///< Thick pen for glow effects and emphasis
};
