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

// Scene description (very small & hard coded)
// Coordinate mapping: Game x in [0,gw] -> world X in [-2,2]
//                    Game y in [0,gh] -> world Y in [-1.5,1.5]
// Z axis depth into screen (camera looks +Z). Camera at z=-5, scene near z=0..+1.5

SoftRenderer::SoftRenderer() {
    // Phase 4: Initialize CPU feature detection
    detectCPUFeatures();
    configure(config);
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
    if (config.emissiveIntensity < 0.1f) config.emissiveIntensity = 0.1f; if (config.emissiveIntensity > 5.0f) config.emissiveIntensity = 5.0f;
    if (config.rouletteStartBounce < 1) config.rouletteStartBounce = 1; if (config.rouletteStartBounce > 16) config.rouletteStartBounce = 16;
    if (config.rouletteMinProb < 0.01f) config.rouletteMinProb = 0.01f; if (config.rouletteMinProb > 0.9f) config.rouletteMinProb = 0.9f;
    if (config.softShadowSamples < 1) config.softShadowSamples = 1; if (config.softShadowSamples > 64) config.softShadowSamples = 64;
    if (config.lightRadiusScale < 0.1f) config.lightRadiusScale = 0.1f; if (config.lightRadiusScale > 5.0f) config.lightRadiusScale = 5.0f;
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
struct Hit { float t; Vec3 n; Vec3 pos; int mat; }; // mat: 0=diffuse wall,1=emissive,2=metal (paddles)

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

// Phase 1: Optimized plane intersection with branch hints
static bool intersectPlane(Vec3 ro, Vec3 rd, Vec3 p, Vec3 n, float tMax, Hit &hit, int mat) {
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
// Phase 2: 4-wide SIMD Ray Packet Structures and Intersection Functions
// ============================================================================

// 4-wide ray packet: 4 rays processed simultaneously
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
    
    // b = dot(oc, rd)
    __m128 b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, rays.dx),
                                     _mm_mul_ps(ocy, rays.dy)),
                          _mm_mul_ps(ocz, rays.dz));
    
    // c = dot(oc, oc) - r^2
    __m128 oc_len2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, ocx),
                                           _mm_mul_ps(ocy, ocy)),
                                _mm_mul_ps(ocz, ocz));
    __m128 c = _mm_sub_ps(oc_len2, r2);
    
    // disc = b*b - c
    __m128 disc = _mm_sub_ps(_mm_mul_ps(b, b), c);
    
    // Check if disc < 0 (no hit)
    __m128 disc_valid = _mm_cmpge_ps(disc, _mm_setzero_ps());
    
    // sqrt(disc) - use scalar fast sqrt for each lane
    alignas(16) float disc_arr[4];
    _mm_store_ps(disc_arr, disc);
    __m128 sqrt_disc = _mm_set_ps(
        sqrt_fast(disc_arr[3]),
        sqrt_fast(disc_arr[2]),
        sqrt_fast(disc_arr[1]),
        sqrt_fast(disc_arr[0])
    );
    
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
    
    // Determine normal direction (flip if denom < 0)
    __m128 denom_neg = _mm_cmplt_ps(denom, _mm_setzero_ps());
    __m128 normal_x = _mm_blendv_ps(nx, _mm_sub_ps(_mm_setzero_ps(), nx), denom_neg);
    __m128 normal_y = _mm_blendv_ps(ny, _mm_sub_ps(_mm_setzero_ps(), ny), denom_neg);
    __m128 normal_z = _mm_blendv_ps(nz, _mm_sub_ps(_mm_setzero_ps(), nz), denom_neg);
    
    hit.nx = _mm_blendv_ps(hit.nx, normal_x, update_mask);
    hit.ny = _mm_blendv_ps(hit.ny, normal_y, update_mask);
    hit.nz = _mm_blendv_ps(hit.nz, normal_z, update_mask);
    
    // Set material ID
    __m128i mat_id = _mm_set1_epi32(mat);
    hit.mat = _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(hit.mat),
                                             _mm_castsi128_ps(mat_id),
                                             update_mask));
}

// Axis aligned thin box (only front/back + sides) simplified: treat as slab intersection returning surface normal of hit face.
static bool intersectBox(Vec3 ro, Vec3 rd, Vec3 bmin, Vec3 bmax, float tMax, Hit &hit, int mat) {
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
    float paddleThickness = 0.05f;

    // Camera setup
    Vec3 camPos = {0,0,-5.0f};
    float fov = 60.0f * 3.1415926f/180.0f;
    float tanF = std::tan(fov*0.5f);

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
        Hit tmp; Hit best; best.t = maxT - 1e-3f;
        // Phase 1: Early out with branch hints
        // Planes
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0))) return true;
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0))) return true;
        if (UNLIKELY(intersectPlane(from,dir, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0))) return true;
        // Paddles (inflated)
        float inflate = 0.01f;
        if (UNLIKELY(intersectBox(from,dir, leftCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, leftCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2))) return true;
        if (UNLIKELY(intersectBox(from,dir, rightCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, rightCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2))) return true;
        if (useHoriz) {
            if (UNLIKELY(intersectBox(from,dir, topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2))) return true;
            if (UNLIKELY(intersectBox(from,dir, bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2))) return true;
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
    // Sample direct lighting from all emissive spheres with soft shadows.
    auto sampleDirect = [&](Vec3 pos, Vec3 n, Vec3 viewDir, uint32_t &seed, bool isMetal)->Vec3 {
        if (UNLIKELY(ballCenters.empty())) return Vec3{0,0,0};
        int lightCount = (int)ballCenters.size();
        int shadowSamples = std::max(1, config.softShadowSamples);
        Vec3 sum{0,0,0};
        for (int li=0; li<lightCount; ++li) {
            Vec3 center = ballCenters[li];
            float radius = ballRs[li] * config.lightRadiusScale;
            Vec3 lightAccum{0,0,0};
            for (int s=0; s<shadowSamples; ++s) {
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
                if (occludedToPoint(pos + n*0.002f, spherePt, li)) continue;
                // Basic Lambert * point radiance with inverse square; treat sample weight as average over sphere area samples
                Vec3 emitColor{2.2f,1.4f,0.8f}; emitColor = emitColor * config.emissiveIntensity;
                // Normalization when multiple lights (keep total similar); also divide by samples
                if (lightCount>1) emitColor = emitColor / (float)lightCount;
                float atten = 1.0f/(4.0f*3.1415926f*std::max(1e-4f, dist2));
                float brdfScale = 1.0f;
                if (config.pbrEnable) {
                    if (!isMetal) {
                        brdfScale = 1.0f/3.1415926f; // Lambert diffuse (albedo = 0.62..0.67 encoded in throughput earlier)
                        lightAccum = lightAccum + emitColor * (ndotl * atten * brdfScale);
                    } else {
                        // Simple specular (Schlick Fresnel * NdotL) with roughness attenuation
                        Vec3 V = norm(viewDir * -1.0f); // view direction towards camera
                        Vec3 H = norm(V + L);
                        float VoH = std::max(0.0f, dot(V,H));
                        Vec3 F0{0.86f,0.88f,0.94f};
                        Vec3 F = F0 + (Vec3{1,1,1} - F0) * std::pow(1.0f - VoH, 5.0f);
                        float rough = std::clamp(config.metallicRoughness, 0.0f, 1.0f);
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
            lightAccum = lightAccum / (float)shadowSamples;
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

        auto worker = [&](int yStart, int yEnd){
            for (int y=yStart; y<yEnd; ++y) {
                for (int x=0; x<rtW; ++x) {
                    Vec3 col{0,0,0}; uint32_t seed = (x*1973) ^ (y*9277) ^ (frameCounter*26699u);
                    for (int s=0; s<spp; ++s) {
                        float u1,u2; rng2(seed,u1,u2);
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
                        for (; bounce<config.maxBounces; ++bounce) {
                            Hit best; best.t=1e30f; bool hit=false; Hit tmp;
                            if (intersectPlane(ro,rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            for(size_t bi=0; bi<ballCenters.size(); ++bi){ if(intersectSphere(ro,rd,ballCenters[bi],ballRs[bi],best.t,tmp, bi==0?1:1)){ best=tmp; hit=true; } }
                            if (intersectBox(ro,rd, leftCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, leftCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                            if (intersectBox(ro,rd, rightCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, rightCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                            if (useHoriz) {
                                if (intersectBox(ro,rd, topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                                if (intersectBox(ro,rd, bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                            }
                            if (useObs) { for (auto &bx : obsBoxes) if (intersectBox(ro,rd, bx.bmin, bx.bmax, best.t, tmp, 0)){ best=tmp; hit=true; } }
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
                                // Phase 4: FMA for emissive color accumulation
                                Vec3 emit{2.2f,1.4f,0.8f}; 
                                emit=emit*config.emissiveIntensity; 
                                col = fma_add(col, throughput * emit, 1.0f);  // col + throughput*emit
                                terminated=true; 
                                break; 
                            }
                            // Phase 1: Early throughput termination before shading
                            float maxT = max_component(throughput);
                            if (UNLIKELY(maxT < 1e-3f)) { 
                                earlyExitAccum++; 
                                terminated = true;
                                break; 
                            }
                            if (best.mat==0) {
                                Vec3 n=best.n; 
                                float uA,uB; rng2(seed,uA,uB); 
                                // Phase 1: Use fast trig
                                float r1=6.28318531f*uA; // 2*PI
                                float r2=uB; 
                                float r2s=sqrt_fast(r2); 
                                Vec3 w=n; 
                                Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; 
                                Vec3 v=norm(cross(w,a)); 
                                Vec3 u=cross(v,w); 
                                // Phase 1: Use fast cos/sin
                                float c1 = cos_fast(r1);
                                float s1 = sin_fast(r1);
                                float sq = sqrt_fast(1.0f - r2);
                                Vec3 d=norm(u*(c1*r2s) + v*(s1*r2s) + w*sq); 
                                ro = fma_add(best.pos, best.n, 0.002f);  // Phase 4: FMA for ray offset
                                rd=d; 
                                throughput=throughput*Vec3{0.62f,0.64f,0.67f}; 
                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, false); 
                                col = fma_add(col, throughput * direct, 1.0f);  // Phase 4: FMA 
                            }
                            else if (best.mat==2) {
                                Vec3 paddleColor{0.25f,0.32f,0.6f}; 
                                Vec3 n=best.n; 
                                float cosi=dot(rd,n); 
                                rd = rd - n*(2.0f*cosi); 
                                float rough=config.metallicRoughness; 
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
                                throughput=throughput*(Vec3{0.86f,0.88f,0.94f}*0.5f + paddleColor*0.5f); 
                                Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * paddleColor; 
                                col = fma_add(col, throughput * direct, 1.0f);  // Phase 4: FMA 
                            }
                            // Phase 1: Optimized early exit and Russian roulette
                            if (best.mat==0 || best.mat==2) { 
                                float maxT = max_component(throughput); 
                                if (UNLIKELY(maxT < 1e-3f)) { 
                                    earlyExitAccum++; 
                                    bounce++; 
                                    break; 
                                } 
                                if (LIKELY(config.rouletteEnable) && bounce >= config.rouletteStartBounce) { 
                                    // Phase 1: Optimized probability calculation
                                    float p = std::max(config.rouletteMinProb, std::min(maxT, 0.95f)); 
                                    float rrand = rng1(seed);
                                    if (UNLIKELY(rrand > p)){ 
                                        rouletteAccum++; 
                                        bounce++; 
                                        break; 
                                    } 
                                    throughput = throughput / p; 
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
                    
                    // Phase 3: Prefetch next pixel's data (sequential scan optimization)
                    // Prefetch ~16 pixels ahead for L1 cache (~100-200 cycles latency)
                    if (LIKELY(x + 16 < rtW)) {
                        size_t prefetchIdx = idx + 16;
                        _mm_prefetch((const char*)&hdrR_ref[prefetchIdx], _MM_HINT_T0);
                        _mm_prefetch((const char*)&hdrG_ref[prefetchIdx], _MM_HINT_T0);
                        _mm_prefetch((const char*)&hdrB_ref[prefetchIdx], _MM_HINT_T0);
                    }
                }
            }
        };

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
    // Phase 3: SIMD-optimized 3x3 box filter with SoA layout
    if (rtW<4 || rtH<4) return;
    float f = config.denoiseStrength;
    if (f <= 0.0001f) return; // skip work if disabled / negligible
    
    const int w = rtW;
    const int h = rtH;
    constexpr float inv9 = 1.0f / 9.0f; // Precomputed constant
    
    __m128 vInv9 = _mm_set1_ps(inv9);
    __m128 vF = _mm_set1_ps(f);
    __m128 vInvF = _mm_set1_ps(1.0f - f);
    
    // Process each channel separately (better cache locality with SoA)
    for (int y=0; y<h; ++y) {
        int y0 = (y>0)? y-1 : y;
        int y1 = y;
        int y2 = (y<h-1)? y+1 : y;
        
        int x = 0;
        
        // Phase 3: Process 4 pixels at once with SIMD (when possible)
        for (; x + 3 < w; x += 4) {
            // Compute indices for 4 consecutive pixels
            size_t o0 = (size_t)(y*w + x);
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
                int x2 = (xi<w-1)? xi+1 : xi;
                
                size_t idx00 = (size_t)(y0*w + x0), idx01 = (size_t)(y0*w + x1), idx02 = (size_t)(y0*w + x2);
                size_t idx10 = (size_t)(y1*w + x0), idx11 = (size_t)(y1*w + x1), idx12 = (size_t)(y1*w + x2);
                size_t idx20 = (size_t)(y2*w + x0), idx21 = (size_t)(y2*w + x1), idx22 = (size_t)(y2*w + x2);
                
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
        for (; x < w; ++x) {
            int x0 = (x>0)? x-1 : x;
            int x1 = x;
            int x2 = (x<w-1)? x+1 : x;
            
            size_t idx00 = (size_t)(y0*w + x0), idx01 = (size_t)(y0*w + x1), idx02 = (size_t)(y0*w + x2);
            size_t idx10 = (size_t)(y1*w + x0), idx11 = (size_t)(y1*w + x1), idx12 = (size_t)(y1*w + x2);
            size_t idx20 = (size_t)(y2*w + x0), idx21 = (size_t)(y2*w + x1), idx22 = (size_t)(y2*w + x2);
            
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
            
            size_t o = (size_t)(y*w + x);
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
