/**
 * @file pt_renderer_adapter.h
 * @brief Adapter for path tracing renderer (D3D12 GPU or CPU fallback)
 * 
 * This file defines the PTRendererAdapter class which provides a
 * simplified interface to the path tracing renderer for
 * integration with the Windows GUI application.
 * 
 * Phase 5: Supports both D3D12 GPU acceleration and CPU fallback
 */

#pragma once
#include <windows.h>
#include "../soft_renderer.h" // for SRConfig, SRStats

class SoftRenderer;
class D3D12Renderer;
struct GameState; 
struct Settings; 
struct UIState; 

/**
 * @brief Adapter interface for path tracing renderer (GPU or CPU)
 * 
 * PTRendererAdapter provides a simplified, high-level interface to the
 * path tracing renderer. It automatically selects D3D12 GPU acceleration
 * if available, falling back to CPU rendering if GPU initialization fails.
 * 
 * The adapter manages the lifecycle of the path tracer, converts
 * application settings to renderer configuration, and provides
 * access to performance statistics for display in the HUD.
 * 
 * Phase 5: GPU acceleration provides 10-50x performance improvement
 */
class PTRendererAdapter { 
public: 
    /**
     * @brief Construct a new PTRendererAdapter
     * 
     * Attempts to initialize D3D12 GPU renderer. If that fails,
     * falls back to CPU renderer. Call isUsingGPU() to check which.
     */
    PTRendererAdapter(); 
    
    /**
     * @brief Destroy the adapter and clean up resources
     * 
     * Properly shuts down the path tracer and releases resources.
     */
    ~PTRendererAdapter(); 
    
    /**
     * @brief Check if using GPU acceleration
     * 
     * @return true if D3D12 GPU renderer is active, false if CPU fallback
     */
    bool isUsingGPU() const { return usingGPU_; }
    
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
    D3D12Renderer* gpuImpl_ = nullptr;   ///< D3D12 GPU renderer (if available)
    SoftRenderer* cpuImpl_ = nullptr;    ///< CPU fallback renderer
    bool usingGPU_ = false;               ///< True if GPU renderer active
    SRConfig cfg{};                       ///< Current renderer configuration
};

