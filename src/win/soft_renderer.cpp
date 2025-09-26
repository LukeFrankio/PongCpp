#ifdef _WIN32
// Ensure Windows headers do not define min/max macros that break std::max/std::min
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "soft_renderer.h"
#include <algorithm>
#include <cstring>
#include <cmath> // sqrt, tan, fabs, pow

// Simple XOR shift RNG for deterministic sampling
static inline uint32_t xorshift(uint32_t &s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

struct Vec3 { float x,y,z; };
static inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vec3 operator*(Vec3 a, float s){ return {a.x*s,a.y*s,a.z*s}; }
static inline Vec3 operator*(float s, Vec3 a){ return {a.x*s,a.y*s,a.z*s}; }
static inline Vec3 operator*(Vec3 a, Vec3 b){ return {a.x*b.x,a.y*b.y,a.z*b.z}; }
static inline Vec3 operator/(Vec3 a, float s){ return {a.x/s,a.y/s,a.z/s}; }
static inline float dot(Vec3 a, Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline Vec3 norm(Vec3 a){ float l=std::sqrt(dot(a,a)); return (l>1e-8f)?Vec3{a.x/l,a.y/l,a.z/l}:Vec3{0,0,0}; }
static inline Vec3 cross(Vec3 a, Vec3 b){ return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; }

// Scene description (very small & hard coded)
// Coordinate mapping: Game x in [0,gw] -> world X in [-2,2]
//                    Game y in [0,gh] -> world Y in [-1.5,1.5]
// Z axis depth into screen (camera looks +Z). Camera at z=-5, scene near z=0..+1.5

SoftRenderer::SoftRenderer() {
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
    haveHistory = false; std::fill(accum.begin(), accum.end(), 0.0f); std::fill(history.begin(), history.end(), 0.0f); frameCounter = 0;
}

void SoftRenderer::updateInternalResolution() {
    if (outW==0||outH==0) return;
    float scale = config.internalScalePct / 100.0f;
    rtW = std::max(8, int(outW * scale));
    rtH = std::max(8, int(outH * scale));
    accum.assign(rtW*rtH*3,0.0f);
    history.assign(rtW*rtH*3,0.0f);
    haveHistory = false;
}

// Ray primitive intersections
struct Hit { float t; Vec3 n; Vec3 pos; int mat; }; // mat: 0=diffuse wall,1=emissive,2=metal (paddles)

static bool intersectSphere(Vec3 ro, Vec3 rd, Vec3 c, float r, float tMax, Hit &hit, int mat) {
    Vec3 oc = ro - c;
    float b = dot(oc, rd);
    float cterm = dot(oc,oc) - r*r;
    float disc = b*b - cterm;
    if (disc < 0) return false;
    float s = std::sqrt(disc);
    float t = -b - s;
    if (t < 1e-3f) t = -b + s;
    if (t < 1e-3f || t > tMax) return false;
    hit.t = t; hit.pos = ro + rd * t; hit.n = norm(hit.pos - c); hit.mat = mat; return true;
}

static bool intersectPlane(Vec3 ro, Vec3 rd, Vec3 p, Vec3 n, float tMax, Hit &hit, int mat) {
    float denom = dot(rd,n);
    if (std::fabs(denom) < 1e-5f) return false;
    float t = dot(p - ro, n) / denom;
    if (t < 1e-3f || t > tMax) return false;
    hit.t=t; hit.pos=ro+rd*t; hit.n=(denom<0)?n: (n*-1.0f); hit.mat=mat; return true;
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
    if (!config.enablePathTracing) return; // nothing (caller can draw classic)
    if (rtW==0||rtH==0) return;

    // Map dynamic game objects to world
    float gw = (float)gs.gw, gh=(float)gs.gh;
    auto toWorld = [&](float gx, float gy)->Vec3 {
        float wx = (gx/gw - 0.5f)*4.0f;
        // Invert Y to match screen coordinate origin at top
        float wy = ((1.0f - gy/gh) - 0.5f)*3.0f;
        return {wx, wy, 0.0f};
    };
    Vec3 ballC = toWorld((float)gs.ball_x, (float)gs.ball_y);
    float ballR = 0.09f;
    // Paddles: width ~ 2 game units => (2/gw)*4 world units
    float paddleHalfX = (2.0f/gw)*4.0f*0.5f; // half
    float paddleHalfY = ((float)gs.paddle_h/gh)*3.0f*0.5f;
    Vec3 leftCenter = toWorld(2.0f, (float)gs.left_y + (float)gs.paddle_h*0.5f);
    Vec3 rightCenter = toWorld(gw-2.0f, (float)gs.right_y + (float)gs.paddle_h*0.5f);
    float paddleThickness = 0.05f;

    // Camera setup
    Vec3 camPos = {0,0,-5.0f};
    float fov = 60.0f * 3.1415926f/180.0f;
    float tanF = std::tan(fov*0.5f);

    // For each pixel (low-res) produce color
    frameCounter++;
    std::vector<float> hdr(rtW*rtH*3,0.0f);
    int pixels = rtW*rtH;
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
    for (int y=0; y<rtH; ++y) {
        for (int x=0; x<rtW; ++x) {
            Vec3 col{0,0,0};
            uint32_t seed = (x*1973) ^ (y*9277) ^ (frameCounter*26699u);
            for (int s=0; s<spp; ++s) {
                float rx = (x + (xorshift(seed)&1023)/1024.0f)/(float)rtW;
                float ry = (y + (xorshift(seed)&1023)/1024.0f)/(float)rtH;
                Vec3 rd; Vec3 ro;
                if (config.useOrtho) {
                    // Map pixel to world X/Y directly matching toWorld scaling ([-2,2] x [-1.5,1.5])
                    float wx = ((x + (xorshift(seed)&1023)/1024.0f)/(float)rtW - 0.5f)*4.0f;
                    float wy = (((rtH-1-y) + (xorshift(seed)&1023)/1024.0f)/(float)rtH - 0.5f)*3.0f; // invert to match screen space
                    ro = { wx, wy, -1.0f }; // start slightly in front of scene origin plane
                    rd = { 0,0,1 };         // shoot straight forward
                } else {
                    float px = (2*rx -1)*tanF * (float)rtW/(float)rtH;
                    float py = (1-2*ry)*tanF;
                    rd = norm(Vec3{px,py,1});
                    ro = camPos;
                }
                Vec3 throughput{1,1,1};
                for (int bounce=0; bounce<config.maxBounces; ++bounce) {
                    Hit best; best.t=1e30f; bool hit=false;
                    // Walls: top/bottom (diffuse)
                    Hit tmp;
                    if (intersectPlane(ro,rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                    if (intersectPlane(ro,rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                    // Back plane (soft grey)
                    if (intersectPlane(ro,rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; hit=true; }
                    // Ball emissive
                    if (intersectSphere(ro,rd,ballC,ballR,best.t,tmp,1)){ best=tmp; hit=true; }
                    // Metallic paddles (mat=2)
                    if (intersectBox(ro,rd, leftCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, leftCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                    if (intersectBox(ro,rd, rightCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, rightCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, best.t, tmp, 2)){ best=tmp; hit=true; }
                    if (!hit) {
                        // background gradient
                        float t = 0.5f*(rd.y+1.0f);
                        Vec3 bg = (1.0f-t)*Vec3{0.05f,0.07f,0.10f} + t*Vec3{0.02f,0.02f,0.04f};
                        col = col + throughput * bg;
                        break;
                    }
                    if (best.mat==1) { // emissive ball
                        Vec3 emit{2.2f,1.4f,0.8f};
                        emit = emit * config.emissiveIntensity;
                        col = col + throughput * emit;
                        break; // light ends path
                    }
                    if (best.mat==0) { // diffuse wall
                        // cosine hemisphere sample
                        Vec3 n = best.n;
                        float r1 = 2*3.1415926f * ((xorshift(seed)&1023)/1024.0f);
                        float r2 = (xorshift(seed)&1023)/1024.0f;
                        float r2s = std::sqrt(r2);
                        // build basis
                        Vec3 w = n;
                        Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0,1,0} : Vec3{1,0,0};
                        Vec3 v = norm(cross(w,a));
                        Vec3 u = cross(v,w);
                        Vec3 d = norm( u * (std::cos(r1)*r2s) + v*(std::sin(r1)*r2s) + w*std::sqrt(1-r2) );
                        ro = best.pos + best.n*0.002f;
                        rd = d;
                        throughput = throughput * Vec3{0.55f,0.55f,0.58f};
                        continue;
                    }
                    if (best.mat==2) { // metallic paddle: perfect reflection + roughness parameter
                        Vec3 n = best.n;
                        // perfect reflection direction
                        float cosi = dot(rd, n);
                        rd = rd - n * (2.0f * cosi);
                        // add a small cosine-weighted perturbation for roughness
                        float rough = config.metallicRoughness;
                        float r1 = 2*3.1415926f*((xorshift(seed)&1023)/1024.0f);
                        float r2 = (xorshift(seed)&1023)/1024.0f;
                        float r2s = std::sqrt(r2);
                        Vec3 w = norm(n);
                        Vec3 a = (std::fabs(w.x) > 0.1f) ? Vec3{0,1,0} : Vec3{1,0,0};
                        Vec3 v = norm(cross(w,a));
                        Vec3 u = cross(v,w);
                        Vec3 fuzz = norm(u*(std::cos(r1)*r2s) + v*(std::sin(r1)*r2s) + w*std::sqrt(1-r2));
                        rd = norm(rd*(1.0f-rough) + fuzz*rough);
                        ro = best.pos + rd*0.002f;
                        // metallic tint (cool steel)
                        throughput = throughput * Vec3{0.85f,0.88f,0.95f};
                        continue;
                    }
                }
            }
            col = col / (float)spp;
            hdr[(y*rtW + x)*3 + 0] = col.x;
            hdr[(y*rtW + x)*3 + 1] = col.y;
            hdr[(y*rtW + x)*3 + 2] = col.z;
        }
    }

    // Temporal accumulation (exponential moving average) then simple spatial denoise
    temporalAccumulate(hdr);
    spatialDenoise();

    // Upscale (nearest) + tone map to outW/outH pixel32
    for (int y=0; y<outH; ++y) {
        int sy = std::min(rtH-1, (int)((float)y/outH * rtH));
        for (int x=0; x<outW; ++x) {
            int sx = std::min(rtW-1, (int)((float)x/outW * rtW));
            float r = accum[(sy*rtW+sx)*3+0];
            float g = accum[(sy*rtW+sx)*3+1];
            float b = accum[(sy*rtW+sx)*3+2];
            // simple tone map + gamma
            auto tm = [](float c){ c = c/(1.0f+c); return std::pow(std::max(0.0f,c), 1.0f/2.2f); };
            r = tm(r); g = tm(g); b = tm(b);
            uint8_t R = (uint8_t)std::min(255.0f, r*255.0f + 0.5f);
            uint8_t G = (uint8_t)std::min(255.0f, g*255.0f + 0.5f);
            uint8_t B = (uint8_t)std::min(255.0f, b*255.0f + 0.5f);
            pixel32[y*outW + x] = (0xFFu<<24) | (R<<16) | (G<<8) | (B);
        }
    }
}

void SoftRenderer::toneMapAndPack() {
    // (unused currently; left for potential accumulation variant)
}

void SoftRenderer::temporalAccumulate(const std::vector<float>& cur) {
    // blend current into accum: accum = lerp(accum, cur, alpha)
    float alpha = config.accumAlpha;
    if (!haveHistory) {
        accum = cur; haveHistory = true; return;
    }
    size_t n = accum.size();
    for (size_t i=0;i<n;i++) accum[i] = accum[i]*(1.0f-alpha) + cur[i]*alpha;
}

void SoftRenderer::spatialDenoise() {
    // 3x3 box filter (single pass) on accum into history, then swap back
    if (rtW<4 || rtH<4) return;
    history.resize(rtW*rtH*3);
    auto idx = [&](int x,int y){ return (y*rtW + x)*3; };
    for (int y=0;y<rtH;y++) {
        for (int x=0;x<rtW;x++) {
            float sum[3]={0,0,0}; int cnt=0;
            for (int dy=-1;dy<=1;dy++) {
                int yy = y+dy; if (yy<0||yy>=rtH) continue;
                for (int dx=-1;dx<=1;dx++) {
                    int xx = x+dx; if (xx<0||xx>=rtW) continue;
                    size_t id = idx(xx,yy);
                    sum[0]+=accum[id+0]; sum[1]+=accum[id+1]; sum[2]+=accum[id+2]; cnt++;
                }
            }
            size_t o = idx(x,y);
            float avg0=sum[0]/cnt, avg1=sum[1]/cnt, avg2=sum[2]/cnt;
            float f = config.denoiseStrength;
            history[o+0]=accum[o+0]*(1.0f-f)+avg0*f;
            history[o+1]=accum[o+1]*(1.0f-f)+avg1*f;
            history[o+2]=accum[o+2]*(1.0f-f)+avg2*f;
        }
    }
    accum.swap(history);
}

#endif // _WIN32
