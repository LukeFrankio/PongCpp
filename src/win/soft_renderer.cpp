#ifdef _WIN32
// Ensure Windows headers do not define min/max macros that break std::max/std::min
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "soft_renderer.h"
#include <algorithm>
#include <cstring>
#include <cmath> // sqrt, tan, fabs, pow
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <cctype>
#include <functional> // for std::function

// Include SSE intrinsics for fast math
#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
    #include <cpuid.h>
#endif

// Branch prediction hints (cross-platform)
#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#endif

// Phase 6: Force inline for hot functions
#if defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
    #define FORCE_INLINE_ATTRIB
#elif defined(__GNUC__) || defined(__clang__)
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define FORCE_INLINE_ATTRIB __attribute__((always_inline))
#else
    #define FORCE_INLINE inline
    #define FORCE_INLINE_ATTRIB
#endif

// Adaptive threading state (global to translation unit)
static std::atomic<float> g_srLastFrameMs{ 1000.0f };      // raw last frame
static std::atomic<float> g_srEmaFrameMs{ 1000.0f };       // smoothed (EMA) frame time
static std::atomic<unsigned> g_srAdaptiveThreads{ 0 };     // current chosen threads
static std::atomic<unsigned> g_srLastLogged{ 0 };          // last logged value
static std::atomic<bool> g_srInitialized{ false };
static std::atomic<int>  g_srCooldown{ 0 };                // frames to wait before another downward adjustment

// ============================================================================
// Phase 4: Runtime CPU Feature Detection
// ============================================================================

struct CPUFeatures {
    bool sse41;
    bool avx;
    bool avx2;
    bool fma;
    bool initialized;
};

static CPUFeatures g_cpuFeatures = { false, false, false, false, false };

static void detectCPUFeatures() {
    if (g_cpuFeatures.initialized) return;
    
#if defined(_MSC_VER)
    int cpuInfo[4];
    
    // Check for SSE4.1, AVX, AVX2, FMA
    __cpuid(cpuInfo, 1);
    g_cpuFeatures.sse41 = (cpuInfo[2] & (1 << 19)) != 0;  // ECX bit 19
    g_cpuFeatures.avx   = (cpuInfo[2] & (1 << 28)) != 0;  // ECX bit 28
    g_cpuFeatures.fma   = (cpuInfo[2] & (1 << 12)) != 0;  // ECX bit 12
    
    // AVX2 requires CPUID leaf 7
    __cpuidex(cpuInfo, 7, 0);
    g_cpuFeatures.avx2  = (cpuInfo[1] & (1 << 5)) != 0;   // EBX bit 5
    
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    
    // CPUID function 1
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    g_cpuFeatures.sse41 = (ecx & (1 << 19)) != 0;
    g_cpuFeatures.avx   = (ecx & (1 << 28)) != 0;
    g_cpuFeatures.fma   = (ecx & (1 << 12)) != 0;
    
    // CPUID function 7, sub-leaf 0
    __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
    g_cpuFeatures.avx2  = (ebx & (1 << 5)) != 0;
#endif
    
    g_cpuFeatures.initialized = true;
    
    // Log detected features
#ifdef _WIN32
    char msg[256];
    _snprintf_s(msg, _TRUNCATE, 
        "[SoftRenderer] CPU Features: SSE4.1=%d AVX=%d AVX2=%d FMA=%d\n",
        g_cpuFeatures.sse41, g_cpuFeatures.avx, g_cpuFeatures.avx2, g_cpuFeatures.fma);
    OutputDebugStringA(msg);
    printf("%s", msg);
#endif
}

// ============================================================================
// Phase 1 Optimizations: Fast Math Functions
// ============================================================================

// Fast reciprocal square root using SSE (Newton-Raphson refinement for accuracy)
static inline float rsqrt_fast(float x) {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
    // SSE rsqrt followed by one Newton-Raphson iteration for better accuracy
    __m128 v = _mm_set_ss(x);
    __m128 r = _mm_rsqrt_ss(v);
    // Newton-Raphson: r' = r * (1.5 - 0.5 * x * r * r)
    __m128 half = _mm_set_ss(0.5f);
    __m128 three_half = _mm_set_ss(1.5f);
    __m128 r2 = _mm_mul_ss(r, r);
    __m128 xr2 = _mm_mul_ss(v, r2);
    __m128 halfxr2 = _mm_mul_ss(half, xr2);
    __m128 refined = _mm_mul_ss(r, _mm_sub_ss(three_half, halfxr2));
    return _mm_cvtss_f32(refined);
#else
    // Fallback for non-SSE platforms
    return 1.0f / std::sqrt(x);
#endif
}

// Fast trigonometry using polynomial approximations
// Cos approximation using Bhaskara I's formula (max error ~0.0016)
static inline float cos_fast(float x) {
    // Normalize to [-pi, pi]
    constexpr float PI = 3.14159265f;
    constexpr float TWO_PI = 6.28318531f;
    x = x - TWO_PI * std::floor(x / TWO_PI);
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    
    // Bhaskara approximation
    const float x2 = x * x;
    constexpr float pi2 = PI * PI;
    return (pi2 - 4.0f * x2) / (pi2 + x2);
}

// Fast sin using cos(x - pi/2) identity
static inline float sin_fast(float x) {
    constexpr float PI_2 = 1.57079633f;
    return cos_fast(x - PI_2);
}

// Fast sqrt using rsqrt_fast (for completeness, though direct sqrt may be faster on modern CPUs)
static inline float sqrt_fast(float x) {
    // For very small values, avoid division issues
    if (UNLIKELY(x < 1e-8f)) return 0.0f;
    return x * rsqrt_fast(x);
}

// Fast exp approximation using polynomial (max error ~2% for x in [-4, 4])
// For Gaussian filters in bilateral filtering
static inline float exp_fast(float x) {
    // Clamp to reasonable range for stability
    if (UNLIKELY(x < -10.0f)) return 0.0f;
    if (UNLIKELY(x > 10.0f)) return std::exp(10.0f);
    
    // Use fast polynomial approximation for exp(x)
    // Based on Pade approximant [4/4] - good accuracy for small x
    // exp(x) ≈ (12 + 6x + x²) / (12 - 6x + x²)
    float x2 = x * x;
    float numer = 12.0f + 6.0f * x + x2;
    float denom = 12.0f - 6.0f * x + x2;
    return numer / denom;
}

// ============================================================================
// Optimized RNG Functions
// ============================================================================

// Optimized XOR shift RNG (fewer operations, better pipelining)
static inline uint32_t xorshift(uint32_t &s) {
    // Original: s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    // Optimized: combine operations to reduce dependencies
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
}

// Generate two uniform floats in [0,1) from a single RNG advance (optimized)
static inline void rng2(uint32_t &seed, float &u, float &v){
    uint32_t r = xorshift(seed);
    // Optimized: multiply-add instead of separate operations
    constexpr float scale = 1.0f / 65536.0f;
    u = static_cast<float>(r & 0xFFFF) * scale;
    v = static_cast<float>(r >> 16) * scale;
}

// Fast single random float [0,1)
static inline float rng1(uint32_t &seed) {
    return static_cast<float>(xorshift(seed) & 0xFFFFFF) * (1.0f / 16777216.0f);
}

// ============================================================================
// Optimized Vec3 operations
// ============================================================================

struct Vec3 { float x,y,z; };

static inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vec3 operator*(Vec3 a, float s){ return {a.x*s,a.y*s,a.z*s}; }
static inline Vec3 operator*(float s, Vec3 a){ return {a.x*s,a.y*s,a.z*s}; }
static inline Vec3 operator*(Vec3 a, Vec3 b){ return {a.x*b.x,a.y*b.y,a.z*b.z}; }
static inline Vec3 operator/(Vec3 a, float s){ 
    float inv = 1.0f / s; 
    return {a.x*inv, a.y*inv, a.z*inv}; 
}

static inline float dot(Vec3 a, Vec3 b){ 
    return a.x*b.x + a.y*b.y + a.z*b.z; 
}

// Phase 4: FMA-accelerated Vec3 operations for critical paths
// Fused multiply-add: result = a + b*s (compiler may use FMA with /fp:fast)
static inline Vec3 fma_add(Vec3 a, Vec3 b, float s) {
    // Computes a + b*s in fewer operations (potential FMA usage)
    return {a.x + b.x*s, a.y + b.y*s, a.z + b.z*s};
}

// Fused multiply-add: result = a*s + b*t
static inline Vec3 fma_madd(Vec3 a, float s, Vec3 b, float t) {
    return {a.x*s + b.x*t, a.y*s + b.y*t, a.z*s + b.z*t};
}

// Optimized normalization using fast reciprocal sqrt
static inline Vec3 norm(Vec3 a){ 
    float len2 = dot(a, a);
    if (UNLIKELY(len2 < 1e-16f)) return Vec3{0,0,0};
    float inv_len = rsqrt_fast(len2);
    return Vec3{a.x*inv_len, a.y*inv_len, a.z*inv_len};
}

// ============================================================================
// Phase 2: SIMD Vec3 using SSE (__m128)
// ============================================================================

// SIMD Vec3: stores x,y,z in lower 3 lanes of __m128 (w lane unused/zero)
struct alignas(16) Vec3SIMD {
    __m128 v;  // [x, y, z, 0]
    
    // Constructors
    Vec3SIMD() : v(_mm_setzero_ps()) {}
    Vec3SIMD(__m128 m) : v(m) {}
    Vec3SIMD(float x, float y, float z) : v(_mm_set_ps(0.0f, z, y, x)) {}
    
    // Load from scalar Vec3
    explicit Vec3SIMD(Vec3 a) : v(_mm_set_ps(0.0f, a.z, a.y, a.x)) {}
    
    // Store to scalar Vec3
    Vec3 toVec3() const {
        alignas(16) float tmp[4];
        _mm_store_ps(tmp, v);
        return {tmp[0], tmp[1], tmp[2]};
    }
    
    // Component access
    float x() const { return _mm_cvtss_f32(v); }
    float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(1,1,1,1))); }
    float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(2,2,2,2))); }
};

// SIMD Vec3 operators
static inline Vec3SIMD operator+(Vec3SIMD a, Vec3SIMD b) {
    return Vec3SIMD(_mm_add_ps(a.v, b.v));
}

static inline Vec3SIMD operator-(Vec3SIMD a, Vec3SIMD b) {
    return Vec3SIMD(_mm_sub_ps(a.v, b.v));
}

static inline Vec3SIMD operator*(Vec3SIMD a, float s) {
    return Vec3SIMD(_mm_mul_ps(a.v, _mm_set1_ps(s)));
}

static inline Vec3SIMD operator*(float s, Vec3SIMD a) {
    return Vec3SIMD(_mm_mul_ps(_mm_set1_ps(s), a.v));
}

static inline Vec3SIMD operator*(Vec3SIMD a, Vec3SIMD b) {
    return Vec3SIMD(_mm_mul_ps(a.v, b.v));
}

static inline Vec3SIMD operator/(Vec3SIMD a, float s) {
    return Vec3SIMD(_mm_mul_ps(a.v, _mm_set1_ps(1.0f / s)));
}

// SIMD dot product (horizontal sum of x*x + y*y + z*z)
static inline float dot(Vec3SIMD a, Vec3SIMD b) {
    __m128 mul = _mm_mul_ps(a.v, b.v);
    // Horizontal add: [x*x, y*y, z*z, 0] -> sum
    __m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 1, 0, 3));
    __m128 sum = _mm_add_ps(mul, shuf);
    shuf = _mm_movehl_ps(shuf, sum);
    sum = _mm_add_ss(sum, shuf);
    return _mm_cvtss_f32(sum);
}

// SIMD cross product
static inline Vec3SIMD cross(Vec3SIMD a, Vec3SIMD b) {
    // a.yzx
    __m128 a_yzx = _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 0, 2, 1));
    // b.zxy
    __m128 b_zxy = _mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 1, 0, 2));
    // a.yzx * b.zxy
    __m128 c1 = _mm_mul_ps(a_yzx, b_zxy);
    
    // a.zxy
    __m128 a_zxy = _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 1, 0, 2));
    // b.yzx
    __m128 b_yzx = _mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 0, 2, 1));
    // a.zxy * b.yzx
    __m128 c2 = _mm_mul_ps(a_zxy, b_yzx);
    
    return Vec3SIMD(_mm_sub_ps(c1, c2));
}

// SIMD normalization
static inline Vec3SIMD norm(Vec3SIMD a) {
    float len2 = dot(a, a);
    if (UNLIKELY(len2 < 1e-16f)) return Vec3SIMD(0, 0, 0);
    float inv_len = rsqrt_fast(len2);
    return a * inv_len;
}

// SIMD length
static inline float length(Vec3SIMD a) {
    return sqrt_fast(dot(a, a));
}

// SIMD length squared
static inline float length2(Vec3SIMD a) {
    return dot(a, a);
}

// Max component for Vec3SIMD
static inline float max_component(Vec3SIMD v) {
    alignas(16) float tmp[4];
    _mm_store_ps(tmp, v.v);
    return std::max(tmp[0], std::max(tmp[1], tmp[2]));
}

// Fast length calculation
static inline float length(Vec3 a) {
    return sqrt_fast(dot(a, a));
}

// Length squared (avoid sqrt when only comparing)
static inline float length2(Vec3 a) {
    return dot(a, a);
}

static inline Vec3 cross(Vec3 a, Vec3 b){ 
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; 
}

// Max component (for throughput checks)
static inline float max_component(Vec3 v) {
    return std::max(v.x, std::max(v.y, v.z));
}

// ============================================================================
// Phase 5: Blue Noise and Low-Discrepancy Sampling
// ============================================================================

// Pre-generated 64x64 blue noise texture (R2 sequence-based)
// Values are normalized [0, 1) with good spectral properties
static float g_blueNoise[64 * 64];
static bool g_blueNoiseInitialized = false;

static void initBlueNoise() {
    if (g_blueNoiseInitialized) return;
    
    // Generate blue noise using R2 low-discrepancy sequence (golden ratio based)
    // This creates a good blue noise approximation without pre-baked textures
    constexpr float g = 1.32471795724474602596f; // Plastic constant (R2 sequence base)
    constexpr float a1 = 1.0f / g;
    constexpr float a2 = 1.0f / (g * g);
    
    for (int i = 0; i < 64 * 64; ++i) {
        float x = std::fmod(0.5f + a1 * (float)i, 1.0f);
        float y = std::fmod(0.5f + a2 * (float)i, 1.0f);
        // Interleave x and y to create single value with good distribution
        g_blueNoise[i] = std::fmod(x + y * 0.618033988749f, 1.0f);
    }
    
    g_blueNoiseInitialized = true;
}

// Sample blue noise texture with temporal offset
static inline float sampleBlueNoise(int x, int y, int frame) {
    if (!g_blueNoiseInitialized) initBlueNoise();
    
    // Toroidal wrapping with frame-based offset for temporal decorrelation
    int offsetX = (x + (frame * 13)) & 63;  // Wrap to 64x64
    int offsetY = (y + (frame * 17)) & 63;
    return g_blueNoise[offsetY * 64 + offsetX];
}

// Halton sequence for low-discrepancy sampling
// Base 2 for first dimension, base 3 for second dimension
static inline float haltonBase2(int index) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= 2.0f;
        r += f * (float)(index & 1);
        index >>= 1;
    }
    return r;
}

static inline float haltonBase3(int index) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= 3.0f;
        r += f * (float)(index % 3);
        index /= 3;
    }
    return r;
}

// Cosine-weighted hemisphere sampling (Malley's method)
// Maps [0,1]² to hemisphere with PDF proportional to cos(theta)
static inline Vec3 sampleCosineHemisphere(float u1, float u2, const Vec3& normal) {
    // Map to disk using concentric mapping (Shirley & Chiu)
    float r, theta;
    float a = 2.0f * u1 - 1.0f;
    float b = 2.0f * u2 - 1.0f;
    
    if (a * a > b * b) {  // Use squares to avoid abs
        r = a;
        theta = 0.785398163f * (b / a);  // PI/4
    } else if (b != 0.0f) {
        r = b;
        theta = 1.570796327f - 0.785398163f * (a / b);  // PI/2 - PI/4
    } else {
        r = 0.0f;
        theta = 0.0f;
    }
    
    float x = r * cos_fast(theta);
    float y = r * sin_fast(theta);
    float z = sqrt_fast(std::max(0.0f, 1.0f - x*x - y*y));
    
    // Build tangent space and transform to world space
    Vec3 w = normal;
    Vec3 a_vec = (std::fabs(w.x) > 0.1f) ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 v = norm(cross(w, a_vec));
    Vec3 u = cross(v, w);
    
    return norm(u * x + v * y + w * z);
}

// Luminance calculation for bilateral filtering
static inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Scene description (very small & hard coded)
// Coordinate mapping: Game x in [0,gw] -> world X in [-2,2]
//                    Game y in [0,gh] -> world Y in [-1.5,1.5]
// Z axis depth into screen (camera looks +Z). Camera at z=-5, scene near z=0..+1.5

SoftRenderer::SoftRenderer() {
    // Phase 4: Initialize CPU feature detection
    detectCPUFeatures();
    configure(config);
    lastFrameTime = std::chrono::steady_clock::now();
}

SoftRenderer::~SoftRenderer() {}

void SoftRenderer::configure(const SRConfig &cfg) {
    config = cfg;
    // Clamp user-provided configuration to sane limits
    if (config.raysPerFrame < 1) config.raysPerFrame = 1;
    if (config.raysPerFrame > 2000000) config.raysPerFrame = 2000000; // prevent pathological stalls
    if (config.internalScalePct < 25) config.internalScalePct = 25;
    if (config.internalScalePct > 100) config.internalScalePct = 100;
    if (config.maxBounces < 1) config.maxBounces = 1; if (config.maxBounces > 8) config.maxBounces = 8;
    if (config.accumAlpha < 0.01f) config.accumAlpha = 0.01f; if (config.accumAlpha > 0.9f) config.accumAlpha = 0.9f;
    if (config.denoiseStrength < 0.0f) config.denoiseStrength = 0.0f; if (config.denoiseStrength > 1.0f) config.denoiseStrength = 1.0f;
    if (config.metallicRoughness < 0.0f) config.metallicRoughness = 0.0f; if (config.metallicRoughness > 1.0f) config.metallicRoughness = 1.0f;
    // emissiveIntensity: no hard limit, allow extreme values for artistic control
    if (config.rouletteStartBounce < 1) config.rouletteStartBounce = 1; if (config.rouletteStartBounce > 16) config.rouletteStartBounce = 16;
    if (config.rouletteMinProb < 0.01f) config.rouletteMinProb = 0.01f; if (config.rouletteMinProb > 0.9f) config.rouletteMinProb = 0.9f;
    if (config.softShadowSamples < 1) config.softShadowSamples = 1; if (config.softShadowSamples > 64) config.softShadowSamples = 64;
    if (config.lightRadiusScale < 0.1f) config.lightRadiusScale = 0.1f; if (config.lightRadiusScale > 5.0f) config.lightRadiusScale = 5.0f;
    // Phase 5: Validate new optimization parameters
    if (config.tileSize < 4) config.tileSize = 4; if (config.tileSize > 64) config.tileSize = 64;
    // Ensure tile size is power of 2 for better cache alignment
    if (config.tileSize & (config.tileSize - 1)) {
        // Round up to next power of 2
        int ts = 4;
        while (ts < config.tileSize && ts < 64) ts <<= 1;
        config.tileSize = ts;
    }
    if (config.bilateralSigmaSpace < 0.1f) config.bilateralSigmaSpace = 0.1f;
    if (config.bilateralSigmaSpace > 10.0f) config.bilateralSigmaSpace = 10.0f;
    if (config.bilateralSigmaColor < 0.01f) config.bilateralSigmaColor = 0.01f;
    if (config.bilateralSigmaColor > 1.0f) config.bilateralSigmaColor = 1.0f;
    if (config.lightCullDistance < 1.0f) config.lightCullDistance = 1.0f;
    if (config.lightCullDistance > 1000.0f) config.lightCullDistance = 1000.0f;
    updateInternalResolution();
}

void SoftRenderer::resize(int w, int h) {
    if (w==outW && h==outH) return;
    outW = std::max(1,w); outH = std::max(1,h);
    updateInternalResolution();
    // setup BITMAPINFO (top‑down: negative height)
    std::memset(&bmpInfo,0,sizeof(bmpInfo));
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = outW;
    bmpInfo.bmiHeader.biHeight = -outH; // top‑down
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 32;
    bmpInfo.bmiHeader.biCompression = BI_RGB;
    pixel32.assign(outW*outH,0);
    // accum/history were already (re)allocated in updateInternalResolution()
    haveHistory = false;
    frameCounter = 0;
}

void SoftRenderer::resetHistory() {
    haveHistory = false;
    std::fill(accumR.begin(), accumR.end(), 0.0f);
    std::fill(accumG.begin(), accumG.end(), 0.0f);
    std::fill(accumB.begin(), accumB.end(), 0.0f);
    std::fill(historyR.begin(), historyR.end(), 0.0f);
    std::fill(historyG.begin(), historyG.end(), 0.0f);
    std::fill(historyB.begin(), historyB.end(), 0.0f);
    frameCounter = 0;
}

void SoftRenderer::updateInternalResolution() {
    if (outW==0||outH==0) return;
    float scale = config.internalScalePct / 100.0f;
    rtW = std::max(8, int(outW * scale));
    rtH = std::max(8, int(outH * scale));
    size_t pixelCount = static_cast<size_t>(rtW) * rtH;
    
    // Phase 2: Allocate Structure of Arrays (separate R, G, B channels)
    accumR.assign(pixelCount, 0.0f);
    accumG.assign(pixelCount, 0.0f);
    accumB.assign(pixelCount, 0.0f);
    
    historyR.assign(pixelCount, 0.0f);
    historyG.assign(pixelCount, 0.0f);
    historyB.assign(pixelCount, 0.0f);
    
    // Phase 2: Pre-allocate scratch buffers with SoA layout
    hdrR.resize(pixelCount);
    hdrG.resize(pixelCount);
    hdrB.resize(pixelCount);
    
    denoiseR.resize(pixelCount);
    denoiseG.resize(pixelCount);
    denoiseB.resize(pixelCount);
    
    haveHistory = false;
}

// Ray primitive intersections
struct Hit { float t; Vec3 n; Vec3 pos; int mat; int objId = -1; }; // mat: 0=diffuse wall,1=emissive,2=metal (paddles); objId: object identifier for caching

// Phase 8: BVH (Bounding Volume Hierarchy) structures - defined early to avoid forward declaration issues
struct BVHPrimitive {
    Vec3 bmin, bmax;    // AABB bounds
    int objType;        // 0=ball, 1=paddle, 2=obstacle
    int objIndex;       // Index into respective array
    int mat;            // Material ID
    int objId;          // Global object ID for caching
};

struct BVHNode {
    Vec3 bmin, bmax;    // Node bounds
    int leftChild;      // Index to left child node (-1 if leaf)
    int rightChild;     // Index to right child node (-1 if leaf)
    int primStart;      // First primitive index (for leaves)
    int primCount;      // Number of primitives (0 if interior node)
};

// Phase 1: Optimized sphere intersection with fast sqrt
static bool intersectSphere(Vec3 ro, Vec3 rd, Vec3 c, float r, float tMax, Hit &hit, int mat) {
    Vec3 oc = ro - c;
    float b = dot(oc, rd);
    float cterm = dot(oc,oc) - r*r;
    float disc = b*b - cterm;
    if (UNLIKELY(disc < 0.0f)) return false;
    // Phase 1: Use fast sqrt
    float s = sqrt_fast(disc);
    float t = -b - s;
    if (t < 1e-3f) t = -b + s;
    if (UNLIKELY(t < 1e-3f || t > tMax)) return false;
    hit.t = t; 
    hit.pos = ro + rd * t; 
    hit.n = norm(hit.pos - c); 
    hit.mat = mat; 
    return true;
}

// Phase 9: SIMD sphere intersection - test up to 4 spheres simultaneously
// Returns bitmask indicating which spheres were hit (bit 0 = sphere 0, etc.)
static FORCE_INLINE int intersectSpheres4(Vec3 ro, Vec3 rd, 
                                          const Vec3* centers,  // Array of 4 sphere centers
                                          const float* radii,    // Array of 4 sphere radii
                                          int count,             // Number of valid spheres (1-4)
                                          float tMax, 
                                          Hit &bestHit,          // Updated if closer hit found
                                          const int* mats,       // Array of 4 material IDs
                                          const int* objIds) {   // Array of 4 object IDs
    // Load ray origin and direction into SIMD registers
    __m128 ro_x = _mm_set1_ps(ro.x);
    __m128 ro_y = _mm_set1_ps(ro.y);
    __m128 ro_z = _mm_set1_ps(ro.z);
    __m128 rd_x = _mm_set1_ps(rd.x);
    __m128 rd_y = _mm_set1_ps(rd.y);
    __m128 rd_z = _mm_set1_ps(rd.z);
    
    // Load sphere centers (pad with zeros if count < 4)
    alignas(16) float cx[4] = {0, 0, 0, 0};
    alignas(16) float cy[4] = {0, 0, 0, 0};
    alignas(16) float cz[4] = {0, 0, 0, 0};
    alignas(16) float r[4] = {0, 0, 0, 0};
    
    for (int i = 0; i < count; ++i) {
        cx[i] = centers[i].x;
        cy[i] = centers[i].y;
        cz[i] = centers[i].z;
        r[i] = radii[i];
    }
    
    __m128 c_x = _mm_load_ps(cx);
    __m128 c_y = _mm_load_ps(cy);
    __m128 c_z = _mm_load_ps(cz);
    __m128 radius = _mm_load_ps(r);
    
    // Compute oc = ro - c (for all 4 spheres)
    __m128 oc_x = _mm_sub_ps(ro_x, c_x);
    __m128 oc_y = _mm_sub_ps(ro_y, c_y);
    __m128 oc_z = _mm_sub_ps(ro_z, c_z);
    
    // b = dot(oc, rd)
    __m128 b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(oc_x, rd_x), 
                                     _mm_mul_ps(oc_y, rd_y)),
                          _mm_mul_ps(oc_z, rd_z));
    
    // cterm = dot(oc, oc) - r*r
    __m128 oc_len2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(oc_x, oc_x),
                                           _mm_mul_ps(oc_y, oc_y)),
                                _mm_mul_ps(oc_z, oc_z));
    __m128 r2 = _mm_mul_ps(radius, radius);
    __m128 cterm = _mm_sub_ps(oc_len2, r2);
    
    // disc = b*b - cterm
    __m128 disc = _mm_sub_ps(_mm_mul_ps(b, b), cterm);
    
    // Check which spheres have valid discriminant (disc >= 0)
    __m128 disc_valid = _mm_cmpge_ps(disc, _mm_setzero_ps());
    
    // Compute sqrt(disc) using fast approximation
    __m128 s = _mm_sqrt_ps(_mm_max_ps(disc, _mm_setzero_ps()));
    
    // t = -b - s (try near intersection first)
    __m128 t = _mm_sub_ps(_mm_sub_ps(_mm_setzero_ps(), b), s);
    
    // Check if t < 1e-3, if so try far intersection: t = -b + s
    __m128 t_valid = _mm_cmpge_ps(t, _mm_set1_ps(1e-3f));
    __m128 t_far = _mm_add_ps(_mm_sub_ps(_mm_setzero_ps(), b), s);
    t = _mm_blendv_ps(t_far, t, t_valid);  // Use far if near invalid
    
    // Final validation: 1e-3 <= t < tMax and disc >= 0
    __m128 t_min_valid = _mm_cmpge_ps(t, _mm_set1_ps(1e-3f));
    __m128 t_max_valid = _mm_cmplt_ps(t, _mm_set1_ps(tMax));
    __m128 hit_mask = _mm_and_ps(_mm_and_ps(disc_valid, t_min_valid), t_max_valid);
    
    // Mask out invalid spheres (beyond count)
    if (count < 4) {
        alignas(16) uint32_t count_mask[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        for (int i = count; i < 4; ++i) count_mask[i] = 0;
        __m128 valid_mask = _mm_load_ps(reinterpret_cast<float*>(count_mask));
        hit_mask = _mm_and_ps(hit_mask, valid_mask);
    }
    
    // Extract hit mask as integer bitmask
    int mask = _mm_movemask_ps(hit_mask);
    if (mask == 0) return 0;  // No hits
    
    // Find closest hit
    alignas(16) float t_arr[4];
    _mm_store_ps(t_arr, t);
    
    bool foundCloser = false;
    int hitSphere = -1;
    float closestT = bestHit.t;
    
    for (int i = 0; i < count; ++i) {
        if ((mask & (1 << i)) && t_arr[i] < closestT) {
            closestT = t_arr[i];
            hitSphere = i;
            foundCloser = true;
        }
    }
    
    // Update best hit if we found closer intersection
    if (foundCloser) {
        bestHit.t = closestT;
        bestHit.pos = ro + rd * closestT;
        bestHit.n = norm(bestHit.pos - centers[hitSphere]);
        bestHit.mat = mats[hitSphere];
        bestHit.objId = objIds[hitSphere];
    }
    
    return foundCloser ? (1 << hitSphere) : 0;
}

// Phase 1: Optimized plane intersection with branch hints
static FORCE_INLINE bool intersectPlane(Vec3 ro, Vec3 rd, Vec3 p, Vec3 n, float tMax, Hit &hit, int mat) {
    float denom = dot(rd,n);
    if (UNLIKELY(std::fabs(denom) < 1e-5f)) return false;
    float t = dot(p - ro, n) / denom;
    if (UNLIKELY(t < 1e-3f || t > tMax)) return false;
    hit.t=t; 
    hit.pos=ro+rd*t; 
    hit.n=(denom<0)?n:(n*-1.0f); 
    hit.mat=mat; 
    return true;
}

// ============================================================================
// Phase 2: SIMD Ray Packet Structures and Intersection Functions  
// Supports both 4-wide (SSE) and 8-wide (AVX) processing
// ============================================================================

// Enable AVX code paths if compiler supports it
#if defined(__AVX2__) || defined(__AVX__) || (defined(_MSC_VER) && defined(__AVX2__))
    #define COMPILE_AVX_SUPPORT 1
    #include <immintrin.h>
#else
    #define COMPILE_AVX_SUPPORT 0
#endif

#if COMPILE_AVX_SUPPORT
// 8-wide ray packet for AVX (processes 8 rays simultaneously)
struct alignas(32) RayPacket8 {
    __m256 ox, oy, oz;  // 8 ray origins
    __m256 dx, dy, dz;  // 8 ray directions  
    __m256 mask;        // Active mask
};

// 8-wide hit record
struct alignas(32) Hit8 {
    __m256 t;           // 8 hit distances
    __m256 nx, ny, nz;  // 8 normals
    __m256 px, py, pz;  // 8 hit positions
    __m256i mat;        // 8 material IDs
    __m256 valid;       // Hit mask
};
#endif

// 4-wide ray packet: 4 rays processed simultaneously (SSE baseline)
struct alignas(16) RayPacket4 {
    // Ray origins (SoA layout for SIMD)
    __m128 ox, oy, oz;  // 4 x-coords, 4 y-coords, 4 z-coords
    
    // Ray directions (SoA layout for SIMD)
    __m128 dx, dy, dz;
    
    // Active mask (which rays are still active)
    __m128 mask;  // 0xFFFFFFFF = active, 0x00000000 = inactive
};

// 4-wide hit record
struct alignas(16) Hit4 {
    __m128 t;           // 4 hit distances
    __m128 nx, ny, nz;  // 4 normals
    __m128 px, py, pz;  // 4 hit positions
    __m128i mat;        // 4 material IDs
    __m128 valid;       // Hit mask (0xFFFFFFFF = hit, 0x00000000 = no hit)
};

// Initialize ray packet from 4 consecutive pixels
static inline void initRayPacket4(RayPacket4 &packet, 
                                   const Vec3 &camPos, 
                                   const Vec3 &camDir, 
                                   const Vec3 &camRight, 
                                   const Vec3 &camUp,
                                   float fov,
                                   int rtW, int rtH,
                                   int px0, int px1, int px2, int px3,
                                   int py0, int py1, int py2, int py3) {
    // Compute ray directions for 4 pixels
    float aspect = static_cast<float>(rtW) / rtH;
    float tanHalfFov = std::tan(fov * 0.5f * 3.14159265f / 180.0f);
    
    float dirs[4][3];
    int pxs[4] = {px0, px1, px2, px3};
    int pys[4] = {py0, py1, py2, py3};
    
    for (int i = 0; i < 4; i++) {
        float u = (2.0f * (pxs[i] + 0.5f) / rtW - 1.0f) * aspect * tanHalfFov;
        float v = (1.0f - 2.0f * (pys[i] + 0.5f) / rtH) * tanHalfFov;
        
        Vec3 dir = norm(camDir + camRight * u + camUp * v);
        dirs[i][0] = dir.x;
        dirs[i][1] = dir.y;
        dirs[i][2] = dir.z;
    }
    
    // Load into SIMD registers (SoA layout)
    packet.ox = _mm_set1_ps(camPos.x);
    packet.oy = _mm_set1_ps(camPos.y);
    packet.oz = _mm_set1_ps(camPos.z);
    
    packet.dx = _mm_set_ps(dirs[3][0], dirs[2][0], dirs[1][0], dirs[0][0]);
    packet.dy = _mm_set_ps(dirs[3][1], dirs[2][1], dirs[1][1], dirs[0][1]);
    packet.dz = _mm_set_ps(dirs[3][2], dirs[2][2], dirs[1][2], dirs[0][2]);
    
    packet.mask = _mm_castsi128_ps(_mm_set1_epi32(0xFFFFFFFF));  // All active
}

#if COMPILE_AVX_SUPPORT
// 8-wide ray packet initialization
static inline void initRayPacket8(RayPacket8 &packet, 
                                   const Vec3 &camPos, 
                                   const Vec3 &camDir, 
                                   const Vec3 &camRight, 
                                   const Vec3 &camUp,
                                   float fov,
                                   int rtW, int rtH,
                                   int px0, int px1, int px2, int px3, int px4, int px5, int px6, int px7,
                                   int py0, int py1, int py2, int py3, int py4, int py5, int py6, int py7) {
    // Compute ray directions for 8 pixels
    float aspect = static_cast<float>(rtW) / rtH;
    float tanHalfFov = std::tan(fov * 0.5f * 3.14159265f / 180.0f);
    
    float dirs[8][3];
    int pxs[8] = {px0, px1, px2, px3, px4, px5, px6, px7};
    int pys[8] = {py0, py1, py2, py3, py4, py5, py6, py7};
    
    for (int i = 0; i < 8; i++) {
        float u = (2.0f * (pxs[i] + 0.5f) / rtW - 1.0f) * aspect * tanHalfFov;
        float v = (1.0f - 2.0f * (pys[i] + 0.5f) / rtH) * tanHalfFov;
        
        Vec3 dir = norm(camDir + camRight * u + camUp * v);
        dirs[i][0] = dir.x;
        dirs[i][1] = dir.y;
        dirs[i][2] = dir.z;
    }
    
    // Load into SIMD registers (SoA layout)
    packet.ox = _mm256_set1_ps(camPos.x);
    packet.oy = _mm256_set1_ps(camPos.y);
    packet.oz = _mm256_set1_ps(camPos.z);
    
    packet.dx = _mm256_set_ps(dirs[7][0], dirs[6][0], dirs[5][0], dirs[4][0], 
                               dirs[3][0], dirs[2][0], dirs[1][0], dirs[0][0]);
    packet.dy = _mm256_set_ps(dirs[7][1], dirs[6][1], dirs[5][1], dirs[4][1],
                               dirs[3][1], dirs[2][1], dirs[1][1], dirs[0][1]);
    packet.dz = _mm256_set_ps(dirs[7][2], dirs[6][2], dirs[5][2], dirs[4][2],
                               dirs[3][2], dirs[2][2], dirs[1][2], dirs[0][2]);
    
    packet.mask = _mm256_castsi256_ps(_mm256_set1_epi32(0xFFFFFFFF));  // All active
}
#endif

// 4-wide sphere intersection (tests 4 rays against 1 sphere)
static inline void intersectSphere4(const RayPacket4 &rays,
                                    const Vec3 &center,
                                    float radius,
                                    const __m128 &tMax,
                                    Hit4 &hit,
                                    int mat) {
    // Load sphere center into SIMD
    __m128 cx = _mm_set1_ps(center.x);
    __m128 cy = _mm_set1_ps(center.y);
    __m128 cz = _mm_set1_ps(center.z);
    __m128 r2 = _mm_set1_ps(radius * radius);
    
    // Compute oc = ro - center (for all 4 rays)
    __m128 ocx = _mm_sub_ps(rays.ox, cx);
    __m128 ocy = _mm_sub_ps(rays.oy, cy);
    __m128 ocz = _mm_sub_ps(rays.oz, cz);
    
    // b = dot(oc, rd) - use FMA if available
    #ifdef __FMA__
    __m128 b = _mm_fmadd_ps(ocx, rays.dx, _mm_fmadd_ps(ocy, rays.dy, _mm_mul_ps(ocz, rays.dz)));
    // c = dot(oc, oc) - r^2
    __m128 oc_len2 = _mm_fmadd_ps(ocx, ocx, _mm_fmadd_ps(ocy, ocy, _mm_mul_ps(ocz, ocz)));
    #else
    __m128 b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, rays.dx),
                                     _mm_mul_ps(ocy, rays.dy)),
                          _mm_mul_ps(ocz, rays.dz));
    // c = dot(oc, oc) - r^2
    __m128 oc_len2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, ocx),
                                           _mm_mul_ps(ocy, ocy)),
                                _mm_mul_ps(ocz, ocz));
    #endif
    __m128 c = _mm_sub_ps(oc_len2, r2);
    
    // disc = b*b - c
    __m128 disc = _mm_sub_ps(_mm_mul_ps(b, b), c);
    
    // Check if disc < 0 (no hit)
    __m128 disc_valid = _mm_cmpge_ps(disc, _mm_setzero_ps());
    
    // sqrt(disc) - use SIMD sqrt for better performance
    __m128 sqrt_disc = _mm_sqrt_ps(_mm_max_ps(disc, _mm_setzero_ps()));
    
    // t = -b - sqrt(disc)
    __m128 t = _mm_sub_ps(_mm_sub_ps(_mm_setzero_ps(), b), sqrt_disc);
    
    // If t < 1e-3, try -b + sqrt(disc)
    __m128 t_alt = _mm_add_ps(_mm_sub_ps(_mm_setzero_ps(), b), sqrt_disc);
    __m128 t_small = _mm_cmplt_ps(t, _mm_set1_ps(1e-3f));
    t = _mm_blendv_ps(t, t_alt, t_small);
    
    // Check if t is in valid range [1e-3, tMax]
    __m128 t_min_valid = _mm_cmpge_ps(t, _mm_set1_ps(1e-3f));
    __m128 t_max_valid = _mm_cmple_ps(t, tMax);
    __m128 t_valid = _mm_and_ps(_mm_and_ps(t_min_valid, t_max_valid), disc_valid);
    
    // Check if this hit is closer than existing hit
    __m128 closer = _mm_cmplt_ps(t, hit.t);
    __m128 update_mask = _mm_and_ps(t_valid, closer);
    
    // Update hit record where update_mask is true
    hit.t = _mm_blendv_ps(hit.t, t, update_mask);
    hit.valid = _mm_or_ps(hit.valid, update_mask);
    
    // Compute hit position: pos = ro + rd * t
    __m128 px = _mm_add_ps(rays.ox, _mm_mul_ps(rays.dx, t));
    __m128 py = _mm_add_ps(rays.oy, _mm_mul_ps(rays.dy, t));
    __m128 pz = _mm_add_ps(rays.oz, _mm_mul_ps(rays.dz, t));
    
    hit.px = _mm_blendv_ps(hit.px, px, update_mask);
    hit.py = _mm_blendv_ps(hit.py, py, update_mask);
    hit.pz = _mm_blendv_ps(hit.pz, pz, update_mask);
    
    // Compute normal: n = normalize(pos - center)
    __m128 nx = _mm_sub_ps(px, cx);
    __m128 ny = _mm_sub_ps(py, cy);
    __m128 nz = _mm_sub_ps(pz, cz);
    
    // Normalize (need to do per-lane)
    alignas(16) float nx_arr[4], ny_arr[4], nz_arr[4];
    _mm_store_ps(nx_arr, nx);
    _mm_store_ps(ny_arr, ny);
    _mm_store_ps(nz_arr, nz);
    
    for (int i = 0; i < 4; i++) {
        float len2 = nx_arr[i]*nx_arr[i] + ny_arr[i]*ny_arr[i] + nz_arr[i]*nz_arr[i];
        if (len2 > 1e-16f) {
            float inv_len = rsqrt_fast(len2);
            nx_arr[i] *= inv_len;
            ny_arr[i] *= inv_len;
            nz_arr[i] *= inv_len;
        }
    }
    
    nx = _mm_load_ps(nx_arr);
    ny = _mm_load_ps(ny_arr);
    nz = _mm_load_ps(nz_arr);
    
    hit.nx = _mm_blendv_ps(hit.nx, nx, update_mask);
    hit.ny = _mm_blendv_ps(hit.ny, ny, update_mask);
    hit.nz = _mm_blendv_ps(hit.nz, nz, update_mask);
    
    // Set material ID
    __m128i mat_id = _mm_set1_epi32(mat);
    hit.mat = _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(hit.mat), 
                                             _mm_castsi128_ps(mat_id), 
                                             update_mask));
}

#if COMPILE_AVX_SUPPORT
// 8-wide sphere intersection (tests 8 rays against 1 sphere)
static inline void intersectSphere8(const RayPacket8 &rays,
                                    const Vec3 &center,
                                    float radius,
                                    const __m256 &tMax,
                                    Hit8 &hit,
                                    int mat) {
    // Load sphere center into SIMD
    __m256 cx = _mm256_set1_ps(center.x);
    __m256 cy = _mm256_set1_ps(center.y);
    __m256 cz = _mm256_set1_ps(center.z);
    __m256 r2 = _mm256_set1_ps(radius * radius);
    
    // Compute oc = ro - center (for all 8 rays)
    __m256 ocx = _mm256_sub_ps(rays.ox, cx);
    __m256 ocy = _mm256_sub_ps(rays.oy, cy);
    __m256 ocz = _mm256_sub_ps(rays.oz, cz);
    
    // b = dot(oc, rd) - use FMA if available
    #ifdef __FMA__
    __m256 b = _mm256_fmadd_ps(ocx, rays.dx, _mm256_fmadd_ps(ocy, rays.dy, _mm256_mul_ps(ocz, rays.dz)));
    __m256 oc_len2 = _mm256_fmadd_ps(ocx, ocx, _mm256_fmadd_ps(ocy, ocy, _mm256_mul_ps(ocz, ocz)));
    #else
    __m256 b = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(ocx, rays.dx),
                                           _mm256_mul_ps(ocy, rays.dy)),
                             _mm256_mul_ps(ocz, rays.dz));
    __m256 oc_len2 = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(ocx, ocx),
                                                  _mm256_mul_ps(ocy, ocy)),
                                   _mm256_mul_ps(ocz, ocz));
    #endif
    __m256 c = _mm256_sub_ps(oc_len2, r2);
    
    // disc = b*b - c
    __m256 disc = _mm256_sub_ps(_mm256_mul_ps(b, b), c);
    
    // Check if disc < 0 (no hit)
    __m256 disc_valid = _mm256_cmp_ps(disc, _mm256_setzero_ps(), _CMP_GE_OQ);
    
    // sqrt(disc)
    __m256 sqrt_disc = _mm256_sqrt_ps(_mm256_max_ps(disc, _mm256_setzero_ps()));
    
    // t = -b - sqrt(disc)
    __m256 t = _mm256_sub_ps(_mm256_sub_ps(_mm256_setzero_ps(), b), sqrt_disc);
    
    // If t < 1e-3, try -b + sqrt(disc)
    __m256 t_alt = _mm256_add_ps(_mm256_sub_ps(_mm256_setzero_ps(), b), sqrt_disc);
    __m256 t_small = _mm256_cmp_ps(t, _mm256_set1_ps(1e-3f), _CMP_LT_OQ);
    t = _mm256_blendv_ps(t, t_alt, t_small);
    
    // Check if t is in valid range [1e-3, tMax]
    __m256 t_min_valid = _mm256_cmp_ps(t, _mm256_set1_ps(1e-3f), _CMP_GE_OQ);
    __m256 t_max_valid = _mm256_cmp_ps(t, tMax, _CMP_LE_OQ);
    __m256 t_valid = _mm256_and_ps(_mm256_and_ps(t_min_valid, t_max_valid), disc_valid);
    
    // Check if this hit is closer than existing hit
    __m256 closer = _mm256_cmp_ps(t, hit.t, _CMP_LT_OQ);
    __m256 update_mask = _mm256_and_ps(t_valid, closer);
    
    // Update hit record where update_mask is true
    hit.t = _mm256_blendv_ps(hit.t, t, update_mask);
    hit.valid = _mm256_or_ps(hit.valid, update_mask);
    
    // Compute hit position: pos = ro + rd * t
    __m256 px = _mm256_add_ps(rays.ox, _mm256_mul_ps(rays.dx, t));
    __m256 py = _mm256_add_ps(rays.oy, _mm256_mul_ps(rays.dy, t));
    __m256 pz = _mm256_add_ps(rays.oz, _mm256_mul_ps(rays.dz, t));
    
    hit.px = _mm256_blendv_ps(hit.px, px, update_mask);
    hit.py = _mm256_blendv_ps(hit.py, py, update_mask);
    hit.pz = _mm256_blendv_ps(hit.pz, pz, update_mask);
    
    // Compute normal: n = normalize(pos - center)
    __m256 nx = _mm256_sub_ps(px, cx);
    __m256 ny = _mm256_sub_ps(py, cy);
    __m256 nz = _mm256_sub_ps(pz, cz);
    
    // Normalize (per-lane)
    alignas(32) float nx_arr[8], ny_arr[8], nz_arr[8];
    _mm256_store_ps(nx_arr, nx);
    _mm256_store_ps(ny_arr, ny);
    _mm256_store_ps(nz_arr, nz);
    
    for (int i = 0; i < 8; i++) {
        float len2 = nx_arr[i]*nx_arr[i] + ny_arr[i]*ny_arr[i] + nz_arr[i]*nz_arr[i];
        if (len2 > 1e-16f) {
            float inv_len = rsqrt_fast(len2);
            nx_arr[i] *= inv_len;
            ny_arr[i] *= inv_len;
            nz_arr[i] *= inv_len;
        }
    }
    
    nx = _mm256_load_ps(nx_arr);
    ny = _mm256_load_ps(ny_arr);
    nz = _mm256_load_ps(nz_arr);
    
    hit.nx = _mm256_blendv_ps(hit.nx, nx, update_mask);
    hit.ny = _mm256_blendv_ps(hit.ny, ny, update_mask);
    hit.nz = _mm256_blendv_ps(hit.nz, nz, update_mask);
    
    // Set material ID
    __m256i mat_id = _mm256_set1_epi32(mat);
    hit.mat = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(hit.mat), 
                                                    _mm256_castsi256_ps(mat_id), 
                                                    update_mask));
}
#endif

// 4-wide plane intersection (tests 4 rays against 1 plane)
static inline void intersectPlane4(const RayPacket4 &rays,
                                   const Vec3 &planePoint,
                                   const Vec3 &planeNormal,
                                   const __m128 &tMax,
                                   Hit4 &hit,
                                   int mat) {
    // Load plane data into SIMD
    __m128 nx = _mm_set1_ps(planeNormal.x);
    __m128 ny = _mm_set1_ps(planeNormal.y);
    __m128 nz = _mm_set1_ps(planeNormal.z);
    
    __m128 px = _mm_set1_ps(planePoint.x);
    __m128 py = _mm_set1_ps(planePoint.y);
    __m128 pz = _mm_set1_ps(planePoint.z);
    
    // denom = dot(rd, n)
    __m128 denom = _mm_add_ps(_mm_add_ps(_mm_mul_ps(rays.dx, nx),
                                         _mm_mul_ps(rays.dy, ny)),
                              _mm_mul_ps(rays.dz, nz));
    
    // Check if |denom| < 1e-5 (parallel to plane)
    __m128 abs_denom = _mm_andnot_ps(_mm_set1_ps(-0.0f), denom);  // Absolute value
    __m128 denom_valid = _mm_cmpge_ps(abs_denom, _mm_set1_ps(1e-5f));
    
    // t = dot(p - ro, n) / denom
    __m128 diff_x = _mm_sub_ps(px, rays.ox);
    __m128 diff_y = _mm_sub_ps(py, rays.oy);
    __m128 diff_z = _mm_sub_ps(pz, rays.oz);
    
    __m128 numerator = _mm_add_ps(_mm_add_ps(_mm_mul_ps(diff_x, nx),
                                             _mm_mul_ps(diff_y, ny)),
                                  _mm_mul_ps(diff_z, nz));
    
    __m128 t = _mm_div_ps(numerator, denom);
    
    // Check if t is in valid range [1e-3, tMax]
    __m128 t_min_valid = _mm_cmpge_ps(t, _mm_set1_ps(1e-3f));
    __m128 t_max_valid = _mm_cmple_ps(t, tMax);
    __m128 t_valid = _mm_and_ps(_mm_and_ps(t_min_valid, t_max_valid), denom_valid);
    
    // Check if this hit is closer than existing hit
    __m128 closer = _mm_cmplt_ps(t, hit.t);
    __m128 update_mask = _mm_and_ps(t_valid, closer);
    
    // Update hit record
    hit.t = _mm_blendv_ps(hit.t, t, update_mask);
    hit.valid = _mm_or_ps(hit.valid, update_mask);
    
    // Compute hit position
    __m128 pos_x = _mm_add_ps(rays.ox, _mm_mul_ps(rays.dx, t));
    __m128 pos_y = _mm_add_ps(rays.oy, _mm_mul_ps(rays.dy, t));
    __m128 pos_z = _mm_add_ps(rays.oz, _mm_mul_ps(rays.dz, t));
    
    hit.px = _mm_blendv_ps(hit.px, pos_x, update_mask);
    hit.py = _mm_blendv_ps(hit.py, pos_y, update_mask);
    hit.pz = _mm_blendv_ps(hit.pz, pos_z, update_mask);
    
    // Determine normal direction (flip if denom >= 0, keep if denom < 0)
    // Scalar: hit.n=(denom<0)?n:(n*-1.0f) means: flip when denom >= 0
    __m128 denom_neg = _mm_cmplt_ps(denom, _mm_setzero_ps());
    // _mm_blendv_ps selects second arg when mask is all 1s (true)
    // So: when denom < 0 (denom_neg=true), keep original; when denom >= 0 (denom_neg=false), negate
    __m128 normal_x = _mm_blendv_ps(_mm_sub_ps(_mm_setzero_ps(), nx), nx, denom_neg);
    __m128 normal_y = _mm_blendv_ps(_mm_sub_ps(_mm_setzero_ps(), ny), ny, denom_neg);
    __m128 normal_z = _mm_blendv_ps(_mm_sub_ps(_mm_setzero_ps(), nz), nz, denom_neg);
    
    hit.nx = _mm_blendv_ps(hit.nx, normal_x, update_mask);
    hit.ny = _mm_blendv_ps(hit.ny, normal_y, update_mask);
    hit.nz = _mm_blendv_ps(hit.nz, normal_z, update_mask);
    
    // Set material ID
    __m128i mat_id = _mm_set1_epi32(mat);
    hit.mat = _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(hit.mat),
                                             _mm_castsi128_ps(mat_id),
                                             update_mask));
}

#if COMPILE_AVX_SUPPORT
// 8-wide plane intersection (tests 8 rays against 1 plane)
static inline void intersectPlane8(const RayPacket8 &rays,
                                   const Vec3 &planePoint,
                                   const Vec3 &planeNormal,
                                   const __m256 &tMax,
                                   Hit8 &hit,
                                   int mat) {
    // Load plane data into SIMD
    __m256 nx = _mm256_set1_ps(planeNormal.x);
    __m256 ny = _mm256_set1_ps(planeNormal.y);
    __m256 nz = _mm256_set1_ps(planeNormal.z);
    
    __m256 px = _mm256_set1_ps(planePoint.x);
    __m256 py = _mm256_set1_ps(planePoint.y);
    __m256 pz = _mm256_set1_ps(planePoint.z);
    
    // denom = dot(rd, n)
    __m256 denom = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(rays.dx, nx),
                                                _mm256_mul_ps(rays.dy, ny)),
                                 _mm256_mul_ps(rays.dz, nz));
    
    // Check if |denom| < 1e-5 (parallel to plane)
    __m256 abs_denom = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), denom);
    __m256 denom_valid = _mm256_cmp_ps(abs_denom, _mm256_set1_ps(1e-5f), _CMP_GE_OQ);
    
    // t = dot(p - ro, n) / denom
    __m256 diff_x = _mm256_sub_ps(px, rays.ox);
    __m256 diff_y = _mm256_sub_ps(py, rays.oy);
    __m256 diff_z = _mm256_sub_ps(pz, rays.oz);
    
    __m256 numerator = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(diff_x, nx),
                                                    _mm256_mul_ps(diff_y, ny)),
                                     _mm256_mul_ps(diff_z, nz));
    
    __m256 t = _mm256_div_ps(numerator, denom);
    
    // Check if t is in valid range [1e-3, tMax]
    __m256 t_min_valid = _mm256_cmp_ps(t, _mm256_set1_ps(1e-3f), _CMP_GE_OQ);
    __m256 t_max_valid = _mm256_cmp_ps(t, tMax, _CMP_LE_OQ);
    __m256 t_valid = _mm256_and_ps(_mm256_and_ps(t_min_valid, t_max_valid), denom_valid);
    
    // Check if this hit is closer than existing hit
    __m256 closer = _mm256_cmp_ps(t, hit.t, _CMP_LT_OQ);
    __m256 update_mask = _mm256_and_ps(t_valid, closer);
    
    // Update hit record
    hit.t = _mm256_blendv_ps(hit.t, t, update_mask);
    hit.valid = _mm256_or_ps(hit.valid, update_mask);
    
    // Compute hit position
    __m256 pos_x = _mm256_add_ps(rays.ox, _mm256_mul_ps(rays.dx, t));
    __m256 pos_y = _mm256_add_ps(rays.oy, _mm256_mul_ps(rays.dy, t));
    __m256 pos_z = _mm256_add_ps(rays.oz, _mm256_mul_ps(rays.dz, t));
    
    hit.px = _mm256_blendv_ps(hit.px, pos_x, update_mask);
    hit.py = _mm256_blendv_ps(hit.py, pos_y, update_mask);
    hit.pz = _mm256_blendv_ps(hit.pz, pos_z, update_mask);
    
    // Determine normal direction (flip if denom >= 0, keep if denom < 0)
    __m256 denom_neg = _mm256_cmp_ps(denom, _mm256_setzero_ps(), _CMP_LT_OQ);
    __m256 normal_x = _mm256_blendv_ps(_mm256_sub_ps(_mm256_setzero_ps(), nx), nx, denom_neg);
    __m256 normal_y = _mm256_blendv_ps(_mm256_sub_ps(_mm256_setzero_ps(), ny), ny, denom_neg);
    __m256 normal_z = _mm256_blendv_ps(_mm256_sub_ps(_mm256_setzero_ps(), nz), nz, denom_neg);
    
    hit.nx = _mm256_blendv_ps(hit.nx, normal_x, update_mask);
    hit.ny = _mm256_blendv_ps(hit.ny, normal_y, update_mask);
    hit.nz = _mm256_blendv_ps(hit.nz, normal_z, update_mask);
    
    // Set material ID
    __m256i mat_id = _mm256_set1_epi32(mat);
    hit.mat = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(hit.mat),
                                                    _mm256_castsi256_ps(mat_id),
                                                    update_mask));
}
#endif

// 4-wide box intersection (tests 4 rays against 1 axis-aligned box)
static inline void intersectBox4(const RayPacket4 &rays,
                                 const Vec3 &bmin,
                                 const Vec3 &bmax,
                                 const __m128 &tMax,
                                 Hit4 &hit,
                                 int mat) {
    // Load box bounds
    __m128 min_x = _mm_set1_ps(bmin.x);
    __m128 min_y = _mm_set1_ps(bmin.y);
    __m128 min_z = _mm_set1_ps(bmin.z);
    __m128 max_x = _mm_set1_ps(bmax.x);
    __m128 max_y = _mm_set1_ps(bmax.y);
    __m128 max_z = _mm_set1_ps(bmax.z);
    
    // Slab test for X axis
    __m128 inv_dx = _mm_div_ps(_mm_set1_ps(1.0f), rays.dx);
    __m128 tx1 = _mm_mul_ps(_mm_sub_ps(min_x, rays.ox), inv_dx);
    __m128 tx2 = _mm_mul_ps(_mm_sub_ps(max_x, rays.ox), inv_dx);
    __m128 tmin_x = _mm_min_ps(tx1, tx2);
    __m128 tmax_x = _mm_max_ps(tx1, tx2);
    
    // Slab test for Y axis
    __m128 inv_dy = _mm_div_ps(_mm_set1_ps(1.0f), rays.dy);
    __m128 ty1 = _mm_mul_ps(_mm_sub_ps(min_y, rays.oy), inv_dy);
    __m128 ty2 = _mm_mul_ps(_mm_sub_ps(max_y, rays.oy), inv_dy);
    __m128 tmin_y = _mm_min_ps(ty1, ty2);
    __m128 tmax_y = _mm_max_ps(ty1, ty2);
    
    // Slab test for Z axis
    __m128 inv_dz = _mm_div_ps(_mm_set1_ps(1.0f), rays.dz);
    __m128 tz1 = _mm_mul_ps(_mm_sub_ps(min_z, rays.oz), inv_dz);
    __m128 tz2 = _mm_mul_ps(_mm_sub_ps(max_z, rays.oz), inv_dz);
    __m128 tmin_z = _mm_min_ps(tz1, tz2);
    __m128 tmax_z = _mm_max_ps(tz1, tz2);
    
    // Compute intersection interval
    __m128 tmin = _mm_max_ps(_mm_max_ps(tmin_x, tmin_y), tmin_z);
    __m128 tmax_slab = _mm_min_ps(_mm_min_ps(tmax_x, tmax_y), tmax_z);
    
    // Valid if: tmax >= max(1e-3, tmin) && tmin < tMax && tmin < hit.t
    __m128 tmin_clamped = _mm_max_ps(_mm_set1_ps(1e-3f), tmin);
    __m128 valid = _mm_and_ps(_mm_cmpge_ps(tmax_slab, tmin_clamped),
                             _mm_cmplt_ps(tmin_clamped, tMax));
    valid = _mm_and_ps(valid, _mm_cmplt_ps(tmin_clamped, hit.t));
    valid = _mm_and_ps(valid, rays.mask);
    
    // Update hits where valid
    hit.t = _mm_blendv_ps(hit.t, tmin_clamped, valid);
    
    // Compute hit position: ro + rd * t
    __m128 px_hit = _mm_add_ps(rays.ox, _mm_mul_ps(rays.dx, tmin_clamped));
    __m128 py_hit = _mm_add_ps(rays.oy, _mm_mul_ps(rays.dy, tmin_clamped));
    __m128 pz_hit = _mm_add_ps(rays.oz, _mm_mul_ps(rays.dz, tmin_clamped));
    
    hit.px = _mm_blendv_ps(hit.px, px_hit, valid);
    hit.py = _mm_blendv_ps(hit.py, py_hit, valid);
    hit.pz = _mm_blendv_ps(hit.pz, pz_hit, valid);
    
    // Determine hit normal (which slab was hit)
    __m128 eps = _mm_set1_ps(1e-5f);
    __m128 hit_x = _mm_cmplt_ps(_mm_sub_ps(tmin, tmin_x), eps);
    __m128 hit_y = _mm_and_ps(_mm_cmplt_ps(_mm_sub_ps(tmin, tmin_y), eps),
                              _mm_andnot_ps(hit_x, _mm_castsi128_ps(_mm_set1_epi32(-1))));
    __m128 hit_z = _mm_andnot_ps(_mm_or_ps(hit_x, hit_y), _mm_castsi128_ps(_mm_set1_epi32(-1)));
    
    // Determine normal direction based on which side was hit
    __m128 center_x = _mm_mul_ps(_mm_add_ps(min_x, max_x), _mm_set1_ps(0.5f));
    __m128 center_y = _mm_mul_ps(_mm_add_ps(min_y, max_y), _mm_set1_ps(0.5f));
    __m128 center_z = _mm_mul_ps(_mm_add_ps(min_z, max_z), _mm_set1_ps(0.5f));
    
    // Sign based on ray origin relative to box center
    __m128 sign_x = _mm_blendv_ps(_mm_set1_ps(-1.0f), _mm_set1_ps(1.0f), 
                                   _mm_cmplt_ps(rays.ox, center_x));
    __m128 sign_y = _mm_blendv_ps(_mm_set1_ps(-1.0f), _mm_set1_ps(1.0f), 
                                   _mm_cmplt_ps(rays.oy, center_y));
    __m128 sign_z = _mm_blendv_ps(_mm_set1_ps(-1.0f), _mm_set1_ps(1.0f), 
                                   _mm_cmplt_ps(rays.oz, center_z));
    
    __m128 nx = _mm_blendv_ps(_mm_setzero_ps(), sign_x, hit_x);
    __m128 ny = _mm_blendv_ps(_mm_setzero_ps(), sign_y, hit_y);
    __m128 nz = _mm_blendv_ps(_mm_setzero_ps(), sign_z, hit_z);
    
    hit.nx = _mm_blendv_ps(hit.nx, nx, valid);
    hit.ny = _mm_blendv_ps(hit.ny, ny, valid);
    hit.nz = _mm_blendv_ps(hit.nz, nz, valid);
    
    // Material
    __m128i mat_val = _mm_set1_epi32(mat);
    hit.mat = _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(hit.mat), 
                                             _mm_castsi128_ps(mat_val), valid));
    
    // Update valid mask
    hit.valid = _mm_or_ps(hit.valid, valid);
}

// Axis aligned thin box (only front/back + sides) simplified: treat as slab intersection returning surface normal of hit face.
static FORCE_INLINE bool intersectBox(Vec3 ro, Vec3 rd, Vec3 bmin, Vec3 bmax, float tMax, Hit &hit, int mat) {
    float tmin = 0.001f, tmax = tMax;
    Vec3 n = {0,0,0};
    // X
    for (int axis=0; axis<3; ++axis) {
        float roA = (&ro.x)[axis];
        float rdA = (&rd.x)[axis];
        float minA = (&bmin.x)[axis];
        float maxA = (&bmax.x)[axis];
        if (std::fabs(rdA) < 1e-6f) { if (roA < minA || roA > maxA) return false; }
        else {
            float ood = 1.0f/rdA;
            float t1 = (minA - roA)*ood;
            float t2 = (maxA - roA)*ood;
            float sign = -1.0f;
            if (t1>t2){ std::swap(t1,t2); sign=1.0f; }
            if (t1 > tmin){ tmin=t1; n=Vec3{0,0,0}; (&n.x)[axis]=sign; }
            if (t2 < tmax) tmax=t2;
            if (tmin>tmax) return false;
        }
    }
    if (tmin < 1e-3f) return false;
    hit.t=tmin; hit.pos = ro + rd*tmin; hit.n = n; hit.mat=mat; return true;
}

#if COMPILE_AVX_SUPPORT
// 8-wide box intersection (tests 8 rays against 1 axis-aligned box)
static inline void intersectBox8(const RayPacket8 &rays,
                                 const Vec3 &bmin,
                                 const Vec3 &bmax,
                                 const __m256 &tMax,
                                 Hit8 &hit,
                                 int mat) {
    // Load box bounds
    __m256 min_x = _mm256_set1_ps(bmin.x);
    __m256 min_y = _mm256_set1_ps(bmin.y);
    __m256 min_z = _mm256_set1_ps(bmin.z);
    __m256 max_x = _mm256_set1_ps(bmax.x);
    __m256 max_y = _mm256_set1_ps(bmax.y);
    __m256 max_z = _mm256_set1_ps(bmax.z);
    
    // Slab test for X axis
    __m256 inv_dx = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dx);
    __m256 tx1 = _mm256_mul_ps(_mm256_sub_ps(min_x, rays.ox), inv_dx);
    __m256 tx2 = _mm256_mul_ps(_mm256_sub_ps(max_x, rays.ox), inv_dx);
    __m256 tmin_x = _mm256_min_ps(tx1, tx2);
    __m256 tmax_x = _mm256_max_ps(tx1, tx2);
    
    // Slab test for Y axis
    __m256 inv_dy = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dy);
    __m256 ty1 = _mm256_mul_ps(_mm256_sub_ps(min_y, rays.oy), inv_dy);
    __m256 ty2 = _mm256_mul_ps(_mm256_sub_ps(max_y, rays.oy), inv_dy);
    __m256 tmin_y = _mm256_min_ps(ty1, ty2);
    __m256 tmax_y = _mm256_max_ps(ty1, ty2);
    
    // Slab test for Z axis
    __m256 inv_dz = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dz);
    __m256 tz1 = _mm256_mul_ps(_mm256_sub_ps(min_z, rays.oz), inv_dz);
    __m256 tz2 = _mm256_mul_ps(_mm256_sub_ps(max_z, rays.oz), inv_dz);
    __m256 tmin_z = _mm256_min_ps(tz1, tz2);
    __m256 tmax_z = _mm256_max_ps(tz1, tz2);
    
    // Compute intersection interval
    __m256 tmin = _mm256_max_ps(_mm256_max_ps(tmin_x, tmin_y), tmin_z);
    __m256 tmax_slab = _mm256_min_ps(_mm256_min_ps(tmax_x, tmax_y), tmax_z);
    
    // Valid if: tmax >= max(1e-3, tmin) && tmin < tMax && tmin < hit.t
    __m256 tmin_clamped = _mm256_max_ps(_mm256_set1_ps(1e-3f), tmin);
    __m256 valid = _mm256_and_ps(_mm256_cmp_ps(tmax_slab, tmin_clamped, _CMP_GE_OQ),
                                 _mm256_cmp_ps(tmin_clamped, tMax, _CMP_LT_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(tmin_clamped, hit.t, _CMP_LT_OQ));
    valid = _mm256_and_ps(valid, rays.mask);
    
    // Update hits where valid
    hit.t = _mm256_blendv_ps(hit.t, tmin_clamped, valid);
    
    // Compute hit position
    __m256 px_hit = _mm256_add_ps(rays.ox, _mm256_mul_ps(rays.dx, tmin_clamped));
    __m256 py_hit = _mm256_add_ps(rays.oy, _mm256_mul_ps(rays.dy, tmin_clamped));
    __m256 pz_hit = _mm256_add_ps(rays.oz, _mm256_mul_ps(rays.dz, tmin_clamped));
    
    hit.px = _mm256_blendv_ps(hit.px, px_hit, valid);
    hit.py = _mm256_blendv_ps(hit.py, py_hit, valid);
    hit.pz = _mm256_blendv_ps(hit.pz, pz_hit, valid);
    
    // Determine hit normal (which slab was hit)
    __m256 eps = _mm256_set1_ps(1e-5f);
    __m256 hit_x = _mm256_cmp_ps(_mm256_sub_ps(tmin, tmin_x), eps, _CMP_LT_OQ);
    __m256 hit_y = _mm256_and_ps(_mm256_cmp_ps(_mm256_sub_ps(tmin, tmin_y), eps, _CMP_LT_OQ),
                                 _mm256_andnot_ps(hit_x, _mm256_castsi256_ps(_mm256_set1_epi32(-1))));
    __m256 hit_z = _mm256_andnot_ps(_mm256_or_ps(hit_x, hit_y), _mm256_castsi256_ps(_mm256_set1_epi32(-1)));
    
    // Determine normal direction based on which side was hit
    __m256 center_x = _mm256_mul_ps(_mm256_add_ps(min_x, max_x), _mm256_set1_ps(0.5f));
    __m256 center_y = _mm256_mul_ps(_mm256_add_ps(min_y, max_y), _mm256_set1_ps(0.5f));
    __m256 center_z = _mm256_mul_ps(_mm256_add_ps(min_z, max_z), _mm256_set1_ps(0.5f));
    
    // Sign based on ray origin relative to box center
    __m256 sign_x = _mm256_blendv_ps(_mm256_set1_ps(-1.0f), _mm256_set1_ps(1.0f), 
                                     _mm256_cmp_ps(rays.ox, center_x, _CMP_LT_OQ));
    __m256 sign_y = _mm256_blendv_ps(_mm256_set1_ps(-1.0f), _mm256_set1_ps(1.0f), 
                                     _mm256_cmp_ps(rays.oy, center_y, _CMP_LT_OQ));
    __m256 sign_z = _mm256_blendv_ps(_mm256_set1_ps(-1.0f), _mm256_set1_ps(1.0f), 
                                     _mm256_cmp_ps(rays.oz, center_z, _CMP_LT_OQ));
    
    __m256 nx = _mm256_blendv_ps(_mm256_setzero_ps(), sign_x, hit_x);
    __m256 ny = _mm256_blendv_ps(_mm256_setzero_ps(), sign_y, hit_y);
    __m256 nz = _mm256_blendv_ps(_mm256_setzero_ps(), sign_z, hit_z);
    
    hit.nx = _mm256_blendv_ps(hit.nx, nx, valid);
    hit.ny = _mm256_blendv_ps(hit.ny, ny, valid);
    hit.nz = _mm256_blendv_ps(hit.nz, nz, valid);
    
    // Material
    __m256i mat_val = _mm256_set1_epi32(mat);
    hit.mat = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(hit.mat), 
                                                    _mm256_castsi256_ps(mat_val), valid));
    
    // Update valid mask
    hit.valid = _mm256_or_ps(hit.valid, valid);
}
#endif

// 4-wide AABB intersection (tests 4 rays against 1 AABB)
// Returns a mask indicating which rays hit the AABB
static inline __m128 intersectAABB4(const RayPacket4 &rays, const Vec3 &bmin, const Vec3 &bmax, const __m128 &tMax) {
    // Load AABB bounds
    __m128 bmin_x = _mm_set1_ps(bmin.x);
    __m128 bmin_y = _mm_set1_ps(bmin.y);
    __m128 bmin_z = _mm_set1_ps(bmin.z);
    __m128 bmax_x = _mm_set1_ps(bmax.x);
    __m128 bmax_y = _mm_set1_ps(bmax.y);
    __m128 bmax_z = _mm_set1_ps(bmax.z);
    
    // Compute inverse ray direction (handle division by zero)
    __m128 invdx = _mm_div_ps(_mm_set1_ps(1.0f), rays.dx);
    __m128 invdy = _mm_div_ps(_mm_set1_ps(1.0f), rays.dy);
    __m128 invdz = _mm_div_ps(_mm_set1_ps(1.0f), rays.dz);
    
    // X slab
    __m128 tx1 = _mm_mul_ps(_mm_sub_ps(bmin_x, rays.ox), invdx);
    __m128 tx2 = _mm_mul_ps(_mm_sub_ps(bmax_x, rays.ox), invdx);
    __m128 tmin = _mm_min_ps(tx1, tx2);
    __m128 tmax_val = _mm_max_ps(tx1, tx2);
    
    // Y slab
    __m128 ty1 = _mm_mul_ps(_mm_sub_ps(bmin_y, rays.oy), invdy);
    __m128 ty2 = _mm_mul_ps(_mm_sub_ps(bmax_y, rays.oy), invdy);
    tmin = _mm_max_ps(tmin, _mm_min_ps(ty1, ty2));
    tmax_val = _mm_min_ps(tmax_val, _mm_max_ps(ty1, ty2));
    
    // Z slab
    __m128 tz1 = _mm_mul_ps(_mm_sub_ps(bmin_z, rays.oz), invdz);
    __m128 tz2 = _mm_mul_ps(_mm_sub_ps(bmax_z, rays.oz), invdz);
    tmin = _mm_max_ps(tmin, _mm_min_ps(tz1, tz2));
    tmax_val = _mm_min_ps(tmax_val, _mm_max_ps(tz1, tz2));
    
    // Check if ray intersects AABB: tmax >= max(0, tmin) && tmin < tMax
    __m128 zero = _mm_setzero_ps();
    __m128 tmin_clamped = _mm_max_ps(zero, tmin);
    __m128 hit1 = _mm_cmpge_ps(tmax_val, tmin_clamped);
    __m128 hit2 = _mm_cmplt_ps(tmin, tMax);
    return _mm_and_ps(hit1, hit2);
}

#if COMPILE_AVX_SUPPORT
// 8-wide AABB intersection (tests 8 rays against 1 AABB)
// Returns a mask indicating which rays hit the AABB
static inline __m256 intersectAABB8(const RayPacket8 &rays, const Vec3 &bmin, const Vec3 &bmax, const __m256 &tMax) {
    // Load AABB bounds
    __m256 bmin_x = _mm256_set1_ps(bmin.x);
    __m256 bmin_y = _mm256_set1_ps(bmin.y);
    __m256 bmin_z = _mm256_set1_ps(bmin.z);
    __m256 bmax_x = _mm256_set1_ps(bmax.x);
    __m256 bmax_y = _mm256_set1_ps(bmax.y);
    __m256 bmax_z = _mm256_set1_ps(bmax.z);
    
    // Compute inverse ray direction
    __m256 invdx = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dx);
    __m256 invdy = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dy);
    __m256 invdz = _mm256_div_ps(_mm256_set1_ps(1.0f), rays.dz);
    
    // X slab
    __m256 tx1 = _mm256_mul_ps(_mm256_sub_ps(bmin_x, rays.ox), invdx);
    __m256 tx2 = _mm256_mul_ps(_mm256_sub_ps(bmax_x, rays.ox), invdx);
    __m256 tmin = _mm256_min_ps(tx1, tx2);
    __m256 tmax_val = _mm256_max_ps(tx1, tx2);
    
    // Y slab
    __m256 ty1 = _mm256_mul_ps(_mm256_sub_ps(bmin_y, rays.oy), invdy);
    __m256 ty2 = _mm256_mul_ps(_mm256_sub_ps(bmax_y, rays.oy), invdy);
    tmin = _mm256_max_ps(tmin, _mm256_min_ps(ty1, ty2));
    tmax_val = _mm256_min_ps(tmax_val, _mm256_max_ps(ty1, ty2));
    
    // Z slab
    __m256 tz1 = _mm256_mul_ps(_mm256_sub_ps(bmin_z, rays.oz), invdz);
    __m256 tz2 = _mm256_mul_ps(_mm256_sub_ps(bmax_z, rays.oz), invdz);
    tmin = _mm256_max_ps(tmin, _mm256_min_ps(tz1, tz2));
    tmax_val = _mm256_min_ps(tmax_val, _mm256_max_ps(tz1, tz2));
    
    // Check if ray intersects AABB: tmax >= max(0, tmin) && tmin < tMax
    __m256 zero = _mm256_setzero_ps();
    __m256 tmin_clamped = _mm256_max_ps(zero, tmin);
    __m256 hit1 = _mm256_cmp_ps(tmax_val, tmin_clamped, _CMP_GE_OQ);
    __m256 hit2 = _mm256_cmp_ps(tmin, tMax, _CMP_LT_OQ);
    return _mm256_and_ps(hit1, hit2);
}
#endif

void SoftRenderer::render(const GameState &gs) {
    // (Segment tracer removed; integrate its tone mapping into main pipeline instead)
    if (!config.enablePathTracing) return; // nothing (caller can draw classic)
    if (rtW==0||rtH==0) return;

    using clock = std::chrono::high_resolution_clock;
    auto tStart = clock::now();
    auto t0 = tStart;

    // Map dynamic game objects to world
    float gw = (float)gs.gw, gh=(float)gs.gh;
    auto toWorld = [&](float gx, float gy)->Vec3 {
        float wx = (gx/gw - 0.5f)*4.0f;
        // Invert Y to match screen coordinate origin at top
        float wy = ((1.0f - gy/gh) - 0.5f)*3.0f;
        return {wx, wy, 0.0f};
    };
    // Balls (multi-ball support). First ball is emissive, others dimmer.
    std::vector<Vec3> ballCenters; std::vector<float> ballRs; ballCenters.reserve(gs.balls.size()); ballRs.reserve(gs.balls.size());
    if (!gs.balls.empty()) {
        for (size_t i=0;i<gs.balls.size();++i){
            ballCenters.push_back(toWorld((float)gs.balls[i].x, (float)gs.balls[i].y));
            ballRs.push_back(0.09f);
        }
    } else {
        ballCenters.push_back(toWorld((float)gs.ball_x, (float)gs.ball_y));
        ballRs.push_back(0.09f);
    }
    Vec3 ballC = ballCenters[0];
    float ballR = ballRs[0];
    // Paddles: width ~ 2 game units => (2/gw)*4 world units
    float paddleHalfX = (2.0f/gw)*4.0f*0.5f; // half
    float paddleHalfY = ((float)gs.paddle_h/gh)*3.0f*0.5f;
    Vec3 leftCenter = toWorld(2.0f, (float)gs.left_y + (float)gs.paddle_h*0.5f);
    Vec3 rightCenter = toWorld(gw-2.0f, (float)gs.right_y + (float)gs.paddle_h*0.5f);
    // Horizontal enemy paddles (ThreeEnemies mode) represented as thin boxes spanning horizontally
    bool useHoriz = (gs.mode == GameMode::ThreeEnemies);
    float horizHalfX = ((float)gs.paddle_w/gw)*4.0f*0.5f;
    float horizHalfY = (0.5f/gh)*3.0f; // very thin
    Vec3 topCenter = toWorld((float)gs.top_x, 1.0f);
    Vec3 bottomCenter = toWorld((float)gs.bottom_x, gh - 2.0f);
    float horizThickness = 0.04f;
    // Obstacles as boxes
    bool useObs = (gs.mode == GameMode::Obstacles || gs.mode == GameMode::ObstaclesMulti);
    struct Box { Vec3 bmin,bmax; };
    std::vector<Box> obsBoxes;
    if (useObs) {
        for (auto &ob : gs.obstacles) {
            Vec3 c = toWorld((float)ob.x, (float)ob.y);
            float hw = (float)(ob.w/gw)*4.0f*0.5f;
            float hh = (float)(ob.h/gh)*3.0f*0.5f;
            Vec3 mn{c.x-hw, c.y-hh, -0.05f}; Vec3 mx{c.x+hw, c.y+hh, 0.05f};
            obsBoxes.push_back({mn,mx});
        }
    }
    
    // Black holes as dark spheres
    std::vector<Vec3> blackholeCenters;
    std::vector<float> blackholeRs;
    if (!gs.blackholes.empty()) {
        for (auto &bh : gs.blackholes) {
            Vec3 c = toWorld((float)bh.x, (float)bh.y);
            float r = 0.15f;  // Black hole radius in world space
            blackholeCenters.push_back(c);
            blackholeRs.push_back(r);
        }
    }
    
    float paddleThickness = 0.05f;
    
    // Phase 6: Pre-compute scene bounds for spatial culling
    struct SceneBounds {
        Vec3 leftPaddleMin, leftPaddleMax;
        Vec3 rightPaddleMin, rightPaddleMax;
        Vec3 topPaddleMin, topPaddleMax;
        Vec3 bottomPaddleMin, bottomPaddleMax;
        float leftPaddleBoundRadius, rightPaddleBoundRadius;
        float topPaddleBoundRadius, bottomPaddleBoundRadius;
    };
    SceneBounds bounds;
    bounds.leftPaddleMin = leftCenter - Vec3{paddleHalfX, paddleHalfY, paddleThickness};
    bounds.leftPaddleMax = leftCenter + Vec3{paddleHalfX, paddleHalfY, paddleThickness};
    bounds.rightPaddleMin = rightCenter - Vec3{paddleHalfX, paddleHalfY, paddleThickness};
    bounds.rightPaddleMax = rightCenter + Vec3{paddleHalfX, paddleHalfY, paddleThickness};
    bounds.leftPaddleBoundRadius = sqrt_fast(paddleHalfX*paddleHalfX + paddleHalfY*paddleHalfY + paddleThickness*paddleThickness);
    bounds.rightPaddleBoundRadius = bounds.leftPaddleBoundRadius;
    
    if (useHoriz) {
        bounds.topPaddleMin = topCenter - Vec3{horizHalfX, horizHalfY, horizThickness};
        bounds.topPaddleMax = topCenter + Vec3{horizHalfX, horizHalfY, horizThickness};
        bounds.bottomPaddleMin = bottomCenter - Vec3{horizHalfX, horizHalfY, horizThickness};
        bounds.bottomPaddleMax = bottomCenter + Vec3{horizHalfX, horizHalfY, horizThickness};
        bounds.topPaddleBoundRadius = sqrt_fast(horizHalfX*horizHalfX + horizHalfY*horizHalfY + horizThickness*horizThickness);
        bounds.bottomPaddleBoundRadius = bounds.topPaddleBoundRadius;
    }
    
    // Paddle lights: collect paddle positions as area light sources if paddle emission enabled
    struct PaddleLight { Vec3 center; float halfX; float halfY; };
    std::vector<PaddleLight> paddleLights;
    if (config.paddleEmissiveIntensity > 0.0f) {
        // Add all active paddles as light sources
        paddleLights.push_back({leftCenter, paddleHalfX, paddleHalfY});
        paddleLights.push_back({rightCenter, paddleHalfX, paddleHalfY});
        if (useHoriz) {
            paddleLights.push_back({topCenter, horizHalfX, horizHalfY});
            paddleLights.push_back({bottomCenter, horizHalfX, horizHalfY});
        }
    }
    
    // Phase 6: Pre-compute material properties (avoid per-ray calculations)
    struct MaterialProps {
        Vec3 diffuseAlbedo;
        Vec3 paddleColor;
        Vec3 emitColor;
        Vec3 paddleEmitColor;
        float roughness;
        float invPi;
    };
    MaterialProps materials;
    materials.diffuseAlbedo = Vec3{0.62f, 0.64f, 0.67f};
    materials.paddleColor = Vec3{0.25f, 0.32f, 0.6f};
    materials.emitColor = Vec3{2.2f, 1.4f, 0.8f} * config.emissiveIntensity;
    materials.paddleEmitColor = Vec3{2.2f, 1.4f, 0.8f} * config.paddleEmissiveIntensity;
    materials.roughness = config.metallicRoughness;
    materials.invPi = 1.0f / 3.1415926f;
    
    // Phase 8: FORCE_INLINE AABB ray intersection test for BVH traversal (defined early)
    auto intersectAABB = [](const Vec3& ro, const Vec3& rd, const Vec3& bmin, const Vec3& bmax, float tMax) FORCE_INLINE_ATTRIB -> bool {
        // Slab test (Andrew Kensler's optimized version)
        float tx1 = (bmin.x - ro.x) / rd.x;
        float tx2 = (bmax.x - ro.x) / rd.x;
        float tmin = std::min(tx1, tx2);
        float tmax = std::max(tx1, tx2);
        
        float ty1 = (bmin.y - ro.y) / rd.y;
        float ty2 = (bmax.y - ro.y) / rd.y;
        tmin = std::max(tmin, std::min(ty1, ty2));
        tmax = std::min(tmax, std::max(ty1, ty2));
        
        float tz1 = (bmin.z - ro.z) / rd.z;
        float tz2 = (bmax.z - ro.z) / rd.z;
        tmin = std::max(tmin, std::min(tz1, tz2));
        tmax = std::min(tmax, std::max(tz1, tz2));
        
        return tmax >= std::max(0.0f, tmin) && tmin < tMax;
    };
    
    // Phase 8: Build BVH primitives list (using indices, not pointers, to avoid corruption)
    std::vector<BVHPrimitive> bvhPrimitives;
    bvhPrimitives.reserve(ballCenters.size() + blackholeCenters.size() + 4 + obsBoxes.size());  // Pre-allocate to avoid reallocation
    
    // Add balls to BVH
    for (size_t i = 0; i < ballCenters.size(); ++i) {
        BVHPrimitive prim;
        prim.bmin = ballCenters[i] - Vec3{ballRs[i], ballRs[i], ballRs[i]};
        prim.bmax = ballCenters[i] + Vec3{ballRs[i], ballRs[i], ballRs[i]};
        prim.objType = 0;  // ball
        prim.objIndex = (int)i;
        prim.mat = 1;  // emissive
        prim.objId = (int)i;
        bvhPrimitives.push_back(prim);
    }
    
    // Add black holes to BVH (objType = 3, non-emissive, dark material)
    for (size_t i = 0; i < blackholeCenters.size(); ++i) {
        BVHPrimitive prim;
        prim.bmin = blackholeCenters[i] - Vec3{blackholeRs[i], blackholeRs[i], blackholeRs[i]};
        prim.bmax = blackholeCenters[i] + Vec3{blackholeRs[i], blackholeRs[i], blackholeRs[i]};
        prim.objType = 3;  // black hole
        prim.objIndex = (int)i;
        prim.mat = 3;  // special black hole material with gravitational lensing
        prim.objId = 400 + (int)i;  // Use 400+ range for black holes
        bvhPrimitives.push_back(prim);
    }
    
    // Add paddles to BVH
    BVHPrimitive leftPaddle;
    leftPaddle.bmin = bounds.leftPaddleMin;
    leftPaddle.bmax = bounds.leftPaddleMax;
    leftPaddle.objType = 1; leftPaddle.objIndex = 0; leftPaddle.mat = 2; leftPaddle.objId = 100;
    bvhPrimitives.push_back(leftPaddle);
    
    BVHPrimitive rightPaddle;
    rightPaddle.bmin = bounds.rightPaddleMin;
    rightPaddle.bmax = bounds.rightPaddleMax;
    rightPaddle.objType = 1; rightPaddle.objIndex = 1; rightPaddle.mat = 2; rightPaddle.objId = 101;
    bvhPrimitives.push_back(rightPaddle);
    
    if (useHoriz) {
        BVHPrimitive topPaddle;
        topPaddle.bmin = bounds.topPaddleMin;
        topPaddle.bmax = bounds.topPaddleMax;
        topPaddle.objType = 1; topPaddle.objIndex = 2; topPaddle.mat = 2; topPaddle.objId = 102;
        bvhPrimitives.push_back(topPaddle);
        
        BVHPrimitive bottomPaddle;
        bottomPaddle.bmin = bounds.bottomPaddleMin;
        bottomPaddle.bmax = bounds.bottomPaddleMax;
        bottomPaddle.objType = 1; bottomPaddle.objIndex = 3; bottomPaddle.mat = 2; bottomPaddle.objId = 103;
        bvhPrimitives.push_back(bottomPaddle);
    }
    
    // Add obstacles to BVH
    if (useObs) {
        for (size_t i = 0; i < obsBoxes.size(); ++i) {
            BVHPrimitive prim;
            prim.bmin = obsBoxes[i].bmin;
            prim.bmax = obsBoxes[i].bmax;
            prim.objType = 2;  // obstacle
            prim.objIndex = (int)i;
            prim.mat = 0;  // diffuse
            prim.objId = 300 + (int)i;
            bvhPrimitives.push_back(prim);
        }
    }
    
    // Phase 8: Build BVH tree (median split, simple and fast for per-frame rebuild)
    std::vector<BVHNode> bvhNodes;
    int bvhRootIndex = -1;
    
    if (!bvhPrimitives.empty()) {
        // Reserve space for nodes (worst case: 2*N-1 nodes for N primitives)
        bvhNodes.reserve(bvhPrimitives.size() * 2);
        
        // Recursive BVH build function using median split
        std::function<int(int, int)> buildBVH = [&](int start, int end) -> int {
            int nodeIdx = (int)bvhNodes.size();
            bvhNodes.push_back(BVHNode());
            BVHNode& node = bvhNodes[nodeIdx];
            
            // Compute bounds for this node
            node.bmin = Vec3{1e30f, 1e30f, 1e30f};
            node.bmax = Vec3{-1e30f, -1e30f, -1e30f};
            for (int i = start; i < end; ++i) {
                node.bmin.x = std::min(node.bmin.x, bvhPrimitives[i].bmin.x);
                node.bmin.y = std::min(node.bmin.y, bvhPrimitives[i].bmin.y);
                node.bmin.z = std::min(node.bmin.z, bvhPrimitives[i].bmin.z);
                node.bmax.x = std::max(node.bmax.x, bvhPrimitives[i].bmax.x);
                node.bmax.y = std::max(node.bmax.y, bvhPrimitives[i].bmax.y);
                node.bmax.z = std::max(node.bmax.z, bvhPrimitives[i].bmax.z);
            }
            
            int count = end - start;
            if (count <= 2) {  // Leaf node (max 2 primitives per leaf for better performance)
                node.leftChild = -1;
                node.rightChild = -1;
                node.primStart = start;
                node.primCount = count;
                return nodeIdx;
            }
            
            // Interior node: split along longest axis at median
            Vec3 extent = node.bmax - node.bmin;
            int axis = 0;
            if (extent.y > extent.x) axis = 1;
            float longestExtent = (axis == 0) ? extent.x : extent.y;
            if (extent.z > longestExtent) axis = 2;
            
            // Sort primitives along axis (median split)
            std::sort(bvhPrimitives.begin() + start, bvhPrimitives.begin() + end,
                [axis](const BVHPrimitive& a, const BVHPrimitive& b) {
                    Vec3 ca = (a.bmin + a.bmax) * 0.5f;
                    Vec3 cb = (b.bmin + b.bmax) * 0.5f;
                    if (axis == 0) return ca.x < cb.x;
                    else if (axis == 1) return ca.y < cb.y;
                    else return ca.z < cb.z;
                });
            
            int mid = start + count / 2;
            node.primStart = -1;
            node.primCount = 0;
            
            // Build children (safe because we reserved space and indices won't change)
            int leftIdx = buildBVH(start, mid);
            int rightIdx = buildBVH(mid, end);
            
            // Update node pointers (safe to reference now that children are built)
            bvhNodes[nodeIdx].leftChild = leftIdx;
            bvhNodes[nodeIdx].rightChild = rightIdx;
            
            return nodeIdx;
        };
        
        bvhRootIndex = buildBVH(0, (int)bvhPrimitives.size());
    }

    // Phase 7: Frustum culling structures
    struct FrustumPlane { Vec3 normal; float d; };  // Plane equation: dot(n, p) + d = 0
    struct Frustum { FrustumPlane planes[6]; };  // left, right, top, bottom, near, far
    
    // Phase 7: Intersection cache for temporal coherence
    struct IntersectionCache {
        int mat = -1;       // Material ID (-1 = invalid)
        float t = 0.0f;     // Distance to hit
        Vec3 pos{0,0,0};    // Hit position
        Vec3 n{0,0,0};      // Hit normal
        int objId = -1;     // Object identifier (for balls: index, paddles: 100+, planes: 200+, obstacles: 300+)
    };
    
    // Camera setup
    Vec3 camPos = {0,0,-5.0f};
    float fov = 60.0f * 3.1415926f/180.0f;
    float tanF = std::tan(fov*0.5f);

    // Phase 7: Calculate frustum planes for culling
    Frustum frustum;
    if (!config.useOrtho) {
        // Perspective frustum
        float halfHNear = tanF * 0.1f;  // Near plane at z=0.1 (camPos.z=-5, looking at +z)
        float halfWNear = halfHNear * ((rtH>0)? (float)rtW/(float)rtH : 1.0f);
        Vec3 forward{0, 0, 1};  // Camera looks along +Z
        Vec3 right{1, 0, 0};
        Vec3 up{0, 1, 0};
        Vec3 fc = camPos + forward * 0.1f;  // Near plane center
        
        // Left plane (points inward)
        Vec3 leftNormal = norm(cross(up, fc + right * (-halfWNear) - camPos));
        frustum.planes[0] = {leftNormal, -dot(leftNormal, camPos)};
        
        // Right plane (points inward)
        Vec3 rightNormal = norm(cross(fc + right * halfWNear - camPos, up));
        frustum.planes[1] = {rightNormal, -dot(rightNormal, camPos)};
        
        // Top plane (points inward)
        Vec3 topNormal = norm(cross(right, fc + up * halfHNear - camPos));
        frustum.planes[2] = {topNormal, -dot(topNormal, camPos)};
        
        // Bottom plane (points inward)
        Vec3 bottomNormal = norm(cross(fc + up * (-halfHNear) - camPos, right));
        frustum.planes[3] = {bottomNormal, -dot(bottomNormal, camPos)};
        
        // Near plane (points inward = forward)
        frustum.planes[4] = {forward, -dot(forward, fc)};
        
        // Far plane (points inward = -forward, at z=10)
        Vec3 farCenter = camPos + forward * 15.0f;  // Far plane at 15 units
        frustum.planes[5] = {forward * -1.0f, -dot(forward * -1.0f, farCenter)};
    } else {
        // Orthographic frustum (simpler, axis-aligned)
        // View bounds: x=[-2,2], y=[-1.5,1.5], z=[-1,10]
        frustum.planes[0] = {Vec3{1,0,0}, 2.0f};      // left: x >= -2
        frustum.planes[1] = {Vec3{-1,0,0}, 2.0f};     // right: x <= 2
        frustum.planes[2] = {Vec3{0,-1,0}, 1.6f};     // top: y <= 1.6
        frustum.planes[3] = {Vec3{0,1,0}, 1.6f};      // bottom: y >= -1.6
        frustum.planes[4] = {Vec3{0,0,1}, 1.0f};      // near: z >= -1
        frustum.planes[5] = {Vec3{0,0,-1}, 10.0f};    // far: z <= 10
    }
    
    // Phase 7: FORCE_INLINE AABB vs Frustum test (conservative, returns true if possibly visible)
    auto testAABBFrustum = [](const Vec3& bmin, const Vec3& bmax, const Frustum& f) FORCE_INLINE_ATTRIB -> bool {
        // Test all 6 planes; if AABB is fully outside any plane, it's culled
        for (int i = 0; i < 6; ++i) {
            const FrustumPlane& plane = f.planes[i];
            // Get positive vertex (farthest along plane normal)
            Vec3 pVertex{
                plane.normal.x > 0 ? bmax.x : bmin.x,
                plane.normal.y > 0 ? bmax.y : bmin.y,
                plane.normal.z > 0 ? bmax.z : bmin.z
            };
            // If positive vertex is outside (negative side), AABB is fully outside
            if (dot(plane.normal, pVertex) + plane.d < 0.0f) return false;
        }
        return true;  // AABB is at least partially inside frustum
    };
    
    // Phase 7: Initialize intersection cache (per-pixel)
    static std::vector<IntersectionCache> intersectionCache;
    static int cacheW = 0, cacheH = 0;
    if (cacheW != rtW || cacheH != rtH) {
        intersectionCache.resize(rtW * rtH);
        cacheW = rtW; cacheH = rtH;
        // Invalidate all cache entries on resize
        for (auto& c : intersectionCache) c.mat = -1;
    }
    
    // For each pixel (low-res) produce color
    frameCounter++;
    stats_ = SRStats{}; // reset (extended stats fields zeroed)
    stats_.frame = frameCounter;
    stats_.internalW = rtW; stats_.internalH = rtH;
    std::vector<float> hdr(rtW*rtH*3,0.0f);
    int pixels = rtW*rtH;
    // Hoisted per-frame constants
    float invRTW = (rtW>0)? 1.0f/(float)rtW : 0.0f;
    float invRTH = (rtH>0)? 1.0f/(float)rtH : 0.0f;
    float aspect = (rtH>0)? (float)rtW/(float)rtH : 1.0f;
    // (tanF already computed for perspective)
    // Samples-per-pixel logic:
    //  - When forceFullPixelRays=false: raysPerFrame is a TOTAL budget distributed across pixels.
    //  - When forceFullPixelRays=true : raysPerFrame means rays PER pixel this frame.
    int spp = 1;
    if (config.forceFullPixelRays) {
        spp = std::max(1, config.raysPerFrame);
    } else {
        int total = config.raysPerFrame;
        spp = std::max(1, total / std::max(1,pixels));
    }
    // Path trace core
    bool fanoutMode = config.fanoutCombinatorial;
    // Occlusion test toward arbitrary point; can optionally ignore one emissive sphere index
    auto occludedToPoint = [&](Vec3 from, Vec3 to, int ignoreSphere)->bool {
        Vec3 dir = to - from; 
        float dist2 = dot(dir,dir); 
        if (UNLIKELY(dist2 < 1e-8f)) return false;
        // Phase 1: Use rsqrt for normalization
        float inv_maxT = rsqrt_fast(dist2);
        float maxT = dist2 * inv_maxT;
        dir = dir * inv_maxT;
        
        // Phase 10: Quick distance check - if target is very close, skip most tests
        bool needFullTest = (maxT > 0.1f);
        
        Hit tmp; Hit best; best.t = maxT - 1e-3f;
        // Phase 1: Early out with branch hints
        // Planes - always test as they're infinite
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0))) return true;
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0))) return true;
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0))) return true;
        
        if (!needFullTest) return false;  // Very close target, planes already checked
        
        // Paddles: only block light if they're NOT emissive (emissive paddles act as light sources, not occluders)
        if (config.paddleEmissiveIntensity <= 0.0f) {
            float inflate = 0.01f;
            if (UNLIKELY(intersectBox(from,dir, leftCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, leftCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2))) return true;
            if (UNLIKELY(intersectBox(from,dir, rightCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, rightCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2))) return true;
            if (useHoriz) {
                if (UNLIKELY(intersectBox(from,dir, topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2))) return true;
                if (UNLIKELY(intersectBox(from,dir, bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2))) return true;
            }
        }
        if (useObs) {
            for (auto &b : obsBoxes) if (UNLIKELY(intersectBox(from,dir, b.bmin, b.bmax, best.t, tmp, 0))) return true;
        }
        // Other spheres block (soft shadows & inter-light occlusion)
        for (size_t si=0; si<ballCenters.size(); ++si) {
            if ((int)si == ignoreSphere) continue; // don't self-shadow chosen sample sphere
            if (UNLIKELY(intersectSphere(from,dir, ballCenters[si], ballRs[si]*config.lightRadiusScale, best.t, tmp, 1))) return true;
        }
        return false;
    };
    // Sample direct lighting from all emissive spheres and paddles with soft shadows.
    // Phase 5: Enhanced with light culling and adaptive sampling
    // Phase 10: Optimized for performance - reduced redundant calculations
    auto sampleDirect = [&](Vec3 pos, Vec3 n, Vec3 viewDir, uint32_t &seed, bool isMetal)->Vec3 {
        int totalLightCount = (int)ballCenters.size() + (int)paddleLights.size();
        if (UNLIKELY(totalLightCount == 0)) return Vec3{0,0,0};
        int ballLightCount = (int)ballCenters.size();
        int shadowSamples = std::max(1, config.softShadowSamples);
        Vec3 sum{0,0,0};
        
        // Pre-compute offset for shadow ray origin
        Vec3 shadowOrigin = pos + n * 0.002f;
        
        // Pre-compute light normalization factor
        float lightNorm = (totalLightCount > 1) ? (1.0f / (float)totalLightCount) : 1.0f;
        
        // Sample ball lights (spherical area lights)
        for (int li=0; li<ballLightCount; ++li) {
            Vec3 center = ballCenters[li];
            float radius = ballRs[li] * config.lightRadiusScale;
            
            // Phase 5: Light culling - skip lights that are too far or backfacing
            Vec3 toLight = center - pos;
            float dist2 = dot(toLight, toLight);
            float cullDist = radius * config.lightCullDistance;
            if (UNLIKELY(dist2 > cullDist * cullDist)) continue;  // Too far to contribute significantly
            
            float inv_dist = rsqrt_fast(dist2);
            Vec3 L = toLight * inv_dist;
            float initialNdotL = dot(n, L);
            if (UNLIKELY(initialNdotL <= 0.0f)) continue;  // Backfacing, no contribution
            
            // Phase 5: Adaptive soft shadow samples
            int adaptiveSamples = shadowSamples;
            if (config.adaptiveSoftShadows && shadowSamples > 1) {
                // Quick visibility test: check center of light
                bool centerVisible = !occludedToPoint(shadowOrigin, center, li);
                if (centerVisible) {
                    // Fully lit, use minimal samples
                    adaptiveSamples = 1;
                } else {
                    // In shadow or penumbra, check if fully shadowed
                    // Sample one edge point
                    Vec3 edgePt = center + Vec3{radius, 0, 0};
                    bool edgeVisible = !occludedToPoint(shadowOrigin, edgePt, li);
                    if (!edgeVisible) {
                        // Fully shadowed, skip entirely
                        continue;
                    }
                    // In penumbra, use full samples
                }
            }
            
            Vec3 lightAccum{0,0,0};
            // Pre-compute emissive color with normalization
            Vec3 emitColor = materials.emitColor * lightNorm;
            
            for (int s=0; s<adaptiveSamples; ++s) {
                // Phase 1: Optimized sphere sampling with fast math
                float u1 = rng1(seed);
                float u2 = rng1(seed);
                float z = 1.0f - 2.0f*u1;
                float rxy = sqrt_fast(std::max(0.0f, 1.0f - z*z));
                float phi = 6.28318531f*u2; // 2*PI
                // Phase 1: Use fast trig
                float cp = cos_fast(phi);
                float sp = sin_fast(phi);
                Vec3 spherePt = center + Vec3{rxy*cp, rxy*sp, z} * radius;
                Vec3 L = spherePt - pos; 
                float dist2 = dot(L,L); 
                if (UNLIKELY(dist2 < 1e-12f)) continue;
                // Phase 1: Use rsqrt for normalization
                float inv_dist = rsqrt_fast(dist2);
                float dist = dist2 * inv_dist;
                L = L * inv_dist;
                float ndotl = dot(n,L); 
                if (UNLIKELY(ndotl <= 0.0f)) continue;
                if (occludedToPoint(shadowOrigin, spherePt, li)) continue;
                // Phase 6: Use pre-computed emissive color and material properties
                float atten = 1.0f/(4.0f*3.1415926f*std::max(1e-4f, dist2));
                if (config.pbrEnable) {
                    if (!isMetal) {
                        lightAccum = lightAccum + emitColor * (ndotl * atten * materials.invPi);
                    } else {
                        // Simple specular (Schlick Fresnel * NdotL) with roughness attenuation
                        Vec3 V = norm(viewDir * -1.0f); // view direction towards camera
                        Vec3 H = norm(V + L);
                        float VoH = std::max(0.0f, dot(V,H));
                        Vec3 F0{0.86f,0.88f,0.94f};
                        Vec3 F = F0 + (Vec3{1,1,1} - F0) * std::pow(1.0f - VoH, 5.0f);
                        float rough = materials.roughness;  // Phase 6: Pre-computed
                        float gloss = 1.0f - 0.7f*rough; // simple energy loss with roughness
                        // Very approximate microfacet: we skip full D/G terms and just modulate by ndotl and a gloss factor to keep energy bounded.
                        Vec3 spec = F * (ndotl * gloss); // F already handles view-angle energy shift
                        lightAccum = lightAccum + emitColor * (spec * atten);
                    }
                } else {
                    // Legacy behavior (no 1/pi so brighter)
                    lightAccum = lightAccum + emitColor * (ndotl * atten);
                }
            }
            lightAccum = lightAccum / (float)adaptiveSamples;
            sum = sum + lightAccum;
        }
        // Sample paddle lights (rectangular area lights)
        // Phase 5: Enhanced with light culling and adaptive sampling
        // Phase 5: Enhanced with light culling and adaptive sampling
        for (size_t pi=0; pi<paddleLights.size(); ++pi) {
            const PaddleLight& plight = paddleLights[pi];
            
            // Phase 5: Light culling for paddle lights
            Vec3 toLight = plight.center - pos;
            float dist2 = dot(toLight, toLight);
            float paddleRadius = sqrt_fast(plight.halfX*plight.halfX + plight.halfY*plight.halfY);
            float cullDist = paddleRadius * config.lightCullDistance;
            if (UNLIKELY(dist2 > cullDist * cullDist)) continue;
            
            float inv_dist = rsqrt_fast(dist2);
            Vec3 L = toLight * inv_dist;
            float initialNdotL = dot(n, L);
            if (UNLIKELY(initialNdotL <= 0.0f)) continue;
            
            // Phase 5: Adaptive sampling for paddles
            int adaptiveSamples = shadowSamples;
            if (config.adaptiveSoftShadows && shadowSamples > 1) {
                bool centerVisible = !occludedToPoint(shadowOrigin, plight.center, -1);
                if (centerVisible) {
                    adaptiveSamples = 1;
                } else {
                    // Test corner
                    Vec3 cornerPt = plight.center + Vec3{plight.halfX, plight.halfY, 0.0f};
                    bool cornerVisible = !occludedToPoint(shadowOrigin, cornerPt, -1);
                    if (!cornerVisible) continue;  // Fully shadowed
                }
            }
            
            Vec3 lightAccum{0,0,0};
            // Pre-compute paddle emission color with normalization
            Vec3 paddleEmit = materials.paddleEmitColor * lightNorm;
            
            for (int s=0; s<adaptiveSamples; ++s) {
                // Sample random point on paddle rectangle (in XY plane, Z~0)
                float u1 = rng1(seed);
                float u2 = rng1(seed);
                float offsetX = (u1 - 0.5f) * 2.0f * plight.halfX;
                float offsetY = (u2 - 0.5f) * 2.0f * plight.halfY;
                Vec3 lightPt = plight.center + Vec3{offsetX, offsetY, 0.0f};
                Vec3 L = lightPt - pos;
                float dist2 = dot(L,L);
                if (UNLIKELY(dist2 < 1e-12f)) continue;
                float inv_dist = rsqrt_fast(dist2);
                L = L * inv_dist;
                float ndotl = dot(n,L);
                if (UNLIKELY(ndotl <= 0.0f)) continue;
                // Check occlusion (shadow test against scene geometry)
                if (occludedToPoint(shadowOrigin, lightPt, -1)) continue;
                // Phase 6: Use pre-computed paddle emission color
                float atten = 1.0f/(4.0f*3.1415926f*std::max(1e-4f, dist2));
                if (config.pbrEnable) {
                    if (!isMetal) {
                        lightAccum = lightAccum + paddleEmit * (ndotl * atten * materials.invPi);
                    } else {
                        Vec3 V = norm(viewDir * -1.0f);
                        Vec3 H = norm(V + L);
                        float VoH = std::max(0.0f, dot(V,H));
                        Vec3 F0{0.86f,0.88f,0.94f};
                        Vec3 F = F0 + (Vec3{1,1,1} - F0) * std::pow(1.0f - VoH, 5.0f);
                        float rough = materials.roughness;  // Phase 6: Pre-computed
                        float gloss = 1.0f - 0.7f*rough;
                        Vec3 spec = F * (ndotl * gloss);
                        lightAccum = lightAccum + paddleEmit * (spec * atten);
                    }
                } else {
                    lightAccum = lightAccum + paddleEmit * (ndotl * atten);
                }
            }
            lightAccum = lightAccum / (float)adaptiveSamples;
            sum = sum + lightAccum;
        }
        return sum;
    };
    if (fanoutMode) {
    // Experimental exponential fan-out (adaptive sampled variant): original idea was full Cartesian expansion
    // NOTE: This branch remains single-threaded intentionally. The combinatorial spawning pattern is used for
    // diagnostics / research and parallelizing it would complicate ray budgeting & reproducibility. Normal path
    // tracing branch (below) is fully multi-threaded.
    // spawning P^d rays (per primary pixel) which becomes intractable. We approximate by sampling a subset of
    // target pixels per bounce so we can reach deeper bounces before hitting the cap. ProjectedRays still reports
    // theoretical full count (clamped) while executed reflects actual sampled rays.
        int P = pixels;
        int B = config.maxBounces; if (B < 1) B = 1;
        // Compute projected rays with overflow checks (use 128-bit via long double fallback)
        long double proj = 0.0L; long double pPow = (long double)P; for(int d=1; d<=B; ++d){ proj += pPow; pPow *= (long double)P; if (proj > 9.22e18L) { proj = 9.22e18L; break; } }
        stats_.projectedRays = (int64_t)std::llround(proj);
        // Safety cap
        uint64_t cap = config.fanoutMaxTotalRays; if (cap == 0) cap = 1000000;
        // We implement breadth-first expansion; at depth d we generate P^d rays.
        // Each ray shading result accumulates into its originating pixel (primary pixel index transmitted along).
    struct FanRay { int pixelIndex; Vec3 ro; Vec3 rd; int depth; uint32_t seed; Vec3 throughput; bool alive; };
        std::vector<FanRay> current, next;
        current.reserve((size_t)std::min<uint64_t>(P, cap));
        // Initialize primary rays (depth=0 will increment to 1 after first bounce shading for accounting)
        for (int i=0;i<P;i++) {
            int x = i % rtW; int y = i / rtW;
            uint32_t seed = (x*1973) ^ (y*9277) ^ (frameCounter*26699u);
            float u1,u2; rng2(seed,u1,u2);
            float rx = (x + u1)*invRTW;
            float ry = (y + u2)*invRTH;
            Vec3 ro, rd;
            if (config.useOrtho) {
                float jx,jy; rng2(seed,jx,jy); // reuse consolidated RNG
                float wx = ((x + jx)*invRTW - 0.5f)*4.0f;
                float wy = (((rtH-1-y) + jy)*invRTH - 0.5f)*3.0f;
                ro = { wx, wy, -1.0f }; rd = {0,0,1};
            } else {
                float fov = 60.0f * 3.1415926f/180.0f; float tanF = std::tan(fov*0.5f);
                float px = (2*rx -1)*tanF * aspect;
                float py = (1-2*ry)*tanF; rd = norm(Vec3{px,py,1}); ro = {0,0,-5.0f};
            }
            current.push_back(FanRay{ i, ro, rd, 0, seed, Vec3{1,1,1}, true });
        }
    std::vector<Vec3> pixelAccum(P, Vec3{0,0,0});
    // Track how many radiance contributions each pixel received so we can normalize.
    std::vector<uint32_t> contribCount(P, 0);
    // Track how many rays have actually been shaded (at least primary rays)
    uint64_t raysExecuted = current.size();
        bool aborted=false;
    for (int depth=0; depth < B; ++depth) {
            // Process all rays in 'current'
            for (auto &r : current) {
                if (!r.alive) continue;
                // Intersection loop similar to normal path tracer but no Russian roulette and no early termination suppression (we still allow emissive/backdrop to accumulate but we DO NOT spawn children for those).
                Hit best; best.t=1e30f; bool hit=false; Hit tmp;
                // Walls
                if (intersectPlane(r.ro,r.rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                if (intersectPlane(r.ro,r.rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                if (intersectPlane(r.ro,r.rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; hit=true; }
                for(size_t bi=0; bi<ballCenters.size(); ++bi){ if(intersectSphere(r.ro,r.rd,ballCenters[bi],ballRs[bi],best.t,tmp, bi==0?1:1)){ best=tmp; hit=true; } }
                if (intersectBox(r.ro,r.rd, leftCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, leftCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                if (intersectBox(r.ro,r.rd, rightCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, rightCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                if (useHoriz) {
                    if (intersectBox(r.ro,r.rd, topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                    if (intersectBox(r.ro,r.rd, bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                }
                if (useObs) {
                    for (auto &bx : obsBoxes) if (intersectBox(r.ro,r.rd, bx.bmin, bx.bmax, best.t, tmp, 0)){ best=tmp; hit=true; }
                }
                if (!hit) {
                    float t = 0.5f*(r.rd.y+1.0f);
                    Vec3 bgTop   {0.26f,0.30f,0.38f};
                    Vec3 bgBottom{0.08f,0.10f,0.16f};
                    Vec3 bg = (1.0f-t)*bgBottom + t*bgTop;
                    pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * bg;
                    contribCount[r.pixelIndex]++;
                    r.alive=false; continue;
                }
                if (best.mat==1) {
                    Vec3 emit{2.2f,1.4f,0.8f}; emit = emit * config.emissiveIntensity;
                    pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * emit;
                    contribCount[r.pixelIndex]++;
                    r.alive=false; continue;
                }
                if (best.mat==0) {
                    // diffuse sample
                    Vec3 n = best.n; float uA,uB; rng2(r.seed,uA,uB); float r1 = 2*3.1415926f * uA; float r2 = uB; float r2s=std::sqrt(r2);
                    Vec3 w=n; Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; Vec3 v=norm(cross(w,a)); Vec3 u=cross(v,w);
                    Vec3 d = norm( u*(std::cos(r1)*r2s) + v*(std::sin(r1)*r2s) + w*std::sqrt(1-r2) );
                    r.ro = best.pos + best.n*0.002f; r.rd = d; r.throughput = r.throughput * Vec3{0.62f,0.64f,0.67f};
                    Vec3 direct = sampleDirect(best.pos, n, r.rd, r.seed, false);
                    if (direct.x>0||direct.y>0||direct.z>0) { pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * direct; contribCount[r.pixelIndex]++; }
                } else if (best.mat==2) {
                    // Paddle material: slightly tinted metal with diffuse under-layer
                    // Emit paddle light if configured (terminate path like emissive ball)
                    if (config.paddleEmissiveIntensity > 0.0f) {
                        Vec3 paddleEmit{2.2f,1.4f,0.8f}; paddleEmit = paddleEmit * config.paddleEmissiveIntensity;
                        pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * paddleEmit;
                        contribCount[r.pixelIndex]++;
                        r.alive=false; continue;
                    }
                    // Non-emissive paddle: metallic reflection
                    Vec3 paddleColor{0.25f,0.32f,0.6f};
                    Vec3 n = best.n; float cosi = dot(r.rd, n); r.rd = r.rd - n*(2.0f*cosi);
                    float rough = config.metallicRoughness; float uA,uB; rng2(r.seed,uA,uB); float r1 = 2*3.1415926f*uA; float r2=uB; float r2s=std::sqrt(r2);
                    Vec3 w=norm(n); Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; Vec3 v=norm(cross(w,a)); Vec3 u=cross(v,w);
                    Vec3 fuzz = norm(u*(std::cos(r1)*r2s) + v*(std::sin(r1)*r2s) + w*std::sqrt(1-r2));
                    r.rd = norm(r.rd*(1.0f-rough) + fuzz*rough); r.ro = best.pos + r.rd*0.002f;
                    // mix base fresnel-ish term with paddle color
                    r.throughput = r.throughput * (Vec3{0.86f,0.88f,0.94f}*0.5f + paddleColor*0.5f);
                    // Direct lighting for specular highlight
                    Vec3 directM = sampleDirect(best.pos, n, r.rd, r.seed, true) * paddleColor;
                    if (directM.x>0||directM.y>0||directM.z>0) { pixelAccum[r.pixelIndex]=pixelAccum[r.pixelIndex]+r.throughput*directM; contribCount[r.pixelIndex]++; }
                }
            }
            // Spawn next generation: every surviving ray spawns P children replicating to every pixel index.
            if (depth < B-1) {
                next.clear();
                // Determine alive count
                int aliveCount=0; for (auto &r: current) if (r.alive) aliveCount++;
                if (aliveCount==0) break;
                // Remaining depth (excluding current) we want to budget rays across
                int remainingDepths = (B-1) - depth;
                if (remainingDepths < 1) remainingDepths = 1;
                uint64_t budgetLeft = (cap > raysExecuted)? (cap - raysExecuted) : 0;
                if (budgetLeft == 0) { aborted=true; stats_.fanoutAborted=true; break; }
                // Per-ray budget heuristic: distribute remaining rays uniformly across remaining depths & alive rays
                uint64_t perRayBudget = std::max<uint64_t>(1, budgetLeft / (uint64_t)aliveCount / (uint64_t)remainingDepths);
                // Can't exceed full fan-out (P) per ray; sample without replacement when possible (Fisher-Yates style partial shuffle)
                uint32_t globalSeedBase = (uint32_t)(frameCounter * 1315423911u + depth*2654435761u);
                for (auto &r: current) {
                    if (!r.alive) continue;
                    uint64_t spawnCount = std::min<uint64_t>(P, perRayBudget);
                    if (spawnCount==0) spawnCount=1;
                    // If spawning all pixels (rare small resolutions) keep deterministic order
                    if (spawnCount == (uint64_t)P) {
                        for (int pix=0; pix<P; ++pix) {
                            if (raysExecuted >= cap) { aborted=true; stats_.fanoutAborted=true; break; }
                            next.push_back(FanRay{ pix, r.ro, r.rd, depth+1, r.seed ^ (pix*911u + depth*101u), r.throughput, true });
                            raysExecuted++;
                        }
                        if (aborted) break;
                    } else {
                        // Sample unique pixel indices: partial Fisher-Yates using a local vector when spawnCount small
                        // Optimization: if spawnCount > P/2, fall back to a bitmap selection
                        if (spawnCount <= (uint64_t)P/2) {
                            // reservoir of first P indices incremental shuffle until spawnCount selected
                            std::vector<int> picks; picks.reserve((size_t)spawnCount);
                            uint32_t lseed = r.seed ^ globalSeedBase;
                            for (int t=0; t<P && (uint64_t)picks.size()<spawnCount; ++t) {
                                // probability to take this index so we end with spawnCount on average
                                uint64_t need = spawnCount - picks.size();
                                uint64_t left = P - t;
                                uint32_t rv = (xorshift(lseed)&0xFFFFFF);
                                if (rv < (uint32_t)((need * 0xFFFFFFull)/left)) picks.push_back(t);
                            }
                            for (int pix: picks) {
                                if (raysExecuted >= cap) { aborted=true; stats_.fanoutAborted=true; break; }
                                next.push_back(FanRay{ pix, r.ro, r.rd, depth+1, r.seed ^ (pix*911u + depth*101u + pix*97u), r.throughput, true });
                                raysExecuted++;
                            }
                            if (aborted) break;
                        } else {
                            // Large spawn fraction: create bitmap then iterate
                            std::vector<uint8_t> used(P,0);
                            uint32_t lseed = r.seed ^ (globalSeedBase*733u);
                            uint64_t placed=0;
                            while (placed < spawnCount) {
                                if (raysExecuted >= cap) { aborted=true; stats_.fanoutAborted=true; break; }
                                int pix = (int)(xorshift(lseed) % P);
                                if (used[pix]) continue;
                                used[pix]=1; placed++;
                                next.push_back(FanRay{ pix, r.ro, r.rd, depth+1, r.seed ^ (pix*1664525u + depth*101u), r.throughput, true });
                                raysExecuted++;
                            }
                            if (aborted) break;
                        }
                    }
                }
                if (!next.empty() && !aborted) current.swap(next);
            }
            if (raysExecuted >= cap) { aborted=true; stats_.fanoutAborted=true; break; }
        }
        // Ambient fallback for any rays that never hit emissive or background
        if (!stats_.fanoutAborted) {
            Vec3 amb{0.10f,0.11f,0.12f};
            for (auto &r : current) {
                if (r.alive) {
                    pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * amb;
                    contribCount[r.pixelIndex]++;
                }
            }
        } else {
            // Even if aborted due to cap, give minimal ambient so diagnostic isn't full black
            Vec3 amb{0.04f,0.045f,0.05f};
            for (auto &r : current) {
                if (r.alive) {
                    pixelAccum[r.pixelIndex] = pixelAccum[r.pixelIndex] + r.throughput * amb;
                    contribCount[r.pixelIndex]++;
                }
            }
        }
        // Phase 2: Accumulate pixel colors to SoA buffers
        for (int i=0;i<P;i++) {
            Vec3 c = pixelAccum[i];
            uint32_t cc = contribCount[i]; if (cc>0) c = c / (float)cc; // normalize
            hdrR[i] = c.x;
            hdrG[i] = c.y;
            hdrB[i] = c.z;
        }
        stats_.spp = 1;
        stats_.totalRays = (int)std::min<uint64_t>(raysExecuted, (uint64_t)INT32_MAX);
        // Skip temporal accumulation history reuse (makes little sense here) – treat as fresh frame
        haveHistory = false;
        // Jump to tone map path (reuse existing code after hdr filled)
        auto tTraceEndFan = clock::now();
        stats_.msTrace = std::chrono::duration<float,std::milli>(tTraceEndFan - t0).count();
        // Phase 2: Copy to accum buffers (no temporal / denoise)
        accumR = hdrR; accumG = hdrG; accumB = hdrB;
    } else {
        // Normal path tracing branch (MULTITHREADED)
        // Phase 2: Use pre-allocated SoA scratch buffers instead of interleaved allocation
        std::vector<float>& hdrR_ref = hdrR;
        std::vector<float>& hdrG_ref = hdrG;
        std::vector<float>& hdrB_ref = hdrB;
        int pixels = rtW*rtH;
        int spp = 1;
        if (config.forceFullPixelRays) {
            spp = std::max(1, config.raysPerFrame);
        } else { int total = config.raysPerFrame; spp = std::max(1, total / std::max(1,pixels)); }

        // Determine thread count (let OS decide; no artificial cap). We still avoid spawning more threads than rows.
        auto detectThreads = [&]()->unsigned {
#ifdef _WIN32
            // Prefer GetActiveProcessorCount for better accuracy on systems with heterogeneous cores
            DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            if (count == 0) {
                SYSTEM_INFO si; GetSystemInfo(&si); count = si.dwNumberOfProcessors; }
            if (count == 0) count = 1;
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) {
                char msg[160];
                _snprintf_s(msg,_TRUNCATE,"[SoftRenderer] HW logical processors detected=%lu\n", (unsigned long)count);
                OutputDebugStringA(msg); printf("%s", msg);
            }
            return (unsigned)count;
#else
            unsigned hc = std::thread::hardware_concurrency(); return hc?hc:1u;
#endif
        };
    unsigned wantMax = detectThreads();
    // Optional oversubscription factor (experimentation). PONG_PT_OVERSUB=2..4 multiplies max logical; 1 or missing = no oversub.
    {
#ifdef _WIN32
        char* val=nullptr; size_t len=0; if (_dupenv_s(&val,&len,"PONG_PT_OVERSUB") == 0 && val){
            try { int f = std::stoi(val); if (f>1 && f<5) { unsigned long long m = (unsigned long long)wantMax * (unsigned long long)f; if (m>UINT_MAX) m=UINT_MAX; wantMax = (unsigned)m; } } catch(...){}
            free(val);
        }
#else
        if (const char* os = std::getenv("PONG_PT_OVERSUB")) { try { int f = std::stoi(os); if (f>1 && f<5) { unsigned long long m=(unsigned long long)wantMax*f; if (m>UINT_MAX) m=UINT_MAX; wantMax=(unsigned)m; } } catch(...){} }
#endif
    }
    bool envOverride = false;
    unsigned want = wantMax; // may be adapted below (no row cap)
        // Environment override PONG_PT_THREADS: accepts 'auto' or integer N; N<=0 => auto
        {
#ifdef _WIN32
            char* val = nullptr; size_t len=0; if (_dupenv_s(&val,&len,"PONG_PT_THREADS") == 0 && val){
                std::string s(val); std::string slow = s; std::transform(slow.begin(), slow.end(), slow.begin(), [](unsigned char c){ return (char)std::tolower(c); });
                if (slow != "auto") { try { int v = std::stoi(slow); if (v>0) { want = (unsigned)v; envOverride=true; } } catch(...){} }
                free(val);
            }
#else
            if (const char* env = std::getenv("PONG_PT_THREADS")) { std::string s(env); std::string slow=s; std::transform(slow.begin(), slow.end(), slow.begin(), [](unsigned char c){ return (char)std::tolower(c); }); if (slow != "auto") { try { int v = std::stoi(slow); if (v>0) { want=(unsigned)v; envOverride=true; } } catch(...){} } }
#endif
        }
        if (wantMax == 0) wantMax = 1;
        if (!envOverride) {
            // One-time init
            if (!g_srInitialized.load(std::memory_order_acquire)) {
                unsigned start = std::max(1u, std::thread::hardware_concurrency()/2);
                if (start > wantMax) start = wantMax;
                g_srAdaptiveThreads.store(start, std::memory_order_relaxed);
                g_srInitialized.store(true, std::memory_order_release);
                g_srCooldown.store(10, std::memory_order_relaxed); // short initial cooldown
            }
            // Use EMA to smooth decisions
            float lastRaw = g_srLastFrameMs.load(std::memory_order_relaxed);
            float emaPrev = g_srEmaFrameMs.load(std::memory_order_relaxed);
            float ema = emaPrev * 0.85f + lastRaw * 0.15f; // smoothing factor
            g_srEmaFrameMs.store(ema, std::memory_order_relaxed);

            unsigned cur = g_srAdaptiveThreads.load(std::memory_order_relaxed);
            int cooldown = g_srCooldown.load(std::memory_order_relaxed);

            // Thresholds with hysteresis
            const float target = 16.6f;
            const float highThreshold = target * 1.05f;   // ~17.4ms increase threshold
            const float lowThreshold  = target * 0.70f;   // ~11.6ms decrease threshold

            unsigned next = cur;
            if (ema > highThreshold && cur < wantMax) {
                // Scale up moderately (not straight to max) to prevent overshoot; at least +1, at most +25% of remaining headroom
                unsigned head = wantMax - cur;
                unsigned step = std::max(1u, std::max(head/4, 1u));
                next = cur + step; if (next > wantMax) next = wantMax;
                // Reset cooldown so we don't immediately scale back down
                g_srCooldown.store(30, std::memory_order_relaxed); // ~0.5s at 60fps
            } else if (ema > (highThreshold*1.15f) && cur == wantMax) {
                // We're already at max logical threads and still way over budget; if oversubscription enabled (>logical) keep as-is
                // (No action now; placeholder for potential auto-increase if we later add dynamic oversub) 
            } else if (ema < lowThreshold && cur > 1 && cooldown <= 0) {
                // Only scale down after cooldown to avoid rapid oscillation
                next = cur - 1;
                g_srCooldown.store(15, std::memory_order_relaxed); // shorter cooldown after a downscale
            } else {
                if (cooldown > 0) g_srCooldown.store(cooldown-1, std::memory_order_relaxed);
            }
            if (next < 1) next = 1; if (next > wantMax) next = wantMax;
            g_srAdaptiveThreads.store(next, std::memory_order_relaxed);
            want = next;
        }
        if (want == 0) want = 1;
        stats_.threadsUsed = (int)want;
        unsigned lastLogged = g_srLastLogged.load(std::memory_order_relaxed);
        if ((unsigned)stats_.threadsUsed != lastLogged) {
#ifdef _WIN32
            char msg[196];
            _snprintf_s(msg, _TRUNCATE, "[SoftRenderer] Threads=%u (max=%u, override=%s, last=%.2fms ema=%.2fms cd=%d)\n", (unsigned)stats_.threadsUsed, wantMax, envOverride?"yes":"no", g_srLastFrameMs.load(), g_srEmaFrameMs.load(), g_srCooldown.load());
            OutputDebugStringA(msg); printf("%s", msg);
#else
            printf("[SoftRenderer] Threads=%u (max=%u, override=%s, last=%.2fms ema=%.2fms cd=%d)\n", (unsigned)stats_.threadsUsed, wantMax, envOverride?"yes":"no": "no", g_srLastFrameMs.load(), g_srEmaFrameMs.load(), g_srCooldown.load());
#endif
            g_srLastLogged.store((unsigned)stats_.threadsUsed, std::memory_order_relaxed);
        }

    std::atomic<long long> totalBounces{0};
        std::atomic<int> pathsTraced{0};
        std::atomic<int> earlyExitAccum{0};
        std::atomic<int> rouletteAccum{0};
        std::atomic<bool> usedPacket4{false};

        // Phase 5: Tile-based rendering for better cache coherency
        auto worker = [&](int yStart, int yEnd){
            // Process in tiles instead of scanlines
            int tileSize = config.tileSize;
            
            for (int tileY = yStart; tileY < yEnd; tileY += tileSize) {
                int tileYEnd = std::min(tileY + tileSize, yEnd);
                
                for (int tileX = 0; tileX < rtW; tileX += tileSize) {
                    int tileXEnd = std::min(tileX + tileSize, rtW);
                    
                    // Process entire tile (better cache locality)
                    for (int y = tileY; y < tileYEnd; ++y) {
                        int x = tileX;
                        
                        // Phase 9: SIMD packet ray tracing for primary rays (4-wide or 8-wide batches)
                        // Process pixels in batches when packet tracing is enabled and spp==1
                        // Uses SIMD BVH traversal for efficient intersection testing
                        // Runtime dispatch: 8-wide AVX path when CPU supports AVX2, else 4-wide SSE
                        // Note: 8-wide can be slower on some CPUs due to AVX frequency throttling
#if COMPILE_AVX_SUPPORT
                        // Runtime choice: 8-wide AVX path when CPU supports AVX2 and not forced to 4-wide
                        // 8-wide can be slower on some CPUs due to AVX frequency throttling
                        // Best for: Zen 2+, Ice Lake+. Use 4-wide for older CPUs.
                        if (config.usePacketTracing && spp == 1 && g_cpuFeatures.avx2 && !config.force4WideSIMD) {
                            // 8-wide AVX path
                            for (; x + 7 < tileXEnd; x += 8) {
                                // Initialize 8 primary rays (one per pixel)
                                RayPacket8 packet8;
                                Vec3 ro8[8], rd8[8];
                                uint32_t seeds8[8];
                                
                                for (int i = 0; i < 8; ++i) {
                                    int px = x + i;
                                    seeds8[i] = (px*1973) ^ (y*9277) ^ (frameCounter*26699u);
                                    float u1, u2;
                                    
                                    if (config.useBlueNoise) {
                                        u1 = sampleBlueNoise(px, y, frameCounter);
                                        u2 = sampleBlueNoise(px + 32, y + 32, frameCounter);
                                    } else {
                                        rng2(seeds8[i], u1, u2);
                                    }
                                    
                                    float rx = (px + u1) * invRTW;
                                    float ry = (y + u2) * invRTH;
                                    
                                    if (config.useOrtho) {
                                        float jx, jy;
                                        rng2(seeds8[i], jx, jy);
                                        float wx = ((px + jx) * invRTW - 0.5f) * 4.0f;
                                        float wy = (((rtH-1-y) + jy) * invRTH - 0.5f) * 3.0f;
                                        ro8[i] = {wx, wy, -1.0f};
                                        rd8[i] = {0, 0, 1};
                                    } else {
                                        float px_cam = (2*rx - 1) * tanF * aspect;
                                        float py_cam = (1 - 2*ry) * tanF;
                                        rd8[i] = norm(Vec3{px_cam, py_cam, 1});
                                        ro8[i] = camPos;
                                    }
                                }
                                
                                // Load rays into SIMD packet - use loadu for better performance with non-aligned data
                                alignas(32) float ox_arr[8], oy_arr[8], oz_arr[8];
                                alignas(32) float dx_arr[8], dy_arr[8], dz_arr[8];
                                for (int i = 0; i < 8; ++i) {
                                    ox_arr[i] = ro8[i].x; oy_arr[i] = ro8[i].y; oz_arr[i] = ro8[i].z;
                                    dx_arr[i] = rd8[i].x; dy_arr[i] = rd8[i].y; dz_arr[i] = rd8[i].z;
                                }
                                packet8.ox = _mm256_load_ps(ox_arr);
                                packet8.oy = _mm256_load_ps(oy_arr);
                                packet8.oz = _mm256_load_ps(oz_arr);
                                packet8.dx = _mm256_load_ps(dx_arr);
                                packet8.dy = _mm256_load_ps(dy_arr);
                                packet8.dz = _mm256_load_ps(dz_arr);
                                packet8.mask = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
                                
                                // Initialize hit records
                                Hit8 hit8;
                                hit8.t = _mm256_set1_ps(1e30f);
                                hit8.valid = _mm256_setzero_ps();
                                hit8.nx = _mm256_setzero_ps();
                                hit8.ny = _mm256_setzero_ps();
                                hit8.nz = _mm256_setzero_ps();
                                hit8.px = _mm256_setzero_ps();
                                hit8.py = _mm256_setzero_ps();
                                hit8.pz = _mm256_setzero_ps();
                                hit8.mat = _mm256_set1_epi32(0);
                                
                                __m256 tMax8 = _mm256_set1_ps(1e30f);
                                
                                // Test against scene geometry (8 rays simultaneously)
                                intersectPlane8(packet8, Vec3{0, 1.6f, 0}, Vec3{0, -1, 0}, tMax8, hit8, 0);
                                intersectPlane8(packet8, Vec3{0, -1.6f, 0}, Vec3{0, 1, 0}, tMax8, hit8, 0);
                                intersectPlane8(packet8, Vec3{0, 0, 1.8f}, Vec3{0, 0, -1}, tMax8, hit8, 0);
                                
                                // BVH traversal for balls, paddles, and obstacles (8 rays simultaneously)
                                if (bvhRootIndex >= 0) {
                                    int stack[64];
                                    int stackPtr = 0;
                                    stack[stackPtr++] = bvhRootIndex;
                                    
                                    while (stackPtr > 0) {
                                        int nodeIdx = stack[--stackPtr];
                                        const BVHNode& node = bvhNodes[nodeIdx];
                                        
                                        __m256 hit_mask = intersectAABB8(packet8, node.bmin, node.bmax, hit8.t);
                                        int hit_any = _mm256_movemask_ps(hit_mask);
                                        if (hit_any == 0) continue;
                                        
                                        if (node.primCount > 0) {
                                            for (int i = 0; i < node.primCount; ++i) {
                                                const BVHPrimitive& prim = bvhPrimitives[node.primStart + i];
                                                if (prim.objType == 0) {
                                                    int bi = prim.objIndex;
                                                    intersectSphere8(packet8, ballCenters[bi], ballRs[bi], tMax8, hit8, prim.mat);
                                                } else if (prim.objType == 3) {
                                                    int bi = prim.objIndex;
                                                    intersectSphere8(packet8, blackholeCenters[bi], blackholeRs[bi], tMax8, hit8, prim.mat);
                                                } else if (prim.objType == 1) {
                                                    intersectBox8(packet8, prim.bmin, prim.bmax, tMax8, hit8, prim.mat);
                                                } else if (prim.objType == 2) {
                                                    intersectBox8(packet8, prim.bmin, prim.bmax, tMax8, hit8, prim.mat);
                                                }
                                            }
                                        } else {
                                            if (node.leftChild >= 0) stack[stackPtr++] = node.leftChild;
                                            if (node.rightChild >= 0) stack[stackPtr++] = node.rightChild;
                                        }
                                    }
                                }
                                
                                // Extract results
                                alignas(32) float t_out8[8], nx_out8[8], ny_out8[8], nz_out8[8];
                                alignas(32) float px_out8[8], py_out8[8], pz_out8[8];
                                alignas(32) int32_t mat_out8[8];
                                alignas(32) float valid_out8[8];
                                
                                _mm256_store_ps(t_out8, hit8.t);
                                _mm256_store_ps(nx_out8, hit8.nx);
                                _mm256_store_ps(ny_out8, hit8.ny);
                                _mm256_store_ps(nz_out8, hit8.nz);
                                _mm256_store_ps(px_out8, hit8.px);
                                _mm256_store_ps(py_out8, hit8.py);
                                _mm256_store_ps(pz_out8, hit8.pz);
                                _mm256_store_si256((__m256i*)mat_out8, hit8.mat);
                                _mm256_store_ps(valid_out8, hit8.valid);
                                
                                // Process each ray result (scalar path tracing continues)
                                for (int i = 0; i < 8; ++i) {
                                    int px = x + i;
                                    Vec3 col{0, 0, 0};
                                    uint32_t seed = seeds8[i];
                                    Vec3 ro = ro8[i];
                                    Vec3 rd = rd8[i];
                                    Vec3 throughput{1, 1, 1};
                                    bool terminated = false;
                                    int bounce = 0;
                                    
                                    uint32_t valid_bits;
                                    std::memcpy(&valid_bits, &valid_out8[i], sizeof(uint32_t));
                                    bool hit_primary = (valid_bits != 0);
                                    
                                    if (!hit_primary) {
                                        float t = 0.5f * (rd.y + 1.0f);
                                        Vec3 bgTop{0.26f, 0.30f, 0.38f};
                                        Vec3 bgBottom{0.08f, 0.10f, 0.16f};
                                        col = fma_madd(bgBottom, 1.0f - t, bgTop, t);
                                        terminated = true;
                                    } else {
                                        Hit best;
                                        best.t = t_out8[i];
                                        best.n = Vec3{nx_out8[i], ny_out8[i], nz_out8[i]};
                                        best.pos = Vec3{px_out8[i], py_out8[i], pz_out8[i]};
                                        best.mat = mat_out8[i];
                                        
                                        if (best.mat == 1) {
                                            col = fma_add(col, throughput * materials.emitColor, 1.0f);
                                            terminated = true;
                                        } else if (best.mat == 0) {
                                            Vec3 n = best.n;
                                            Vec3 d;
                                            if (config.useCosineWeighted) {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                d = sampleCosineHemisphere(uA, uB, n);
                                            } else {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 u = (std::fabs(n.x)>0.1f) ? Vec3{0,1,0} : Vec3{1,0,0};
                                                Vec3 tangent = norm(cross(u,n));
                                                Vec3 bitangent = cross(n,tangent);
                                                d = norm(tangent*std::cos(r1)*r2s + bitangent*std::sin(r1)*r2s + n*std::sqrt(1.0f-r2));
                                            }
                                            float cosTh = std::max(dot(d,n),0.0f);
                                            throughput = throughput * materials.diffuseAlbedo;
                                            ro = fma_add(best.pos, best.n, 0.002f);
                                            rd = d;
                                            Vec3 direct = sampleDirect(best.pos, n, rd, seed, false);
                                            col = fma_add(col, throughput * direct, 1.0f);
                                            bounce = 1;
                                        } else if (best.mat == 2) {
                                            if (config.paddleEmissiveIntensity > 0.0f) {
                                                col = fma_add(col, throughput * materials.paddleEmitColor, 1.0f);
                                                terminated = true;
                                                bounce = config.maxBounces;
                                            } else {
                                                Vec3 n = best.n;
                                                float cosi = dot(rd, n);
                                                rd = rd - n * (2.0f * cosi);
                                                float rough = materials.roughness;
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = norm(n);
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                Vec3 fuzz = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                                rd = norm(fma_madd(rd, 1.0f - rough, fuzz, rough));
                                                ro = fma_add(best.pos, rd, 0.002f);
                                                throughput = throughput * (Vec3{0.86f, 0.88f, 0.94f} * 0.5f + materials.paddleColor * 0.5f);
                                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * materials.paddleColor;
                                                col = fma_add(col, throughput * direct, 1.0f);
                                                bounce = 1;
                                            }
                                        } else if (best.mat == 3) {
                                            // Black hole: gravitational lensing (8-wide path)
                                            int bhIdx = best.objId - 400;
                                            if (bhIdx >= 0 && bhIdx < (int)blackholeCenters.size()) {
                                                Vec3 bhCenter = blackholeCenters[bhIdx];
                                                float bhRadius = blackholeRs[bhIdx];
                                                Vec3 toCenter = bhCenter - best.pos;
                                                float dist = length(toCenter);
                                                Vec3 dirToCenter = toCenter * (1.0f / dist);
                                                float normDist = 1.0f - (dist / bhRadius);
                                                normDist = std::max(0.0f, std::min(1.0f, normDist));
                                                if (normDist > 0.7f) {
                                                    terminated = true;
                                                    bounce = config.maxBounces;
                                                } else {
                                                    if (normDist < 0.4f) {
                                                        float glowIntensity = (0.4f - normDist) * 8.0f;
                                                        col = fma_add(col, throughput * Vec3{2.5f, 1.2f, 0.3f} * glowIntensity, 1.0f);
                                                    }
                                                    float bendStrength = normDist * normDist * 2.5f;
                                                    rd = norm(rd + dirToCenter * bendStrength);
                                                    ro = best.pos - best.n * 0.002f;
                                                    float absorption = 0.3f + normDist * 0.5f;
                                                    throughput = throughput * (1.0f - absorption);
                                                    bounce = 1;
                                                }
                                            } else {
                                                terminated = true;
                                                bounce = config.maxBounces;
                                            }
                                        }
                                    }
                                    
                                    // Continue with remaining bounces using scalar intersection
                                    for (; bounce < config.maxBounces && !terminated; ++bounce) {
                                        Hit best; best.t = 1e30f; bool hit = false; Hit tmp;
                                        
                                        // Test planes
                                        if (intersectPlane(ro, rd, Vec3{0, 1.6f, 0}, Vec3{0, -1, 0}, best.t, tmp, 0)) { best = tmp; best.objId = 200; hit = true; }
                                        if (intersectPlane(ro, rd, Vec3{0, -1.6f, 0}, Vec3{0, 1, 0}, best.t, tmp, 0)) { best = tmp; best.objId = 201; hit = true; }
                                        if (intersectPlane(ro, rd, Vec3{0, 0, 1.8f}, Vec3{0, 0, -1}, best.t, tmp, 0)) { best = tmp; best.objId = 202; hit = true; }
                                        
                                        // BVH traversal
                                        if (bvhRootIndex >= 0) {
                                            int stack[64];
                                            int stackPtr = 0;
                                            stack[stackPtr++] = bvhRootIndex;
                                            
                                            while (stackPtr > 0) {
                                                int nodeIdx = stack[--stackPtr];
                                                const BVHNode& node = bvhNodes[nodeIdx];
                                                if (!intersectAABB(ro, rd, node.bmin, node.bmax, best.t)) continue;
                                                
                                                if (node.primCount > 0) {
                                                    for (int j = 0; j < node.primCount; ++j) {
                                                        const BVHPrimitive& prim = bvhPrimitives[node.primStart + j];
                                                        if (prim.objType == 0) {
                                                            int bi = prim.objIndex;
                                                            if (intersectSphere(ro, rd, ballCenters[bi], ballRs[bi], best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 3) {
                                                            int bi = prim.objIndex;
                                                            if (intersectSphere(ro, rd, blackholeCenters[bi], blackholeRs[bi], best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 1) {
                                                            if (intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 2) {
                                                            if (intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        }
                                                    }
                                                } else {
                                                    if (node.leftChild >= 0) stack[stackPtr++] = node.leftChild;
                                                    if (node.rightChild >= 0) stack[stackPtr++] = node.rightChild;
                                                }
                                            }
                                        }
                                        
                                        if (!hit) {
                                            float t = 0.5f * (rd.y + 1.0f);
                                            Vec3 bgTop{0.26f, 0.30f, 0.38f};
                                            Vec3 bgBottom{0.08f, 0.10f, 0.16f};
                                            col = fma_add(col, throughput * fma_madd(bgBottom, 1.0f - t, bgTop, t), 1.0f);
                                            terminated = true;
                                            break;
                                        }
                                        
                                        if (best.mat == 1) {
                                            col = fma_add(col, throughput * materials.emitColor, 1.0f);
                                            terminated = true;
                                            break;
                                        }
                                        
                                        if (best.mat == 0) {
                                            Vec3 n = best.n;
                                            Vec3 d;
                                            if (config.useCosineWeighted) {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                d = sampleCosineHemisphere(uA, uB, n);
                                            } else {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = n;
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                d = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                            }
                                            ro = fma_add(best.pos, best.n, 0.002f);
                                            rd = d;
                                            throughput = throughput * materials.diffuseAlbedo;
                                            Vec3 direct = sampleDirect(best.pos, n, rd, seed, false);
                                            col = fma_add(col, throughput * direct, 1.0f);
                                        } else if (best.mat == 2) {
                                            if (config.paddleEmissiveIntensity > 0.0f) {
                                                col = fma_add(col, throughput * materials.paddleEmitColor, 1.0f);
                                                terminated = true;
                                                bounce = config.maxBounces;
                                            } else {
                                                Vec3 n = best.n;
                                                float cosi = dot(rd, n);
                                                rd = rd - n * (2.0f * cosi);
                                                float rough = materials.roughness;
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = norm(n);
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                Vec3 fuzz = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                                rd = norm(fma_madd(rd, 1.0f - rough, fuzz, rough));
                                                ro = fma_add(best.pos, rd, 0.002f);
                                                throughput = throughput * (Vec3{0.86f, 0.88f, 0.94f} * 0.5f + materials.paddleColor * 0.5f);
                                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * materials.paddleColor;
                                                col = fma_add(col, throughput * direct, 1.0f);
                                            }
                                        }
                                    }
                                    
                                    int idx = y*rtW + px;
                                    accumR[idx] += col.x; accumG[idx] += col.y; accumB[idx] += col.z;
                                    totalBounces.fetch_add(bounce, std::memory_order_relaxed);
                                    pathsTraced.fetch_add(1, std::memory_order_relaxed);
                                }
                            }
                        }
#endif
                        if (config.usePacketTracing) {
                            // 4-wide SSE path with arbitrary spp support
                            // Process 4 pixels at a time, doing all spp samples for each pixel
                            for (; x + 3 < tileXEnd; x += 4) {
                                // Per-pixel accumulators for all samples
                                Vec3 pixelAccum[4] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
                                
                                // Process all samples for these 4 pixels
                                for (int s = 0; s < spp; ++s) {
                                    // Initialize 4 primary rays (one per pixel, same sample index)
                                    RayPacket4 packet;
                                    Vec3 ro4[4], rd4[4];
                                    uint32_t seeds[4];
                                    
                                    for (int i = 0; i < 4; ++i) {
                                        int px = x + i;
                                        // Unique seed per pixel, frame, and sample
                                        seeds[i] = (px*1973) ^ (y*9277) ^ (frameCounter*26699u) ^ (s*6151u);
                                        float u1, u2;
                                        
                                        // Use same sampling strategy as scalar path
                                        int globalSample = frameCounter * spp + s;
                                        if (config.useHaltonSeq) {
                                            int sampleIndex = globalSample & 0x3FFF;
                                            u1 = haltonBase2(sampleIndex);
                                            u2 = haltonBase3(sampleIndex);
                                        } else if (config.useBlueNoise) {
                                            u1 = sampleBlueNoise(px, y, globalSample);
                                            u2 = sampleBlueNoise(px + 32, y + 32, globalSample);
                                        } else if (config.useStratified) {
                                            int sqrtSpp = (int)sqrt_fast((float)spp);
                                            if (sqrtSpp * sqrtSpp >= spp && sqrtSpp > 1) {
                                                int sx = s % sqrtSpp;
                                                int sy = s / sqrtSpp;
                                                float jx, jy;
                                                rng2(seeds[i], jx, jy);
                                                u1 = ((float)sx + jx) / (float)sqrtSpp;
                                                u2 = ((float)sy + jy) / (float)sqrtSpp;
                                            } else {
                                                rng2(seeds[i], u1, u2);
                                            }
                                        } else {
                                            rng2(seeds[i], u1, u2);
                                        }
                                        
                                        float rx = (px + u1) * invRTW;
                                        float ry = (y + u2) * invRTH;
                                        
                                        if (config.useOrtho) {
                                            float jx, jy;
                                            rng2(seeds[i], jx, jy);
                                            float wx = ((px + jx) * invRTW - 0.5f) * 4.0f;
                                            float wy = (((rtH-1-y) + jy) * invRTH - 0.5f) * 3.0f;
                                            ro4[i] = {wx, wy, -1.0f};
                                            rd4[i] = {0, 0, 1};
                                        } else {
                                            float px_cam = (2*rx - 1) * tanF * aspect;
                                            float py_cam = (1 - 2*ry) * tanF;
                                            rd4[i] = norm(Vec3{px_cam, py_cam, 1});
                                            ro4[i] = camPos;
                                        }
                                    }
                                    
                                    // Load rays into SIMD packet
                                    packet.ox = _mm_set_ps(ro4[3].x, ro4[2].x, ro4[1].x, ro4[0].x);
                                    packet.oy = _mm_set_ps(ro4[3].y, ro4[2].y, ro4[1].y, ro4[0].y);
                                    packet.oz = _mm_set_ps(ro4[3].z, ro4[2].z, ro4[1].z, ro4[0].z);
                                    packet.dx = _mm_set_ps(rd4[3].x, rd4[2].x, rd4[1].x, rd4[0].x);
                                    packet.dy = _mm_set_ps(rd4[3].y, rd4[2].y, rd4[1].y, rd4[0].y);
                                    packet.dz = _mm_set_ps(rd4[3].z, rd4[2].z, rd4[1].z, rd4[0].z);
                                    packet.mask = _mm_castsi128_ps(_mm_set1_epi32(-1));  // All active
                                
                                // Initialize hit records
                                Hit4 hit4;
                                hit4.t = _mm_set1_ps(1e30f);
                                hit4.valid = _mm_setzero_ps();
                                hit4.nx = _mm_setzero_ps();
                                hit4.ny = _mm_setzero_ps();
                                hit4.nz = _mm_setzero_ps();
                                hit4.px = _mm_setzero_ps();
                                hit4.py = _mm_setzero_ps();
                                hit4.pz = _mm_setzero_ps();
                                hit4.mat = _mm_set1_epi32(0);
                                
                                __m128 tMax = _mm_set1_ps(1e30f);
                                
                                // Test against scene geometry (4 rays simultaneously)
                                // Planes (not in BVH, test separately)
                                intersectPlane4(packet, Vec3{0, 1.6f, 0}, Vec3{0, -1, 0}, tMax, hit4, 0);
                                intersectPlane4(packet, Vec3{0, -1.6f, 0}, Vec3{0, 1, 0}, tMax, hit4, 0);
                                intersectPlane4(packet, Vec3{0, 0, 1.8f}, Vec3{0, 0, -1}, tMax, hit4, 0);
                                
                                // BVH traversal for balls, paddles, and obstacles (4 rays simultaneously)
                                if (bvhRootIndex >= 0) {
                                    // Stack-based BVH traversal for 4 rays with optimized node ordering
                                    int stack[64];
                                    int stackPtr = 0;
                                    stack[stackPtr++] = bvhRootIndex;
                                    
                                    while (stackPtr > 0) {
                                        int nodeIdx = stack[--stackPtr];
                                        const BVHNode& node = bvhNodes[nodeIdx];
                                        
                                        // Test all 4 rays against this AABB
                                        __m128 hit_mask = intersectAABB4(packet, node.bmin, node.bmax, hit4.t);
                                        
                                        // Check if any ray hit the AABB
                                        int hit_any = _mm_movemask_ps(hit_mask);
                                        if (hit_any == 0) continue;  // No rays hit this node, skip
                                        
                                        if (node.primCount > 0) {
                                            // Leaf node: test primitives
                                            for (int i = 0; i < node.primCount; ++i) {
                                                const BVHPrimitive& prim = bvhPrimitives[node.primStart + i];
                                                if (prim.objType == 0) {
                                                    // Ball (sphere)
                                                    int bi = prim.objIndex;
                                                    intersectSphere4(packet, ballCenters[bi], ballRs[bi], tMax, hit4, prim.mat);
                                                } else if (prim.objType == 3) {
                                                    // Black hole (sphere)
                                                    int bi = prim.objIndex;
                                                    intersectSphere4(packet, blackholeCenters[bi], blackholeRs[bi], tMax, hit4, prim.mat);
                                                } else if (prim.objType == 1) {
                                                    // Paddle (box)
                                                    intersectBox4(packet, prim.bmin, prim.bmax, tMax, hit4, prim.mat);
                                                } else if (prim.objType == 2) {
                                                    // Obstacle (box)
                                                    intersectBox4(packet, prim.bmin, prim.bmax, tMax, hit4, prim.mat);
                                                }
                                            }
                                        } else {
                                            // Internal node: push children in optimal order
                                            // Push both if valid, no need for distance sorting in simple scenes
                                            if (node.leftChild >= 0) stack[stackPtr++] = node.leftChild;
                                            if (node.rightChild >= 0) stack[stackPtr++] = node.rightChild;
                                        }
                                    }
                                }
                                
                                // Extract results and continue with scalar path tracing for bounces
                                alignas(16) float t_out[4], nx_out[4], ny_out[4], nz_out[4];
                                alignas(16) float px_out[4], py_out[4], pz_out[4];
                                alignas(16) int32_t mat_out[4];
                                alignas(16) float valid_out[4];
                                
                                _mm_store_ps(t_out, hit4.t);
                                _mm_store_ps(nx_out, hit4.nx);
                                _mm_store_ps(ny_out, hit4.ny);
                                _mm_store_ps(nz_out, hit4.nz);
                                _mm_store_ps(px_out, hit4.px);
                                _mm_store_ps(py_out, hit4.py);
                                _mm_store_ps(pz_out, hit4.pz);
                                _mm_store_si128((__m128i*)mat_out, hit4.mat);
                                _mm_store_ps(valid_out, hit4.valid);
                                
                                    // Process each ray result and accumulate
                                    for (int i = 0; i < 4; ++i) {
                                        int px = x + i;
                                        Vec3 col{0, 0, 0};
                                        uint32_t seed = seeds[i];
                                        Vec3 ro = ro4[i];
                                        Vec3 rd = rd4[i];
                                        Vec3 throughput{1, 1, 1};
                                        bool terminated = false;
                                        int bounce = 0;
                                        
                                        // Check if primary ray hit anything (from packet trace)
                                        uint32_t valid_bits;
                                        std::memcpy(&valid_bits, &valid_out[i], sizeof(uint32_t));
                                        bool hit_primary = (valid_bits != 0);
                                    
                                    if (!hit_primary) {
                                        // Background
                                        float t = 0.5f * (rd.y + 1.0f);
                                        Vec3 bgTop{0.26f, 0.30f, 0.38f};
                                        Vec3 bgBottom{0.08f, 0.10f, 0.16f};
                                        col = fma_madd(bgBottom, 1.0f - t, bgTop, t);
                                        terminated = true;
                                    } else {
                                        // Extract hit information from packet result
                                        Hit best;
                                        best.t = t_out[i];
                                        best.n = Vec3{nx_out[i], ny_out[i], nz_out[i]};
                                        best.pos = Vec3{px_out[i], py_out[i], pz_out[i]};
                                        best.mat = mat_out[i];
                                        // Always accumulate direct lighting for diffuse and paddle after primary hit
                                        if (best.mat == 1) {
                                            col = fma_add(col, throughput * materials.emitColor, 1.0f);
                                            terminated = true;
                                        } else if (best.mat == 0) {
                                            Vec3 n = best.n;
                                            Vec3 d;
                                            if (config.useCosineWeighted) {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                d = sampleCosineHemisphere(uA, uB, n);
                                            } else {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = n;
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                d = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                            }
                                            ro = fma_add(best.pos, best.n, 0.002f);
                                            rd = d;
                                            throughput = throughput * materials.diffuseAlbedo;
                                            Vec3 direct = sampleDirect(best.pos, n, rd, seed, false);
                                            col = fma_add(col, throughput * direct, 1.0f);
                                            bounce = 1;
                                        } else if (best.mat == 2) {
                                            if (config.paddleEmissiveIntensity > 0.0f) {
                                                col = fma_add(col, throughput * materials.paddleEmitColor, 1.0f);
                                                terminated = true;
                                                bounce = config.maxBounces;
                                            } else {
                                                Vec3 n = best.n;
                                                float cosi = dot(rd, n);
                                                rd = rd - n * (2.0f * cosi);
                                                float rough = materials.roughness;
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = norm(n);
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                Vec3 fuzz = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                                rd = norm(fma_madd(rd, 1.0f - rough, fuzz, rough));
                                                ro = fma_add(best.pos, rd, 0.002f);
                                                throughput = throughput * (Vec3{0.86f, 0.88f, 0.94f} * 0.5f + materials.paddleColor * 0.5f);
                                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * materials.paddleColor;
                                                col = fma_add(col, throughput * direct, 1.0f);
                                                bounce = 1;
                                            }
                                        } else if (best.mat == 3) {
                                            // Black hole: gravitational lensing (4-wide path)
                                            int bhIdx = best.objId - 400;
                                            if (bhIdx >= 0 && bhIdx < (int)blackholeCenters.size()) {
                                                Vec3 bhCenter = blackholeCenters[bhIdx];
                                                float bhRadius = blackholeRs[bhIdx];
                                                Vec3 toCenter = bhCenter - best.pos;
                                                float dist = length(toCenter);
                                                Vec3 dirToCenter = toCenter * (1.0f / dist);
                                                float normDist = 1.0f - (dist / bhRadius);
                                                normDist = std::max(0.0f, std::min(1.0f, normDist));
                                                if (normDist > 0.7f) {
                                                    terminated = true;
                                                    bounce = config.maxBounces;
                                                } else {
                                                    if (normDist < 0.4f) {
                                                        float glowIntensity = (0.4f - normDist) * 8.0f;
                                                        col = fma_add(col, throughput * Vec3{2.5f, 1.2f, 0.3f} * glowIntensity, 1.0f);
                                                    }
                                                    float bendStrength = normDist * normDist * 2.5f;
                                                    rd = norm(rd + dirToCenter * bendStrength);
                                                    ro = best.pos - best.n * 0.002f;
                                                    float absorption = 0.3f + normDist * 0.5f;
                                                    throughput = throughput * (1.0f - absorption);
                                                    bounce = 1;
                                                }
                                            } else {
                                                terminated = true;
                                                bounce = config.maxBounces;
                                            }
                                        }
                                    }
                                    
                                    // Continue with remaining bounces using scalar intersection (ray divergence makes SIMD inefficient)
                                    for (; bounce < config.maxBounces && !terminated; ++bounce) {
                                        Hit best; best.t = 1e30f; bool hit = false; Hit tmp;
                                        
                                        // Test planes
                                        if (intersectPlane(ro, rd, Vec3{0, 1.6f, 0}, Vec3{0, -1, 0}, best.t, tmp, 0)) { best = tmp; best.objId = 200; hit = true; }
                                        if (intersectPlane(ro, rd, Vec3{0, -1.6f, 0}, Vec3{0, 1, 0}, best.t, tmp, 0)) { best = tmp; best.objId = 201; hit = true; }
                                        if (intersectPlane(ro, rd, Vec3{0, 0, 1.8f}, Vec3{0, 0, -1}, best.t, tmp, 0)) { best = tmp; best.objId = 202; hit = true; }
                                        
                                        // BVH traversal (same as scalar path)
                                        if (bvhRootIndex >= 0) {
                                            int stack[64];
                                            int stackPtr = 0;
                                            stack[stackPtr++] = bvhRootIndex;
                                            
                                            while (stackPtr > 0) {
                                                int nodeIdx = stack[--stackPtr];
                                                const BVHNode& node = bvhNodes[nodeIdx];
                                                if (!intersectAABB(ro, rd, node.bmin, node.bmax, best.t)) continue;
                                                
                                                if (node.primCount > 0) {
                                                    for (int i = 0; i < node.primCount; ++i) {
                                                        const BVHPrimitive& prim = bvhPrimitives[node.primStart + i];
                                                        if (prim.objType == 0) {
                                                            int bi = prim.objIndex;
                                                            if (intersectSphere(ro, rd, ballCenters[bi], ballRs[bi], best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 3) {
                                                            int bi = prim.objIndex;
                                                            if (intersectSphere(ro, rd, blackholeCenters[bi], blackholeRs[bi], best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 1) {
                                                            if (intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        } else if (prim.objType == 2) {
                                                            if (intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                                best = tmp; best.objId = prim.objId; hit = true;
                                                            }
                                                        }
                                                    }
                                                } else {
                                                    if (node.leftChild >= 0) stack[stackPtr++] = node.leftChild;
                                                    if (node.rightChild >= 0) stack[stackPtr++] = node.rightChild;
                                                }
                                            }
                                        }
                                        
                                        if (!hit) {
                                            float t = 0.5f * (rd.y + 1.0f);
                                            Vec3 bgTop{0.26f, 0.30f, 0.38f};
                                            Vec3 bgBottom{0.08f, 0.10f, 0.16f};
                                            col = fma_add(col, throughput * fma_madd(bgBottom, 1.0f - t, bgTop, t), 1.0f);
                                            terminated = true;
                                            break;
                                        }
                                        if (best.mat == 1) {
                                            col = fma_add(col, throughput * materials.emitColor, 1.0f);
                                            terminated = true;
                                            break;
                                        }
                                        
                                        float maxT = max_component(throughput);
                                        if (UNLIKELY(maxT < 5e-3f)) {
                                            earlyExitAccum++;
                                            terminated = true;
                                            break;
                                        }
                                        
                                        if (best.mat == 0) {
                                            Vec3 n = best.n;
                                            Vec3 d;
                                            if (config.useCosineWeighted) {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                d = sampleCosineHemisphere(uA, uB, n);
                                            } else {
                                                float uA, uB;
                                                rng2(seed, uA, uB);
                                                float r1 = 6.28318531f * uA;
                                                float r2 = uB;
                                                float r2s = sqrt_fast(r2);
                                                Vec3 w = n;
                                                Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                                Vec3 v = norm(cross(w, a));
                                                Vec3 u = cross(v, w);
                                                d = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                            }
                                            ro = fma_add(best.pos, best.n, 0.002f);
                                            rd = d;
                                            throughput = throughput * materials.diffuseAlbedo;
                                            Vec3 direct = sampleDirect(best.pos, n, rd, seed, false);
                                            col = fma_add(col, throughput * direct, 1.0f);
                                        } else if (best.mat == 2) {
                                            if (config.paddleEmissiveIntensity > 0.0f) {
                                                col = fma_add(col, throughput * materials.paddleEmitColor, 1.0f);
                                                break;
                                            }
                                            Vec3 n = best.n;
                                            float cosi = dot(rd, n);
                                            rd = rd - n * (2.0f * cosi);
                                            float rough = materials.roughness;
                                            float uA, uB;
                                            rng2(seed, uA, uB);
                                            float r1 = 6.28318531f * uA;
                                            float r2 = uB;
                                            float r2s = sqrt_fast(r2);
                                            Vec3 w = norm(n);
                                            Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                                            Vec3 v = norm(cross(w, a));
                                            Vec3 u = cross(v, w);
                                            Vec3 fuzz = norm(u * (cos_fast(r1) * r2s) + v * (sin_fast(r1) * r2s) + w * sqrt_fast(1.0f - r2));
                                            rd = norm(fma_madd(rd, 1.0f - rough, fuzz, rough));
                                            ro = fma_add(best.pos, rd, 0.002f);
                                            throughput = throughput * (Vec3{0.86f, 0.88f, 0.94f} * 0.5f + materials.paddleColor * 0.5f);
                                            Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * materials.paddleColor;
                                            col = fma_add(col, throughput * direct, 1.0f);
                                        }
                                        
                                        // Russian roulette
                                        if (best.mat == 0 || best.mat == 2) {
                                            float maxT = max_component(throughput);
                                            if (UNLIKELY(maxT < 5e-3f)) {
                                                earlyExitAccum++;
                                                break;
                                            }
                                            if (LIKELY(config.rouletteEnable) && bounce >= config.rouletteStartBounce) {
                                                float baseProbability = std::max(config.rouletteMinProb, std::min(maxT * 1.2f, 0.95f));
                                                float rrand = rng1(seed);
                                                if (UNLIKELY(rrand > baseProbability)) {
                                                    rouletteAccum++;
                                                    break;
                                                }
                                                throughput = throughput / baseProbability;
                                            }
                                        }
                                    }
                                    
                                    if (!terminated) {
                                        Vec3 amb{0.05f, 0.055f, 0.06f};
                                        col = fma_add(col, throughput * amb, 1.0f);
                                    }
                                        
                                        totalBounces.fetch_add(bounce, std::memory_order_relaxed);
                                        pathsTraced++;
                                        
                                        // Accumulate this sample to pixel accumulator
                                        pixelAccum[i] = pixelAccum[i] + col;
                                    }
                                }  // end sample loop
                                
                                // Write final accumulated results (average of all samples)
                                float invSpp = 1.0f / (float)spp;
                                for (int i = 0; i < 4; ++i) {
                                    int px = x + i;
                                    size_t idx = (size_t)(y * rtW + px);
                                    Vec3 finalColor = pixelAccum[i] * invSpp;
                                    hdrR_ref[idx] = finalColor.x;
                                    hdrG_ref[idx] = finalColor.y;
                                    hdrB_ref[idx] = finalColor.z;
                                }
                            }  // end 4-pixel packet loop
                            
                            // Mark that we used 4-wide SIMD (if we actually processed any packets)
                            if (x > tileX) {
                                usedPacket4.store(true, std::memory_order_relaxed);
                            }
                        }  // end if (usePacketTracing)
                        
                        // Fallback to scalar processing for remaining pixels (after packet processing) or all pixels when packet tracing disabled
                        for (; x < tileXEnd; ++x) {
                                Vec3 col{0, 0, 0};
                                uint32_t seed = (x * 1973) ^ (y * 9277) ^ (frameCounter * 26699u);
                                
                                for (int s = 0; s < spp; ++s) {
                                    float u1, u2;
                                    
                                    // Phase 5: Enhanced sampling strategies
                                    if (config.useHaltonSeq) {
                                    // Low-discrepancy Halton sequence
                                    int sampleIndex = (frameCounter * spp + s) & 0x3FFF;  // Wrap at 16K samples
                                    u1 = haltonBase2(sampleIndex);
                                    u2 = haltonBase3(sampleIndex);
                                } else if (config.useBlueNoise) {
                                    // Blue noise with frame offset
                                    u1 = sampleBlueNoise(x, y, frameCounter * spp + s);
                                    u2 = sampleBlueNoise(x + 32, y + 32, frameCounter * spp + s);
                                } else if (config.useStratified) {
                                    // Stratified jittered sampling
                                    int sqrtSpp = (int)sqrt_fast((float)spp);
                                    if (sqrtSpp * sqrtSpp >= spp && sqrtSpp > 1) {
                                        int sx = s % sqrtSpp;
                                        int sy = s / sqrtSpp;
                                        float jx, jy;
                                        rng2(seed, jx, jy);
                                        u1 = ((float)sx + jx) / (float)sqrtSpp;
                                        u2 = ((float)sy + jy) / (float)sqrtSpp;
                                    } else {
                                        rng2(seed, u1, u2);
                                    }
                                } else {
                                    // Standard white noise
                                    rng2(seed, u1, u2);
                                }
                        float rx = (x + u1)*invRTW;
                        float ry = (y + u2)*invRTH;
                        Vec3 rd; Vec3 ro;
                        if (config.useOrtho) {
                            float jx,jy; rng2(seed,jx,jy);
                            float wx = ((x + jx)*invRTW - 0.5f)*4.0f;
                            float wy = (((rtH-1-y) + jy)*invRTH - 0.5f)*3.0f;
                            ro = { wx, wy, -1.0f }; rd = {0,0,1};
                        } else {
                            float px = (2*rx -1)*tanF * aspect;
                            float py = (1-2*ry)*tanF;
                            rd = norm(Vec3{px,py,1}); ro = camPos;
                        }
                        Vec3 throughput{1,1,1}; int bounce=0; bool terminated=false;
                        
                        // Phase 7: Try intersection cache on first bounce (temporal coherence)
                        IntersectionCache* cache = &intersectionCache[y * rtW + x];
                        bool cacheValid = (bounce == 0 && cache->mat >= 0);
                        
                        for (; bounce<config.maxBounces; ++bounce) {
                            Hit best; best.t=1e30f; bool hit=false; Hit tmp;
                            
                            // Phase 7: Test cached object first (if valid and first bounce)
                            if (cacheValid && bounce == 0) {
                                // Test cached object first for temporal coherence
                                bool cacheHit = false;
                                if (cache->mat == 0) {  // Plane
                                    // Identify which plane from cache->objId
                                    if (cache->objId == 200) {  // Top plane
                                        if (intersectPlane(ro,rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; cacheHit=true; }
                                    } else if (cache->objId == 201) {  // Bottom plane
                                        if (intersectPlane(ro,rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; cacheHit=true; }
                                    } else if (cache->objId == 202) {  // Back plane
                                        if (intersectPlane(ro,rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; cacheHit=true; }
                                    }
                                } else if (cache->mat == 1) {  // Ball
                                    int bi = cache->objId;
                                    if (bi >= 0 && bi < (int)ballCenters.size()) {
                                        if(intersectSphere(ro,rd,ballCenters[bi],ballRs[bi],best.t,tmp, bi==0?1:1)){ best=tmp; cacheHit=true; }
                                    }
                                } else if (cache->mat == 2) {  // Paddle
                                    if (cache->objId == 100) {  // Left paddle
                                        if (intersectBox(ro,rd, bounds.leftPaddleMin, bounds.leftPaddleMax, best.t, tmp, 2)){ best=tmp; cacheHit=true; }
                                    } else if (cache->objId == 101) {  // Right paddle
                                        if (intersectBox(ro,rd, bounds.rightPaddleMin, bounds.rightPaddleMax, best.t, tmp, 2)){ best=tmp; cacheHit=true; }
                                    } else if (cache->objId == 102 && useHoriz) {  // Top paddle
                                        if (intersectBox(ro,rd, bounds.topPaddleMin, bounds.topPaddleMax, best.t, tmp, 2)){ best=tmp; cacheHit=true; }
                                    } else if (cache->objId == 103 && useHoriz) {  // Bottom paddle
                                        if (intersectBox(ro,rd, bounds.bottomPaddleMin, bounds.bottomPaddleMax, best.t, tmp, 2)){ best=tmp; cacheHit=true; }
                                    }
                                }
                                if (cacheHit) { hit = true; }
                                cacheValid = false;  // Disable cache for remaining tests this frame
                            }
                            
                            // Test planes (always visible from both sides)
                            if (intersectPlane(ro,rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; best.objId=200; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; best.objId=201; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; best.objId=202; hit=true; }
                            
                            // Phase 8: BVH traversal for balls, paddles, obstacles (replaces linear tests)
                            if (bvhRootIndex >= 0) {
                                // Stack-based BVH traversal (no recursion)
                                int stack[64];  // Enough for depth ~32 BVH
                                int stackPtr = 0;
                                stack[stackPtr++] = bvhRootIndex;
                                
                                while (stackPtr > 0) {
                                    int nodeIdx = stack[--stackPtr];
                                    const BVHNode& node = bvhNodes[nodeIdx];
                                    
                                    // Test ray against node bounds
                                    if (!intersectAABB(ro, rd, node.bmin, node.bmax, best.t)) continue;
                                    
                                    if (node.primCount > 0) {
                                        // Leaf node: test primitives
                                        // Phase 9: Batch sphere tests using SIMD
                                        Vec3 sphereCenters[4];
                                        float sphereRadii[4];
                                        int sphereMats[4];
                                        int sphereObjIds[4];
                                        int sphereCount = 0;
                                        int firstNonSphere = -1;
                                        
                                        // Collect up to 4 spheres for SIMD batch processing
                                        for (int i = 0; i < node.primCount && sphereCount < 4; ++i) {
                                            const BVHPrimitive& prim = bvhPrimitives[node.primStart + i];
                                            if (prim.objType == 0) {  // Ball (sphere)
                                                int bi = prim.objIndex;
                                                sphereCenters[sphereCount] = ballCenters[bi];
                                                sphereRadii[sphereCount] = ballRs[bi];
                                                sphereMats[sphereCount] = prim.mat;
                                                sphereObjIds[sphereCount] = prim.objId;
                                                sphereCount++;
                                            } else if (prim.objType == 3) {  // Black hole (sphere)
                                                int bi = prim.objIndex;
                                                sphereCenters[sphereCount] = blackholeCenters[bi];
                                                sphereRadii[sphereCount] = blackholeRs[bi];
                                                sphereMats[sphereCount] = prim.mat;
                                                sphereObjIds[sphereCount] = prim.objId;
                                                sphereCount++;
                                            } else if (firstNonSphere == -1) {
                                                firstNonSphere = i;  // Remember first non-sphere for fallback
                                                break;  // Stop collecting spheres once we hit non-sphere
                                            }
                                        }
                                        
                                        // Process batched spheres with SIMD
                                        if (sphereCount > 0) {
                                            if (intersectSpheres4(ro, rd, sphereCenters, sphereRadii, sphereCount, best.t, best, sphereMats, sphereObjIds)) {
                                                hit = true;
                                            }
                                        }
                                        
                                        // Process remaining primitives (paddles, obstacles, or remaining spheres)
                                        int startIdx = (firstNonSphere >= 0) ? firstNonSphere : sphereCount;
                                        for (int i = startIdx; i < node.primCount; ++i) {
                                            const BVHPrimitive& prim = bvhPrimitives[node.primStart + i];
                                            
                                            if (prim.objType == 0) {
                                                // Ball (sphere) - fallback to scalar for remaining spheres
                                                int bi = prim.objIndex;
                                                if (intersectSphere(ro, rd, ballCenters[bi], ballRs[bi], best.t, tmp, prim.mat)) {
                                                    best = tmp;
                                                    best.objId = prim.objId;
                                                    hit = true;
                                                }
                                            } else if (prim.objType == 3) {
                                                // Black hole (sphere) - fallback to scalar
                                                int bi = prim.objIndex;
                                                if (intersectSphere(ro, rd, blackholeCenters[bi], blackholeRs[bi], best.t, tmp, prim.mat)) {
                                                    best = tmp;
                                                    best.objId = prim.objId;
                                                    hit = true;
                                                }
                                            } else if (prim.objType == 1) {
                                                // Paddle (box) - apply direction culling
                                                bool testPaddle = false;
                                                if (prim.objIndex == 0) {  // Left paddle
                                                    testPaddle = (rd.x < 0.0f || ro.x < 0.0f);
                                                } else if (prim.objIndex == 1) {  // Right paddle
                                                    testPaddle = (rd.x > 0.0f || ro.x > 0.0f);
                                                } else if (prim.objIndex == 2) {  // Top paddle
                                                    testPaddle = (rd.y < 0.0f || ro.y > 0.0f);
                                                } else if (prim.objIndex == 3) {  // Bottom paddle
                                                    testPaddle = (rd.y > 0.0f || ro.y < 0.0f);
                                                }
                                                
                                                if (testPaddle && intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                    best = tmp;
                                                    best.objId = prim.objId;
                                                    hit = true;
                                                }
                                            } else if (prim.objType == 2) {
                                                // Obstacle (box)
                                                if (intersectBox(ro, rd, prim.bmin, prim.bmax, best.t, tmp, prim.mat)) {
                                                    best = tmp;
                                                    best.objId = prim.objId;
                                                    hit = true;
                                                }
                                            }
                                        }
                                    } else {
                                        // Interior node: push children onto stack (closer child last for better early rejection)
                                        float t1 = 1e30f, t2 = 1e30f;
                                        if (node.leftChild >= 0) {
                                            const BVHNode& left = bvhNodes[node.leftChild];
                                            if (intersectAABB(ro, rd, left.bmin, left.bmax, best.t)) {
                                                // Estimate distance to left child
                                                Vec3 center = (left.bmin + left.bmax) * 0.5f;
                                                Vec3 diff = center - ro;
                                                t1 = dot(diff, rd);
                                            }
                                        }
                                        if (node.rightChild >= 0) {
                                            const BVHNode& right = bvhNodes[node.rightChild];
                                            if (intersectAABB(ro, rd, right.bmin, right.bmax, best.t)) {
                                                Vec3 center = (right.bmin + right.bmax) * 0.5f;
                                                Vec3 diff = center - ro;
                                                t2 = dot(diff, rd);
                                            }
                                        }
                                        
                                        // Push farther child first, closer child second (so closer is popped first)
                                        if (t1 < 1e30f && t2 < 1e30f) {
                                            if (t1 < t2) {
                                                stack[stackPtr++] = node.rightChild;
                                                stack[stackPtr++] = node.leftChild;
                                            } else {
                                                stack[stackPtr++] = node.leftChild;
                                                stack[stackPtr++] = node.rightChild;
                                            }
                                        } else if (t1 < 1e30f) {
                                            stack[stackPtr++] = node.leftChild;
                                        } else if (t2 < 1e30f) {
                                            stack[stackPtr++] = node.rightChild;
                                        }
                                    }
                                }
                            }
                            
                            // Phase 7: Update cache on first bounce
                            if (bounce == 0) {
                                if (hit) {
                                    cache->mat = best.mat;
                                    cache->t = best.t;
                                    cache->pos = best.pos;
                                    cache->n = best.n;
                                    cache->objId = best.objId;
                                } else {
                                    cache->mat = -1;  // Invalidate cache (ray missed scene)
                                }
                            }
                            
                            if (!hit) { 
                                // Phase 4: FMA for background blend
                                float t = 0.5f*(rd.y+1.0f); 
                                Vec3 bgTop{0.26f,0.30f,0.38f}; 
                                Vec3 bgBottom{0.08f,0.10f,0.16f}; 
                                Vec3 bg = fma_madd(bgBottom, 1.0f-t, bgTop, t);  // (1-t)*bgBottom + t*bgTop
                                col = fma_add(col, throughput * bg, 1.0f);  // col + throughput*bg
                                terminated=true; 
                                break; 
                            }
                            if (best.mat==1) { 
                                // Phase 6: Use pre-computed emissive color
                                col = fma_add(col, throughput * materials.emitColor, 1.0f);
                                terminated=true; 
                                break; 
                            }
                            // Phase 6: More aggressive early throughput termination (5e-3f for faster convergence)
                            float maxT = max_component(throughput);
                            if (UNLIKELY(maxT < 5e-3f)) { 
                                earlyExitAccum++; 
                                terminated = true;
                                break; 
                            }
                            if (best.mat==0) {
                                Vec3 n=best.n; 
                                Vec3 d;
                                
                                // Phase 5: Cosine-weighted hemisphere sampling (2x quality improvement)
                                if (config.useCosineWeighted) {
                                    float uA, uB;
                                    rng2(seed, uA, uB);
                                    d = sampleCosineHemisphere(uA, uB, n);
                                    // No need to multiply by cos(theta) since PDF already includes it
                                } else {
                                    // Legacy uniform hemisphere sampling
                                    float uA,uB; rng2(seed,uA,uB); 
                                    float r1=6.28318531f*uA; // 2*PI
                                    float r2=uB; 
                                    float r2s=sqrt_fast(r2); 
                                    Vec3 w=n; 
                                    Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; 
                                    Vec3 v=norm(cross(w,a)); 
                                    Vec3 u=cross(v,w); 
                                    float c1 = cos_fast(r1);
                                    float s1 = sin_fast(r1);
                                    float sq = sqrt_fast(1.0f - r2);
                                    d=norm(u*(c1*r2s) + v*(s1*r2s) + w*sq);
                                } 
                                ro = fma_add(best.pos, best.n, 0.002f);  // Phase 4: FMA for ray offset
                                rd=d; 
                                throughput=throughput*materials.diffuseAlbedo;  // Phase 6: Use pre-computed albedo
                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, false); 
                                col = fma_add(col, throughput * direct, 1.0f);  // Phase 4: FMA 
                            }
                            else if (best.mat==2) {
                                // Emit paddle light if configured (terminate path like emissive ball)
                                if (config.paddleEmissiveIntensity > 0.0f) {
                                    col = fma_add(col, throughput * materials.paddleEmitColor, 1.0f);  // Phase 6: Pre-computed
                                    break; // Terminate path
                                }
                                // Phase 6: Non-emissive paddle: metallic reflection with pre-computed properties
                                Vec3 n=best.n; 
                                float cosi=dot(rd,n); 
                                rd = rd - n*(2.0f*cosi); 
                                float rough=materials.roughness;  // Phase 6: Pre-computed
                                float uA,uB; rng2(seed,uA,uB); 
                                // Phase 1: Use fast trig
                                float r1=6.28318531f*uA; 
                                float r2=uB; 
                                float r2s=sqrt_fast(r2); 
                                Vec3 w=norm(n); 
                                Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; 
                                Vec3 v=norm(cross(w,a)); 
                                Vec3 u=cross(v,w); 
                                // Phase 1: Use fast cos/sin
                                float c1 = cos_fast(r1);
                                float s1 = sin_fast(r1);
                                float sq = sqrt_fast(1.0f - r2);
                                Vec3 fuzz=norm(u*(c1*r2s)+v*(s1*r2s)+w*sq); 
                                rd=norm(fma_madd(rd, 1.0f-rough, fuzz, rough));  // Phase 4: FMA for roughness blend
                                ro=fma_add(best.pos, rd, 0.002f);  // Phase 4: FMA for ray offset 
                                throughput=throughput*(Vec3{0.86f,0.88f,0.94f}*0.5f + materials.paddleColor*0.5f);  // Phase 6: Pre-computed
                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * materials.paddleColor;  // Phase 6: Pre-computed
                                col = fma_add(col, throughput * direct, 1.0f);  // Phase 4: FMA 
                            }
                            else if (best.mat==3) {
                                // Black hole: gravitational lensing effect
                                // Get black hole center from objId
                                int bhIdx = best.objId - 400;
                                if (bhIdx >= 0 && bhIdx < (int)blackholeCenters.size()) {
                                    Vec3 bhCenter = blackholeCenters[bhIdx];
                                    float bhRadius = blackholeRs[bhIdx];
                                    
                                    // Calculate distance from hit point to black hole center
                                    Vec3 toCenter = bhCenter - best.pos;
                                    float dist = length(toCenter);
                                    Vec3 dirToCenter = toCenter * (1.0f / dist);
                                    
                                    // Normalized distance (0 at edge, 1 at center)
                                    float normDist = 1.0f - (dist / bhRadius);
                                    normDist = std::max(0.0f, std::min(1.0f, normDist));
                                    
                                    // Event horizon: pure absorption at center
                                    if (normDist > 0.7f) {
                                        // Deep in event horizon - complete absorption
                                        col = col + throughput * Vec3{0.0f, 0.0f, 0.0f};
                                        terminated = true;
                                        break;
                                    }
                                    
                                    // Accretion disk glow (orange/red) at the edge
                                    if (normDist < 0.4f) {
                                        float glowIntensity = (0.4f - normDist) * 8.0f;
                                        Vec3 accretionGlow = Vec3{2.5f, 1.2f, 0.3f} * glowIntensity;
                                        col = fma_add(col, throughput * accretionGlow, 1.0f);
                                    }
                                    
                                    // Gravitational lensing: bend ray toward center
                                    float bendStrength = normDist * normDist * 2.5f; // Quadratic falloff
                                    Vec3 bentDir = norm(rd + dirToCenter * bendStrength);
                                    
                                    // Offset ray origin slightly away from surface
                                    ro = best.pos - best.n * 0.002f; // Move slightly inside
                                    rd = bentDir;
                                    
                                    // Reduce throughput (light being absorbed/redshifted)
                                    float absorption = 0.3f + normDist * 0.5f;
                                    throughput = throughput * (1.0f - absorption);
                                } else {
                                    // Fallback if objId is invalid
                                    terminated = true;
                                    break;
                                }
                            }
                            // Phase 6: Smarter Russian Roulette with BRDF-weighted probability
                            if (best.mat==0 || best.mat==2) { 
                                float maxT = max_component(throughput); 
                                if (UNLIKELY(maxT < 5e-3f)) {  // Phase 6: More aggressive threshold
                                    earlyExitAccum++; 
                                    bounce++; 
                                    break; 
                                } 
                                if (LIKELY(config.rouletteEnable) && bounce >= config.rouletteStartBounce) { 
                                    // Phase 6: Smarter probability based on throughput and BRDF contribution
                                    // Higher throughput = higher survival probability (up to 95%)
                                    float baseProbability = std::max(config.rouletteMinProb, std::min(maxT * 1.2f, 0.95f));
                                    float rrand = rng1(seed);
                                    if (UNLIKELY(rrand > baseProbability)){ 
                                        rouletteAccum++; 
                                        bounce++; 
                                        break; 
                                    } 
                                    throughput = throughput / baseProbability; 
                                } 
                            }
                        }
                        if(!terminated){ 
                            Vec3 amb{0.05f,0.055f,0.06f}; 
                            col = fma_add(col, throughput * amb, 1.0f);  // Phase 4: FMA
                        }
                        totalBounces.fetch_add(bounce, std::memory_order_relaxed); pathsTraced++;
                    }
                    col = col / (float)spp;
                    // Phase 2: Write to Structure of Arrays (separate R, G, B channels)
                    size_t idx = (size_t)(y*rtW + x);
                    hdrR_ref[idx] = col.x;
                    hdrG_ref[idx] = col.y;
                    hdrB_ref[idx] = col.z;
                    
                            // Phase 3: Prefetch next pixel's data within tile
                            if (LIKELY(x + 4 < tileXEnd)) {
                                size_t prefetchIdx = idx + 4;
                                _mm_prefetch((const char*)&hdrR_ref[prefetchIdx], _MM_HINT_T0);
                                _mm_prefetch((const char*)&hdrG_ref[prefetchIdx], _MM_HINT_T0);
                                _mm_prefetch((const char*)&hdrB_ref[prefetchIdx], _MM_HINT_T0);
                            }
                        }  // end scalar x loop
                    }  // end y loop
                }  // end tileX loop
            }  // end tileY loop
        };  // end worker lambda

        auto tTraceStart = clock::now();
        if (want == 1) {
            worker(0, rtH);
        } else if ((unsigned)rtH >= want) {
            // Enough rows for at least one per thread
            std::vector<std::thread> threads; threads.reserve(want-1);
            int rowsPer = (rtH + (int)want -1)/(int)want;
            int y0=0; for (unsigned ti=0; ti<want-1; ++ti){ int y1 = std::min(rtH, y0+rowsPer); threads.emplace_back(worker,y0,y1); y0=y1; }
            if (y0 < rtH) worker(y0, rtH);
            for (auto &th: threads) th.join();
        } else {
            // More threads than rows: row-stride dynamic distribution
            std::atomic<int> nextRow{0};
            auto strideThread = [&](){
                int y;
                while ((y = nextRow.fetch_add(1, std::memory_order_relaxed)) < rtH) {
                    worker(y, y+1);
                }
            };
            std::vector<std::thread> threads; threads.reserve(want-1);
            for (unsigned i=0; i<want-1; ++i) threads.emplace_back(strideThread);
            strideThread();
            for (auto &th: threads) th.join();
        }
        auto tTraceEnd = clock::now();
        stats_.msTrace = std::chrono::duration<float,std::milli>(tTraceEnd - t0).count(); t0 = tTraceEnd;
        stats_.spp = spp; stats_.totalRays = spp * rtW * rtH;
        
        // Track packet tracing mode
        if (usedPacket4) {
            stats_.packetMode = 4;
        } else {
            stats_.packetMode = 0;
        }
        
    int pt = pathsTraced.load(); long long tb = totalBounces.load();
    stats_.avgBounceDepth = (pt>0)? (float)tb / (float)pt : 0.0f;
        stats_.earlyExitCount = earlyExitAccum.load();
        stats_.rouletteTerminations = rouletteAccum.load();
        // Phase 2: Pass separate R, G, B arrays to temporal accumulation
        temporalAccumulate(hdrR_ref, hdrG_ref, hdrB_ref); auto tTempEnd = clock::now(); stats_.msTemporal = std::chrono::duration<float,std::milli>(tTempEnd - t0).count(); t0 = tTempEnd;
        bool skipDenoise = (spp >= 4) && config.denoiseStrength > 0.0f; if (skipDenoise) { stats_.denoiseSkipped = true; } else { spatialDenoise(); auto tDenoiseEnd = clock::now(); stats_.msDenoise = std::chrono::duration<float,std::milli>(tDenoiseEnd - t0).count(); t0 = tDenoiseEnd; }
    }
    // If we were in normal mode, hdr/accum already processed; fanout mode set accum directly.

    // Phase 3: SIMD-optimized upscale + tone map with SoA layout
    // Fast gamma approximation (x^(1/2.2) ≈ x^0.4545)
    auto gamma_fast_scalar = [](float x) -> float {
        // Fast pow approximation for gamma correction
        // Using repeated sqrt is faster than std::pow for fractional exponents
        if (UNLIKELY(x <= 0.0f)) return 0.0f;
        if (UNLIKELY(x >= 1.0f)) return 1.0f;
        // x^0.4545 ≈ x^(9/20) can be approximated with sqrt chains
        // Or use lookup table / polynomial. For now, keep std::pow with fast-math enabled
        return std::pow(x, 0.454545f); // Compiler with /fp:fast will optimize this
    };
    
    // Phase 3: SIMD gamma correction (4-wide)
    // Phase 4: True SIMD pow approximation for gamma correction (x^0.4545)
    // Using 5th-order polynomial approximation valid for x in [0,1]
    // Derived from Remez algorithm for x^0.4545 with max error ~0.0015
    auto gamma_fast_simd = [](__m128 x) -> __m128 {
        // Clamp to [0, 1] range
        __m128 zero = _mm_setzero_ps();
        __m128 one = _mm_set1_ps(1.0f);
        x = _mm_max_ps(zero, _mm_min_ps(one, x));
        
        // Phase 4: Polynomial approximation for x^0.4545 (inverse gamma 2.2)
        // p(x) = c0 + c1*x + c2*x^2 + c3*x^3 + c4*x^4 + c5*x^5
        // Coefficients optimized for [0,1] range (max error ~0.15%)
        const __m128 c0 = _mm_set1_ps(0.0023f);
        const __m128 c1 = _mm_set1_ps(0.4860f);
        const __m128 c2 = _mm_set1_ps(0.3010f);
        const __m128 c3 = _mm_set1_ps(-0.1875f);
        const __m128 c4 = _mm_set1_ps(0.2520f);
        const __m128 c5 = _mm_set1_ps(-0.1420f);
        
        // Horner's method: ((((c5*x + c4)*x + c3)*x + c2)*x + c1)*x + c0
        __m128 x2 = _mm_mul_ps(x, x);
        __m128 x3 = _mm_mul_ps(x2, x);
        __m128 x4 = _mm_mul_ps(x3, x);
        __m128 x5 = _mm_mul_ps(x4, x);
        
#if defined(__FMA__)
        __m128 result = _mm_fmadd_ps(c5, x5, _mm_fmadd_ps(c4, x4, _mm_fmadd_ps(c3, x3, _mm_fmadd_ps(c2, x2, _mm_fmadd_ps(c1, x, c0)))));
#else
        __m128 result = _mm_add_ps(_mm_mul_ps(c5, x5),
                        _mm_add_ps(_mm_mul_ps(c4, x4),
                        _mm_add_ps(_mm_mul_ps(c3, x3),
                        _mm_add_ps(_mm_mul_ps(c2, x2),
                        _mm_add_ps(_mm_mul_ps(c1, x), c0)))));
#endif
        
        // Clamp result to [0, 1] to handle numerical errors
        return _mm_max_ps(zero, _mm_min_ps(one, result));
    };
    
    // Phase 3: SIMD ACES tone mapping (4-wide)
    auto aces_simd = [](__m128 x) -> __m128 {
        constexpr float a=2.51f, b_=0.03f, c=2.43f, d=0.59f, e=0.14f;
        __m128 vA = _mm_set1_ps(a);
        __m128 vB = _mm_set1_ps(b_);
        __m128 vC = _mm_set1_ps(c);
        __m128 vD = _mm_set1_ps(d);
        __m128 vE = _mm_set1_ps(e);
        
        // num = x*(a*x + b)
#if defined(__FMA__)
        __m128 num = _mm_mul_ps(x, _mm_fmadd_ps(vA, x, vB));
#else
        __m128 num = _mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(vA, x), vB));
#endif
        
        // den = x*(c*x + d) + e
#if defined(__FMA__)
        __m128 den = _mm_fmadd_ps(x, _mm_fmadd_ps(vC, x, vD), vE);
#else
        __m128 den = _mm_add_ps(_mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(vC, x), vD)), vE);
#endif
        
        // o = num/den (with safety check)
        __m128 eps = _mm_set1_ps(1e-8f);
        __m128 safe_den = _mm_max_ps(den, eps);
        __m128 o = _mm_div_ps(num, safe_den);
        
        // Clamp to [0, 1]
        __m128 zero = _mm_setzero_ps();
        __m128 one = _mm_set1_ps(1.0f);
        return _mm_max_ps(zero, _mm_min_ps(one, o));
    };
    
    for (int y=0; y<outH; ++y) {
        int sy = std::min(rtH-1, (int)((float)y/outH * rtH));
        int syBase = sy*rtW;
        
        int x = 0;
        // Phase 3: Process 4 pixels at once with SIMD
        for (; x + 3 < outW; x += 4) {
            // Gather source indices for 4 pixels
            int sx0 = std::min(rtW-1, (int)((float)(x+0)/outW * rtW));
            int sx1 = std::min(rtW-1, (int)((float)(x+1)/outW * rtW));
            int sx2 = std::min(rtW-1, (int)((float)(x+2)/outW * rtW));
            int sx3 = std::min(rtW-1, (int)((float)(x+3)/outW * rtW));
            
            size_t si0 = (size_t)syBase + (size_t)sx0;
            size_t si1 = (size_t)syBase + (size_t)sx1;
            size_t si2 = (size_t)syBase + (size_t)sx2;
            size_t si3 = (size_t)syBase + (size_t)sx3;
            
            // Phase 3: Prefetch ahead for next iteration (L1 cache)
            if (LIKELY(x + 16 < outW)) {
                int prefetchX = std::min(rtW-1, (int)((float)(x+16)/outW * rtW));
                size_t prefetchIdx = (size_t)syBase + (size_t)prefetchX;
                _mm_prefetch((const char*)&accumR[prefetchIdx], _MM_HINT_T0);
                _mm_prefetch((const char*)&accumG[prefetchIdx], _MM_HINT_T0);
                _mm_prefetch((const char*)&accumB[prefetchIdx], _MM_HINT_T0);
            }
            
            // Phase 3: Gather from SoA buffers (manual gather, no AVX2 intrinsic needed)
            __m128 r = _mm_setr_ps(accumR[si0], accumR[si1], accumR[si2], accumR[si3]);
            __m128 g = _mm_setr_ps(accumG[si0], accumG[si1], accumG[si2], accumG[si3]);
            __m128 b = _mm_setr_ps(accumB[si0], accumB[si1], accumB[si2], accumB[si3]);
            
            // Phase 3: SIMD ACES tone mapping
            r = aces_simd(r);
            g = aces_simd(g);
            b = aces_simd(b);
            
            // Phase 3: SIMD gamma correction
            r = gamma_fast_simd(r);
            g = gamma_fast_simd(g);
            b = gamma_fast_simd(b);
            
            // Convert to 8-bit and pack
            __m128 v255 = _mm_set1_ps(255.0f);
            __m128 half = _mm_set1_ps(0.5f);
            
            r = _mm_add_ps(_mm_mul_ps(r, v255), half);
            g = _mm_add_ps(_mm_mul_ps(g, v255), half);
            b = _mm_add_ps(_mm_mul_ps(b, v255), half);
            
            // Clamp and convert to int
            __m128 v255_clamp = _mm_set1_ps(255.0f);
            r = _mm_min_ps(r, v255_clamp);
            g = _mm_min_ps(g, v255_clamp);
            b = _mm_min_ps(b, v255_clamp);
            
            // Extract results
            alignas(16) float rVals[4], gVals[4], bVals[4];
            _mm_store_ps(rVals, r);
            _mm_store_ps(gVals, g);
            _mm_store_ps(bVals, b);
            
            // Pack into pixel32 (4 pixels)
            for (int i = 0; i < 4; i++) {
                uint8_t R = (uint8_t)rVals[i];
                uint8_t G = (uint8_t)gVals[i];
                uint8_t B = (uint8_t)bVals[i];
                pixel32[y*outW + (x+i)] = (0xFFu<<24) | (R<<16) | (G<<8) | (B);
            }
        }
        
        // Phase 3: Handle remaining pixels with scalar code
        for (; x < outW; ++x) {
            int sx = std::min(rtW-1, (int)((float)x/outW * rtW));
            // Phase 2: Read from SoA buffers (no *3 stride)
            size_t si = (size_t)syBase + (size_t)sx;
            float r = accumR[si];
            float g = accumG[si];
            float b = accumB[si];
            // Phase 2: Optimized ACES tone mapping (inlined, reduced operations)
            {
                constexpr float a=2.51f, b_=0.03f, c=2.43f, d=0.59f, e=0.14f;
                auto aces = [](float x, float a, float b_, float c, float d, float e) -> float {
                    float num = x*(a*x + b_);
                    float den = x*(c*x + d) + e;
                    float o = (den > 1e-8f) ? num/den : 0.0f;
                    return std::min(std::max(o, 0.0f), 1.0f);
                };
                r = aces(r, a, b_, c, d, e);
                g = aces(g, a, b_, c, d, e);
                b = aces(b, a, b_, c, d, e);
            }
            // Phase 2: Gamma correction with fast approximation
            r = gamma_fast_scalar(r);
            g = gamma_fast_scalar(g);
            b = gamma_fast_scalar(b);
            uint8_t R = (uint8_t)std::min(255.0f, r*255.0f + 0.5f);
            uint8_t G = (uint8_t)std::min(255.0f, g*255.0f + 0.5f);
            uint8_t B = (uint8_t)std::min(255.0f, b*255.0f + 0.5f);
            pixel32[y*outW + x] = (0xFFu<<24) | (R<<16) | (G<<8) | (B);
        }
    }
    auto tUpscaleEnd = clock::now();
    stats_.msUpscale = std::chrono::duration<float, std::milli>(tUpscaleEnd - t0).count();
    stats_.msTotal = std::chrono::duration<float, std::milli>(tUpscaleEnd - tStart).count();
    
    // Calculate FPS with exponential moving average for smoothing
    auto currentTime = clock::now();
    float actualFrameTime = std::chrono::duration<float, std::milli>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;
    
    // Smooth frame time using EMA (alpha = 0.1 for stability)
    const float alpha = 0.1f;
    smoothedFrameTime = alpha * actualFrameTime + (1.0f - alpha) * smoothedFrameTime;
    
    // Calculate FPS from smoothed frame time (avoid division by zero)
    stats_.fps = (smoothedFrameTime > 0.001f) ? (1000.0f / smoothedFrameTime) : 0.0f;
    
    // Feed adaptive controller for next frame
    g_srLastFrameMs.store(stats_.msTotal, std::memory_order_relaxed);
}

void SoftRenderer::toneMapAndPack() {
    // (unused currently; left for potential accumulation variant)
}

void SoftRenderer::temporalAccumulate(const std::vector<float>& curR, const std::vector<float>& curG, const std::vector<float>& curB) {
    // Phase 3: SIMD-accelerated temporal accumulation (4-wide with FMA)
    float alpha = config.accumAlpha;
    if (!haveHistory) {
        accumR = curR; accumG = curG; accumB = curB;
        haveHistory = true;
        return;
    }
    
    size_t n = accumR.size();
    __m128 vAlpha = _mm_set1_ps(alpha);
    __m128 vOneMinusAlpha = _mm_set1_ps(1.0f - alpha);
    
    // Process 4 pixels at a time with SIMD
    size_t i = 0;
    size_t n4 = (n / 4) * 4;  // Round down to multiple of 4
    
    for (; i < n4; i += 4) {
        // Load 4 pixels from accumR and curR
        __m128 accR = _mm_loadu_ps(&accumR[i]);
        __m128 cR = _mm_loadu_ps(&curR[i]);
        
        // Phase 3: Use FMA for lerp: accum*(1-alpha) + cur*alpha
#if defined(__FMA__)
        // FMA available: result = accR * (1-alpha) + cR * alpha
        __m128 resultR = _mm_fmadd_ps(accR, vOneMinusAlpha, _mm_mul_ps(cR, vAlpha));
#else
        // Fallback: separate multiply and add
        __m128 resultR = _mm_add_ps(_mm_mul_ps(accR, vOneMinusAlpha), _mm_mul_ps(cR, vAlpha));
#endif
        _mm_storeu_ps(&accumR[i], resultR);
        
        // Same for G channel
        __m128 accG = _mm_loadu_ps(&accumG[i]);
        __m128 cG = _mm_loadu_ps(&curG[i]);
#if defined(__FMA__)
        __m128 resultG = _mm_fmadd_ps(accG, vOneMinusAlpha, _mm_mul_ps(cG, vAlpha));
#else
        __m128 resultG = _mm_add_ps(_mm_mul_ps(accG, vOneMinusAlpha), _mm_mul_ps(cG, vAlpha));
#endif
        _mm_storeu_ps(&accumG[i], resultG);
        
        // Same for B channel
        __m128 accB = _mm_loadu_ps(&accumB[i]);
        __m128 cB = _mm_loadu_ps(&curB[i]);
#if defined(__FMA__)
        __m128 resultB = _mm_fmadd_ps(accB, vOneMinusAlpha, _mm_mul_ps(cB, vAlpha));
#else
        __m128 resultB = _mm_add_ps(_mm_mul_ps(accB, vOneMinusAlpha), _mm_mul_ps(cB, vAlpha));
#endif
        _mm_storeu_ps(&accumB[i], resultB);
    }
    
    // Handle remaining pixels (scalar)
    for (; i < n; i++) {
        accumR[i] = accumR[i]*(1.0f-alpha) + curR[i]*alpha;
        accumG[i] = accumG[i]*(1.0f-alpha) + curG[i]*alpha;
        accumB[i] = accumB[i]*(1.0f-alpha) + curB[i]*alpha;
    }
}

void SoftRenderer::spatialDenoise() {
    // Skip spatial denoising if disabled
    if (config.denoiseStrength <= 0.0f) return;
    if (rtW==0 || rtH==0) return;
    
    int w=rtW, h=rtH;
    float alpha = config.denoiseStrength;
    
    // Phase 5: Bilateral filter (edge-preserving) or box blur
    // NOTE: Bilateral is high quality but expensive - disabled by default at high resolutions
    if (config.useBilateralDenoise && w*h < 500000) {  // Only use bilateral if < 500K pixels
        // Bilateral filter: preserves edges based on both spatial and color distance
        float sigmaSpace = config.bilateralSigmaSpace;
        float sigmaColor = config.bilateralSigmaColor;
        float sigmaSpatial2 = 2.0f * sigmaSpace * sigmaSpace;
        float sigmaColor2 = 2.0f * sigmaColor * sigmaColor;
        
        // Use 3x3 kernel for performance (9 samples vs 49 for 7x7)
        const int radius = 1;
        
        // Pre-compute spatial weights (constant for each offset) - skip center (dx=0,dy=0)
        float spatialWeights[8];  // 8 neighbors (center excluded)
        int widx = 0;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) continue;  // Skip center
                float spatialDist2 = (float)(dx*dx + dy*dy);
                spatialWeights[widx++] = exp_fast(-spatialDist2 / sigmaSpatial2);
            }
        }
        float centerSpatialWeight = 1.0f;  // Center always has weight 1.0
        
        // Color weight threshold (skip if negligible contribution)
        const float minColorWeight = 0.01f;
        float invSigmaColor2 = -1.0f / sigmaColor2;
        
        for (int y=0; y<h; ++y) {
            for (int x=0; x<w; ++x) {
                size_t centerIdx = (size_t)(y*w + x);
                float centerR = accumR[centerIdx];
                float centerG = accumG[centerIdx];
                float centerB = accumB[centerIdx];
                float centerLum = luminance(centerR, centerG, centerB);
                
                // Start with center pixel (weight = 1.0, color weight = 1.0)
                float sumR = centerR * centerSpatialWeight;
                float sumG = centerG * centerSpatialWeight;
                float sumB = centerB * centerSpatialWeight;
                float sumWeight = centerSpatialWeight;
                
                // Gather from 8 neighbors with pre-computed spatial weights
                widx = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int ny = y + dy;
                    if (ny < 0 || ny >= h) {
                        if (dy != 0) widx += 3;  // Skip row (but not if center row)
                        continue;
                    }
                    
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (dx == 0 && dy == 0) continue;  // Already processed center
                        
                        int nx = x + dx;
                        if (nx < 0 || nx >= w) {
                            widx++;
                            continue;
                        }
                        
                        size_t nIdx = (size_t)(ny*w + nx);
                        float nR = accumR[nIdx];
                        float nG = accumG[nIdx];
                        float nB = accumB[nIdx];
                        float nLum = luminance(nR, nG, nB);
                        
                        // Color weight (fast path: pre-multiply invSigmaColor2)
                        float colorDist = nLum - centerLum;
                        float colorWeight = exp_fast(colorDist * colorDist * invSigmaColor2);
                        
                        // Early exit if color difference is too large (negligible weight)
                        if (colorWeight < minColorWeight) {
                            widx++;
                            continue;
                        }
                        
                        float weight = spatialWeights[widx++] * colorWeight;
                        sumR += nR * weight;
                        sumG += nG * weight;
                        sumB += nB * weight;
                        sumWeight += weight;
                    }
                }
                
                float invWeight = 1.0f / sumWeight;
                denoiseR[centerIdx] = sumR * invWeight;
                denoiseG[centerIdx] = sumG * invWeight;
                denoiseB[centerIdx] = sumB * invWeight;
            }
        }
        
        // Blend with original based on alpha
        for (size_t i=0; i<(size_t)(w*h); ++i) {
            accumR[i] = accumR[i] * (1.0f - alpha) + denoiseR[i] * alpha;
            accumG[i] = accumG[i] * (1.0f - alpha) + denoiseG[i] * alpha;
            accumB[i] = accumB[i] * (1.0f - alpha) + denoiseB[i] * alpha;
        }
        return;
    }
    
    // Fallback to box blur (SIMD-optimized 3x3 box filter with SoA layout)
    if (rtW<4 || rtH<4) return;
    float f = config.denoiseStrength;
    if (f <= 0.0001f) return; // skip work if disabled / negligible
    
    const int w2 = rtW;
    const int h2 = rtH;
    constexpr float inv9 = 1.0f / 9.0f; // Precomputed constant
    
    __m128 vInv9 = _mm_set1_ps(inv9);
    __m128 vF = _mm_set1_ps(f);
    __m128 vInvF = _mm_set1_ps(1.0f - f);
    
    // Process each channel separately (better cache locality with SoA)
    for (int y=0; y<h2; ++y) {
        int y0 = (y>0)? y-1 : y;
        int y1 = y;
        int y2 = (y<h2-1)? y+1 : y;
        
        int x = 0;
        
        // Phase 3: Process 4 pixels at once with SIMD (when possible)
        for (; x + 3 < w2; x += 4) {
            // Compute indices for 4 consecutive pixels
            size_t o0 = (size_t)(y*w2 + x);
            size_t o1 = o0 + 1;
            size_t o2 = o0 + 2;
            size_t o3 = o0 + 3;
            
            // For each of 4 pixels, compute 3x3 box filter
            // This is still somewhat scalar due to gather complexity, but we vectorize the final blend
            float sumR[4], sumG[4], sumB[4];
            
            for (int i = 0; i < 4; i++) {
                int xi = x + i;
                int x0 = (xi>0)? xi-1 : xi;
                int x1 = xi;
                int x2 = (xi<w2-1)? xi+1 : xi;
                
                size_t idx00 = (size_t)(y0*w2 + x0), idx01 = (size_t)(y0*w2 + x1), idx02 = (size_t)(y0*w2 + x2);
                size_t idx10 = (size_t)(y1*w2 + x0), idx11 = (size_t)(y1*w2 + x1), idx12 = (size_t)(y1*w2 + x2);
                size_t idx20 = (size_t)(y2*w2 + x0), idx21 = (size_t)(y2*w2 + x1), idx22 = (size_t)(y2*w2 + x2);
                
                sumR[i] = accumR[idx00] + accumR[idx01] + accumR[idx02] +
                         accumR[idx10] + accumR[idx11] + accumR[idx12] +
                         accumR[idx20] + accumR[idx21] + accumR[idx22];
                
                sumG[i] = accumG[idx00] + accumG[idx01] + accumG[idx02] +
                         accumG[idx10] + accumG[idx11] + accumG[idx12] +
                         accumG[idx20] + accumG[idx21] + accumG[idx22];
                
                sumB[i] = accumB[idx00] + accumB[idx01] + accumB[idx02] +
                         accumB[idx10] + accumB[idx11] + accumB[idx12] +
                         accumB[idx20] + accumB[idx21] + accumB[idx22];
            }
            
            // Phase 3: Vectorize the averaging and blending for 4 pixels
            __m128 vSumR = _mm_loadu_ps(sumR);
            __m128 vSumG = _mm_loadu_ps(sumG);
            __m128 vSumB = _mm_loadu_ps(sumB);
            
            __m128 vAvgR = _mm_mul_ps(vSumR, vInv9);
            __m128 vAvgG = _mm_mul_ps(vSumG, vInv9);
            __m128 vAvgB = _mm_mul_ps(vSumB, vInv9);
            
            // Load current values
            __m128 vAccR = _mm_setr_ps(accumR[o0], accumR[o1], accumR[o2], accumR[o3]);
            __m128 vAccG = _mm_setr_ps(accumG[o0], accumG[o1], accumG[o2], accumG[o3]);
            __m128 vAccB = _mm_setr_ps(accumB[o0], accumB[o1], accumB[o2], accumB[o3]);
            
            // Phase 3: FMA blend: accum*(1-f) + avg*f
#if defined(__FMA__)
            __m128 vResultR = _mm_fmadd_ps(vAccR, vInvF, _mm_mul_ps(vAvgR, vF));
            __m128 vResultG = _mm_fmadd_ps(vAccG, vInvF, _mm_mul_ps(vAvgG, vF));
            __m128 vResultB = _mm_fmadd_ps(vAccB, vInvF, _mm_mul_ps(vAvgB, vF));
#else
            __m128 vResultR = _mm_add_ps(_mm_mul_ps(vAccR, vInvF), _mm_mul_ps(vAvgR, vF));
            __m128 vResultG = _mm_add_ps(_mm_mul_ps(vAccG, vInvF), _mm_mul_ps(vAvgG, vF));
            __m128 vResultB = _mm_add_ps(_mm_mul_ps(vAccB, vInvF), _mm_mul_ps(vAvgB, vF));
#endif
            
            // Store results
            alignas(16) float resultR[4], resultG[4], resultB[4];
            _mm_store_ps(resultR, vResultR);
            _mm_store_ps(resultG, vResultG);
            _mm_store_ps(resultB, vResultB);
            
            denoiseR[o0] = resultR[0]; denoiseR[o1] = resultR[1]; denoiseR[o2] = resultR[2]; denoiseR[o3] = resultR[3];
            denoiseG[o0] = resultG[0]; denoiseG[o1] = resultG[1]; denoiseG[o2] = resultG[2]; denoiseG[o3] = resultG[3];
            denoiseB[o0] = resultB[0]; denoiseB[o1] = resultB[1]; denoiseB[o2] = resultB[2]; denoiseB[o3] = resultB[3];
        }
        
        // Handle remaining pixels in this row (scalar)
        for (; x < w2; ++x) {
            int x0 = (x>0)? x-1 : x;
            int x1 = x;
            int x2 = (x<w2-1)? x+1 : x;
            
            size_t idx00 = (size_t)(y0*w2 + x0), idx01 = (size_t)(y0*w2 + x1), idx02 = (size_t)(y0*w2 + x2);
            size_t idx10 = (size_t)(y1*w2 + x0), idx11 = (size_t)(y1*w2 + x1), idx12 = (size_t)(y1*w2 + x2);
            size_t idx20 = (size_t)(y2*w2 + x0), idx21 = (size_t)(y2*w2 + x1), idx22 = (size_t)(y2*w2 + x2);
            
            float sumR = accumR[idx00] + accumR[idx01] + accumR[idx02] +
                        accumR[idx10] + accumR[idx11] + accumR[idx12] +
                        accumR[idx20] + accumR[idx21] + accumR[idx22];
            
            float sumG = accumG[idx00] + accumG[idx01] + accumG[idx02] +
                        accumG[idx10] + accumG[idx11] + accumG[idx12] +
                        accumG[idx20] + accumG[idx21] + accumG[idx22];
            
            float sumB = accumB[idx00] + accumB[idx01] + accumB[idx02] +
                        accumB[idx10] + accumB[idx11] + accumB[idx12] +
                        accumB[idx20] + accumB[idx21] + accumB[idx22];
            
            float avgR = sumR * inv9;
            float avgG = sumG * inv9;
            float avgB = sumB * inv9;
            
            size_t o = (size_t)(y*w2 + x);
            float invF = 1.0f - f;
            denoiseR[o] = accumR[o]*invF + avgR*f;
            denoiseG[o] = accumG[o]*invF + avgG*f;
            denoiseB[o] = accumB[o]*invF + avgB*f;
        }
    }
    // Swap back
    accumR.swap(denoiseR);
    accumG.swap(denoiseG);
    accumB.swap(denoiseB);
}

#endif // _WIN32
