/**
 * @file soft_renderer.h
 * @brief Minimal CPU path tracing style renderer (no external graphics APIs)
 *
 * This renderer creates a small per-frame ray/path traced image of the Pong
 * scene (ball as an emissive sphere, paddles as thin glass panels) and blits
 * it to the window via GDI StretchDIBits.
 *
 * Design goals:
 *  - Self‑contained (only <windows.h> + standard library)
 *  - Fully parameter driven (no fixed quality presets). Caller supplies:
 *      raysPerFrame: total rays this frame (or per-pixel when forceFullPixelRays)
 *      maxBounces: path depth (1..8)
 *      internalScalePct: internal resolution percentage (25..100)
 *      metallicRoughness: 0 (perfect mirror) .. 1 (fully rough)
 *      emissiveIntensity: scales emissive ball radiance (0.1 .. 5)
 *      accumAlpha: temporal accumulation factor (EMA) (0.01 .. 0.9)
 *      denoiseStrength: blend factor for 3x3 box denoiser (0..1)
 *      forceFullPixelRays: interpret raysPerFrame as rays per pixel instead of a global budget
 *  - Temporal accumulation + simple spatial denoise to reduce noise while the
 *    scene animates. History resets when configuration changes or on resize.
 *  - Simple physically inspired shading: diffuse walls, emissive sphere,
 *    metallic paddles with adjustable roughness.
 */

#pragma once
#ifdef _WIN32

#include <windows.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include "../core/game_core.h"

struct SRConfig {
    // Runtime toggles
    bool enablePathTracing = true;        // Master switch so caller can keep struct and just disable

    // Core sampling / resolution controls
    int  raysPerFrame = 5000;             // If forceFullPixelRays=false this is a total budget, else rays per pixel
    int  maxBounces = 3;                  // Path depth (1..8)
    int  internalScalePct = 60;           // Internal resolution percentage (25..100)
    bool forceFullPixelRays = false;      // When true, raysPerFrame == spp for every pixel

    // Material / lighting tuning
    float metallicRoughness = 0.2f;       // 0 mirror .. 1 rough
    float emissiveIntensity = 1.0f;       // Multiplier on emissive ball radiance (0.1 .. 5)

    // Denoising / accumulation
    float accumAlpha = 0.1f;              // Temporal EMA factor (0.01 .. 0.9)
    float denoiseStrength = 0.35f;        // 0 disables spatial blend, 1 full 3x3 blur
};

class SoftRenderer {
public:
    SoftRenderer();
    ~SoftRenderer();

    void configure(const SRConfig &cfg);
    void resize(int w, int h); // output framebuffer size (window size)
    void resetHistory();

    // Renders into internal pixel buffer; caller blits via getBitmapInfo/pixels
    void render(const GameState &gs);

    const BITMAPINFO &getBitmapInfo() const { return bmpInfo; }
    const uint32_t *pixels() const { return reinterpret_cast<const uint32_t*>(pixel32.data()); }

private:
    int outW = 0, outH = 0;      // window size
    int rtW = 0, rtH = 0;        // internal render resolution
    SRConfig config{};
    BITMAPINFO bmpInfo{};        // top‑down 32bpp DIB header
    std::vector<float> accum;    // accumulation buffer (RGB float per internal pixel)
    std::vector<float> history;  // previous frame (for temporal blending)
    bool haveHistory = false;
    std::vector<uint32_t> pixel32; // packed BGRA for GDI (A unused)
    unsigned frameCounter = 0;

    void updateInternalResolution();
    void toneMapAndPack();
    void temporalAccumulate(const std::vector<float>& cur);
    void spatialDenoise();
};

#endif // _WIN32
