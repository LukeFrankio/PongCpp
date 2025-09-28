/**
 * @file pt_renderer_adapter.h
 * @brief Adapter for the software path tracing renderer
 * 
 * This file defines the PTRendererAdapter class which provides a
 * simplified interface to the software path tracing renderer for
 * integration with the Windows GUI application.
 */

#pragma once
#include <windows.h>
#include "../soft_renderer.h" // for SRConfig, SRStats

class SoftRenderer; 
struct GameState; 
struct Settings; 
struct UIState; 

/**
 * @brief Adapter interface for software path tracing renderer
 * 
 * PTRendererAdapter provides a simplified, high-level interface to the
 * software path tracing renderer. It handles configuration management,
 * rendering coordination, and statistics reporting while abstracting
 * the complexity of the underlying path tracing implementation.
 * 
 * The adapter manages the lifecycle of the path tracer, converts
 * application settings to renderer configuration, and provides
 * access to performance statistics for display in the HUD.
 */
class PTRendererAdapter { 
public: 
    /**
     * @brief Construct a new PTRendererAdapter
     * 
     * Creates the adapter but does not initialize the underlying
     * renderer. Call configure() before rendering.
     */
    PTRendererAdapter(); 
    
    /**
     * @brief Destroy the adapter and clean up resources
     * 
     * Properly shuts down the path tracer and releases resources.
     */
    ~PTRendererAdapter(); 
    
    /**
     * @brief Configure the renderer with current settings
     * 
     * Updates the path tracer configuration based on application
     * settings including ray count, bounces, quality parameters, etc.
     * 
     * @param settings Current application settings
     */
    void configure(const Settings& settings); 
    
    /**
     * @brief Resize the renderer to match window dimensions
     * 
     * Updates the internal rendering resolution and buffers to
     * match the current window size.
     * 
     * @param w New width in pixels
     * @param h New height in pixels
     */
    void resize(int w, int h); 
    
    /**
     * @brief Render the current frame using path tracing
     * 
     * Performs path traced rendering of the game state and outputs
     * the result to the target device context.
     * 
     * @param gameState Current game state to render
     * @param settings Current rendering settings
     * @param uiState Current UI state for overlay rendering
     * @param target Target device context for output
     */
    void render(const GameState& gameState, const Settings& settings, const UIState& uiState, HDC target); 
    
    /**
     * @brief Get performance statistics from the renderer
     * 
     * Returns detailed performance statistics from the path tracer
     * including timing information, sample counts, and quality metrics.
     * 
     * @return Pointer to statistics structure (may be nullptr if no stats available)
     */
    const SRStats* stats() const; 
    
private: 
    SoftRenderer* impl = nullptr;  ///< Underlying path tracing implementation
    SRConfig cfg{};                ///< Current renderer configuration
};
