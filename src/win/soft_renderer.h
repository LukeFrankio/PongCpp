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
 *      emissiveIntensity: scales emissive ball radiance (no limit)
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
    float emissiveIntensity = 2.5f;       // Multiplier on emissive ball radiance (no hard limit)
    float paddleEmissiveIntensity = 0.0f; // Multiplier on paddle emissive radiance (0=no emission, no hard limit)

    // Denoising / accumulation
    float accumAlpha = 0.1f;              // Temporal EMA factor (0.01 .. 0.9)
    float denoiseStrength = 0.35f;        // 0 disables spatial blend, 1 full 3x3 blur
    // Camera
    bool useOrtho = true;                 // When true, use orthographic camera matching 2D game mapping
    // Russian roulette termination controls
    bool  rouletteEnable = true;          // Enable Russian roulette early termination
    int   rouletteStartBounce = 2;         // Start applying roulette at or after this bounce
    float rouletteMinProb = 0.1f;          // Minimum survival probability clamp

    // Experimental: Exponential combinatorial fan-out mode.
    // When enabled, for P pixels and maxBounces=B, we generate sum_{d=1..B} P^d rays (early termination disabled).
    // WARNING: Explodes extremely fast. Guarded by safety caps.
    bool  fanoutCombinatorial = false;     // Master toggle for experimental fan-out
    uint64_t fanoutMaxTotalRays = 2000000;  // Hard cap to abort to protect performance
    bool  fanoutAbortOnCap = true;          // If true, abort fan-out tracing when cap exceeded

    // Soft shadow / PBR extensions
    int   softShadowSamples = 4;            // Number of importance samples per light for soft shadows (1=hard shadow)
    float lightRadiusScale = 1.0f;          // Multiplier on physical ball radius when treated as area light
    bool  pbrEnable = true;                 // Enable physically based energy terms (Lambert 1/pi, simple Fresnel, energy conservation)
    
    // Phase 5: Advanced sampling and rendering optimizations
    int   tileSize = 16;                    // Tile size for tile-based rendering (8, 16, or 32 recommended for cache efficiency)
    bool  useBlueNoise = true;              // Use blue noise sampling instead of white noise for better low-SPP quality
    bool  useCosineWeighted = true;         // Use cosine-weighted hemisphere sampling for diffuse (2x quality improvement)
    bool  useStratified = true;             // Use stratified jittered sampling within pixels
    bool  useHaltonSeq = false;             // Use Halton low-discrepancy sequence (slower but better distribution)
    bool  adaptiveSoftShadows = true;       // Adaptive soft shadow samples (1 sample if fully lit/shadowed, more for penumbra)
    bool  useBilateralDenoise = true;       // Use bilateral filter instead of box blur (edge-preserving)
    float bilateralSigmaSpace = 1.0f;       // Bilateral spatial sigma (pixel distance falloff)
    float bilateralSigmaColor = 0.1f;       // Bilateral color sigma (luminance difference falloff)
    float lightCullDistance = 50.0f;        // Distance multiplier for light culling (lights beyond this * radius are skipped)
    
    // Phase 9: SIMD packet ray tracing
    bool  usePacketTracing = true;          // Use 4-wide SIMD ray packets for primary rays (4x throughput improvement)
};

// Runtime statistics for profiling / HUD overlay
struct SRStats {
    float msTrace = 0.0f;      // path tracing kernel (ray casting & shading)
    float msTemporal = 0.0f;   // temporal accumulation time
    float msDenoise = 0.0f;    // spatial denoise time
    float msUpscale = 0.0f;    // upscale + tone map packing time
    float msTotal = 0.0f;      // total time spent inside render()
    int internalW = 0;         // internal render target width
    int internalH = 0;         // internal render target height
    int spp = 0;               // samples per pixel this frame
    int totalRays = 0;         // spp * internalW * internalH
    int64_t projectedRays = 0; // projected (or capped) rays in fan-out mode
    bool fanoutAborted = false; // set when fan-out aborted due to safety cap
    float avgBounceDepth = 0.0f; // average number of bounces executed per path
    unsigned frame = 0;        // frame counter for renderer (post increment)
    // Phase 2 extended diagnostics
    int earlyExitCount = 0;          // number of paths terminated due to low throughput threshold
    int rouletteTerminations = 0;    // number of paths killed by Russian roulette
    bool denoiseSkipped = false;     // true when denoise pass skipped due to quality heuristic
    int threadsUsed = 1;             // number of threads used in last render (includes main)
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

    const SRStats &stats() const { return stats_; }

    const BITMAPINFO &getBitmapInfo() const { return bmpInfo; }
    const uint32_t *pixels() const { return reinterpret_cast<const uint32_t*>(pixel32.data()); }

private:
    int outW = 0, outH = 0;      // window size
    int rtW = 0, rtH = 0;        // internal render resolution
    SRConfig config{};
    BITMAPINFO bmpInfo{};        // top‑down 32bpp DIB header
    
    // Phase 2: Structure of Arrays layout for better SIMD performance
    // Instead of [RGBRGBRGB...], we have separate R[], G[], B[] arrays
    std::vector<float> accumR, accumG, accumB;  // Accumulation buffers (separate channels)
    std::vector<float> historyR, historyG, historyB;  // Previous frame (separate channels)
    bool haveHistory = false;
    std::vector<uint32_t> pixel32; // packed BGRA for GDI (A unused)
    unsigned frameCounter = 0;
    SRStats stats_{};              // last frame statistics
    
    // Phase 2: Pre-allocated scratch buffers with SoA layout
    std::vector<float> hdrR, hdrG, hdrB;          // HDR working buffers (separate channels)
    std::vector<float> denoiseR, denoiseG, denoiseB;  // Temporary for denoise pass (separate channels)

    void updateInternalResolution();
    void toneMapAndPack();
    void temporalAccumulate(const std::vector<float>& curR, const std::vector<float>& curG, const std::vector<float>& curB);
    void spatialDenoise();
};

#endif // _WIN32
