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
#include <condition_variable>
#include <functional>
#include <array>

// Adaptive threading state (global to translation unit)
static std::atomic<float> g_srLastFrameMs{ 1000.0f };      // raw last frame
static std::atomic<float> g_srEmaFrameMs{ 1000.0f };       // smoothed (EMA) frame time
static std::atomic<unsigned> g_srAdaptiveThreads{ 0 };     // current chosen threads
static std::atomic<unsigned> g_srLastLogged{ 0 };          // last logged value
static std::atomic<bool> g_srInitialized{ false };
static std::atomic<int>  g_srCooldown{ 0 };                // frames to wait before another downward adjustment

// Simple persistent thread pool for row/tile dispatch
struct SRThreadPool {
    std::vector<std::thread> threads;
    std::mutex m;
    std::condition_variable cvWork;
    std::condition_variable cvDone;
    bool stop = false;
    bool haveWork = false;
    int activeWorkers = 0;          // workers participating in this frame
    std::atomic<int> nextRow{0};
    int totalRows = 0;
    std::function<void(int)> rowFunc; // processes a single row

    void ensureSize(unsigned want) {
        if (threads.size() == want) return;
        // teardown
        stop = true;
        cvWork.notify_all();
        for (auto &t: threads) if (t.joinable()) t.join();
        threads.clear();
        stop = false;
        if (want <= 1) return; // single-thread fallback
        threads.reserve(want-1); // main thread counts as one
        for (unsigned i=0;i<want-1;++i) {
            threads.emplace_back([this]{
                std::unique_lock<std::mutex> lk(m);
                while (true) {
                    cvWork.wait(lk, [&]{ return stop || haveWork; });
                    if (stop) break;
                    lk.unlock();
                    // work loop
                    while (true) {
                        int y = nextRow.fetch_add(1, std::memory_order_relaxed);
                        if (y >= totalRows) break;
                        rowFunc(y);
                    }
                    lk.lock();
                    if (--activeWorkers == 0) { haveWork = false; cvDone.notify_one(); }
                }
            });
        }
    }

    void dispatch(unsigned workers, int rows, const std::function<void(int)> &fn) {
        ensureSize(workers);
        if (workers <= 1) { // run inline
            for (int y=0;y<rows;++y) fn(y);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m);
            rowFunc = fn;
            totalRows = rows;
            nextRow.store(0, std::memory_order_relaxed);
            activeWorkers = (int)workers - 1; // excluding main thread which will also run
            haveWork = true;
        }
        cvWork.notify_all();
        // Main thread participates
        while (true) {
            int y = nextRow.fetch_add(1, std::memory_order_relaxed);
            if (y >= rows) break;
            fn(y);
        }
        // Wait for workers
        std::unique_lock<std::mutex> lk(m);
        cvDone.wait(lk, [&]{ return !haveWork; });
    }

    ~SRThreadPool(){
        stop = true; haveWork = true; cvWork.notify_all();
        for(auto &t: threads) if(t.joinable()) t.join();
    }
};

static SRThreadPool g_srPool; // global pool instance


// Simple XOR shift RNG for deterministic sampling
static inline uint32_t xorshift(uint32_t &s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

// Generate two uniform floats in [0,1) from a single RNG advance (packs into 16-bit halves)
static inline void rng2(uint32_t &seed, float &u, float &v){
    uint32_t r = xorshift(seed);
    u = (r & 0xFFFF) * (1.0f/65536.0f);
    v = (r >> 16)    * (1.0f/65536.0f);
}

// Vec3 defined in header
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
// -------------------- BVH Utilities (persistent) --------------------
struct AABB { Vec3 bmin; Vec3 bmax; };
static inline AABB merge(const AABB&a,const AABB&b){ return { { std::min(a.bmin.x,b.bmin.x), std::min(a.bmin.y,b.bmin.y), std::min(a.bmin.z,b.bmin.z) }, { std::max(a.bmax.x,b.bmax.x), std::max(a.bmax.y,b.bmax.y), std::max(a.bmax.z,b.bmax.z) } }; }
static inline Vec3 center(const AABB&a){ return {(a.bmin.x+a.bmax.x)*0.5f,(a.bmin.y+a.bmax.y)*0.5f,(a.bmin.z+a.bmax.z)*0.5f}; }
static inline bool intersectAABB(Vec3 ro, Vec3 rd, const AABB& box, float &tMax){
    float tmin=0.0f, tmax=tMax;
    for(int i=0;i<3;i++){
        float o = (i==0)?ro.x: (i==1)?ro.y: ro.z;
        float d = (i==0)?rd.x: (i==1)?rd.y: rd.z;
        float mn = (i==0)?box.bmin.x: (i==1)?box.bmin.y: box.bmin.z;
        float mx = (i==0)?box.bmax.x: (i==1)?box.bmax.y: box.bmax.z;
        if (std::fabs(d) < 1e-8f) { if (o < mn || o > mx) return false; }
        else {
            float inv = 1.0f/d; float t1 = (mn - o)*inv; float t2 = (mx - o)*inv; if (t1>t2) std::swap(t1,t2); tmin = t1>tmin? t1:tmin; tmax = t2<tmax? t2:tmax; if (tmin>tmax) return false; }
    }
    tMax = tmax; return true;
}
// Member functions to build / refit persistent BVH
void SoftRenderer::refitBVHNode(int idx){
    if (idx < 0 || idx >= (int)bvhNodes.size()) return;
    BVHNode &n = bvhNodes[idx];
    if (n.primCount>0){
        for(int k=0;k<3;k++){ n.bmin[k]=1e30f; n.bmax[k]=-1e30f; }
        for(int i=0;i<n.primCount;i++){
            const BVHPrim &p = bvhPrims[n.primStart+i];
            for(int k=0;k<3;k++){ n.bmin[k]=std::min(n.bmin[k],p.bmin[k]); n.bmax[k]=std::max(n.bmax[k],p.bmax[k]); }
        }
        return;
    }
    if (n.left!=-1) refitBVHNode(n.left);
    if (n.right!=-1) refitBVHNode(n.right);
    for(int k=0;k<3;k++){
        float mn = 1e30f, mx = -1e30f;
        if (n.left!=-1){ mn = std::min(mn, bvhNodes[n.left].bmin[k]); mx = std::max(mx, bvhNodes[n.left].bmax[k]); }
        if (n.right!=-1){ mn = std::min(mn, bvhNodes[n.right].bmin[k]); mx = std::max(mx, bvhNodes[n.right].bmax[k]); }
        n.bmin[k]=mn; n.bmax[k]=mx;
    }
}

namespace { // anonymous to limit scope
static void buildBVHRecursive(std::vector<SoftRenderer::BVHNode>& nodes, std::vector<SoftRenderer::BVHPrim>& prims, int start, int count){
    SoftRenderer::BVHNode node{}; for(int k=0;k<3;k++){ node.bmin[k]=prims[start].bmin[k]; node.bmax[k]=prims[start].bmax[k]; }
    for(int i=1;i<count;i++) for(int k=0;k<3;k++){ node.bmin[k]=std::min(node.bmin[k],prims[start+i].bmin[k]); node.bmax[k]=std::max(node.bmax[k],prims[start+i].bmax[k]); }
    if (count <= 2){ node.primStart=start; node.primCount=count; node.left=node.right=-1; nodes.push_back(node); return; }
    float ex=node.bmax[0]-node.bmin[0], ey=node.bmax[1]-node.bmin[1], ez=node.bmax[2]-node.bmin[2]; int axis=(ex>ey&&ex>ez)?0:(ey>ez?1:2);
    int mid=start+count/2;
    std::nth_element(prims.begin()+start, prims.begin()+mid, prims.begin()+start+count, [&](const SoftRenderer::BVHPrim&a,const SoftRenderer::BVHPrim&b){ return (a.bmin[axis]+a.bmax[axis]) < (b.bmin[axis]+b.bmax[axis]); });
    int idx=(int)nodes.size(); nodes.push_back(node); int leftIdx=(int)nodes.size(); buildBVHRecursive(nodes,prims,start,mid-start); int rightIdx=(int)nodes.size(); buildBVHRecursive(nodes,prims,mid,count-(mid-start)); nodes[idx].left=leftIdx; nodes[idx].right=rightIdx; }
} // namespace

void SoftRenderer::buildOrRefitBVH(const GameState& gs, const std::vector<Vec3>& ballCenters, const std::vector<float>& ballRs,
                          Vec3 leftCenter, Vec3 rightCenter, Vec3 topCenter, Vec3 bottomCenter,
                          float paddleHalfX, float paddleHalfY, float paddleThickness,
                          float horizHalfX, float horizHalfY, float horizThickness,
                          bool useHoriz, const std::vector<std::pair<Vec3,Vec3>>& obsBoxes, bool useObs){
    // Build primitive list each frame (dynamic positions). Refit nodes afterwards.
    this->bvhPrims.clear(); this->bvhPrims.reserve(ballCenters.size()+8+obsBoxes.size());
    auto pushPrim=[&](Vec3 mn, Vec3 mx, int id, int mat){ BVHPrim p; p.id=id; p.mat=mat; p.bmin[0]=mn.x; p.bmin[1]=mn.y; p.bmin[2]=mn.z; p.bmax[0]=mx.x; p.bmax[1]=mx.y; p.bmax[2]=mx.z; this->bvhPrims.push_back(p); };
    for(size_t i=0;i<ballCenters.size();++i){ Vec3 c=ballCenters[i]; float r=ballRs[i]; pushPrim(c-Vec3{r,r,r}, c+Vec3{r,r,r}, (int)i, 1); }
    pushPrim(leftCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, leftCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, 100, 2);
    pushPrim(rightCenter - Vec3{paddleHalfX,paddleHalfY,paddleThickness}, rightCenter + Vec3{paddleHalfX,paddleHalfY,paddleThickness}, 101, 2);
    if (useHoriz){
        pushPrim(topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, 102, 2);
        pushPrim(bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, 103, 2);
    }
    if (useObs){ for(auto &bx: obsBoxes) pushPrim(bx.first, bx.second, 200, 0); }
    // Add planes as large thin boxes (ids 1000+)
    pushPrim(Vec3{-4, 1.55f,-2}, Vec3{4,1.65f,2}, 1000,0); // top plane
    pushPrim(Vec3{-4,-1.65f,-2}, Vec3{4,-1.55f,2}, 1001,0); // bottom plane
    pushPrim(Vec3{-4,-2, 1.75f}, Vec3{4,2,1.85f}, 1002,0);  // back plane

    size_t sig = this->bvhPrims.size();
    auto tStart = std::chrono::high_resolution_clock::now();
    if (!this->bvhBuilt || sig != this->prevPrimSignature){
        this->bvhNodes.clear(); this->bvhNodes.reserve(this->bvhPrims.size()*2);
        std::vector<BVHPrim> temp = this->bvhPrims; // copy for sorting only
        buildBVHRecursive(this->bvhNodes,temp,0,(int)temp.size());
        this->bvhBuilt = true; this->prevPrimSignature = sig;
    } else {
        // Refit bounds
        this->refitBVHNode(0);
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    stats_.bvhNodeCount = (int)this->bvhNodes.size();
    stats_.msBVH = std::chrono::duration<float,std::milli>(tEnd - tStart).count();
}
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
    haveHistory = false; std::fill(accum.begin(), accum.end(), 0.0f); std::fill(history.begin(), history.end(), 0.0f); frameCounter = 0;
}

void SoftRenderer::updateInternalResolution() {
    if (outW==0||outH==0) return;
    float scale = config.internalScalePct / 100.0f;
    rtW = std::max(8, int(outW * scale));
    rtH = std::max(8, int(outH * scale));
    accum.assign(rtW*rtH*3,0.0f);
    history.assign(rtW*rtH*3,0.0f);
    firstHitNormal.assign(rtW*rtH*3,0.0f);
    firstHitAlbedo.assign(rtW*rtH*3,0.0f);
    prevFirstHitNormal.assign(rtW*rtH*3,0.0f);
    prevFirstHitAlbedo.assign(rtW*rtH*3,0.0f);
    varianceAccum.assign(rtW*rtH*2,0.0f); // store sum and sumSq for luminance
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
        Vec3 dir = to - from; float maxT = std::sqrt(std::max(0.0f, dot(dir,dir))); if (maxT < 1e-4f) return false; dir = dir / maxT;
        Hit tmp; Hit best; best.t = maxT - 1e-3f;
        // Planes
        if (intersectPlane(from,dir, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)) return true;
        if (intersectPlane(from,dir, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)) return true;
        if (intersectPlane(from,dir, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)) return true;
        // Paddles (inflated)
        float inflate = 0.01f;
        if (intersectBox(from,dir, leftCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, leftCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2)) return true;
        if (intersectBox(from,dir, rightCenter - Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, rightCenter + Vec3{paddleHalfX+inflate,paddleHalfY+inflate,paddleThickness+inflate}, best.t, tmp, 2)) return true;
        if (useHoriz) {
            if (intersectBox(from,dir, topCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, topCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)) return true;
            if (intersectBox(from,dir, bottomCenter - Vec3{horizHalfX,horizHalfY,horizThickness}, bottomCenter + Vec3{horizHalfX,horizHalfY,horizThickness}, best.t, tmp, 2)) return true;
        }
        if (useObs) {
            for (auto &b : obsBoxes) if (intersectBox(from,dir, b.bmin, b.bmax, best.t, tmp, 0)) return true;
        }
        // Other spheres block (soft shadows & inter-light occlusion)
        for (size_t si=0; si<ballCenters.size(); ++si) {
            if ((int)si == ignoreSphere) continue; // don't self-shadow chosen sample sphere
            if (intersectSphere(from,dir, ballCenters[si], ballRs[si]*config.lightRadiusScale, best.t, tmp, 1)) return true;
        }
        return false;
    };
    // Sample direct lighting from all emissive spheres with soft shadows.
    auto sampleDirect = [&](Vec3 pos, Vec3 n, Vec3 viewDir, uint32_t &seed, bool isMetal)->Vec3 {
        if (ballCenters.empty()) return Vec3{0,0,0};
        int lightCount = (int)ballCenters.size();
        int shadowSamples = std::max(1, config.softShadowSamples);
        Vec3 sum{0,0,0};
        for (int li=0; li<lightCount; ++li) {
            Vec3 center = ballCenters[li];
            float radius = ballRs[li] * config.lightRadiusScale;
            Vec3 lightAccum{0,0,0};
            for (int s=0; s<shadowSamples; ++s) {
                // Uniform point on sphere surface (normal distribution via sphere point picking)
                float u1 = (xorshift(seed)&1023)/1024.0f;
                float u2 = (xorshift(seed)&1023)/1024.0f;
                float z = 1.0f - 2.0f*u1; // cosine of polar angle
                float rxy = std::sqrt(std::max(0.0f, 1.0f - z*z));
                float phi = 2.0f*3.1415926f*u2;
                Vec3 spherePt = center + Vec3{rxy*std::cos(phi), rxy*std::sin(phi), z} * radius;
                Vec3 L = spherePt - pos; float dist2 = dot(L,L); float dist = (dist2>1e-6f)? std::sqrt(dist2):0.0f; if (dist < 1e-6f) continue; L = L / dist;
                float ndotl = std::max(0.0f, dot(n,L)); if (ndotl <= 0.0f) continue;
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
        // Accumulate pixel colors (average per generation count ~ use throughput sums). We have no per-ray radiance except background/emissive contributions already added.
        for (int i=0;i<P;i++) {
            Vec3 c = pixelAccum[i];
            uint32_t cc = contribCount[i]; if (cc>0) c = c / (float)cc; // normalize
            hdr[i*3+0] = c.x; hdr[i*3+1] = c.y; hdr[i*3+2] = c.z;
        }
        stats_.spp = 1;
        stats_.totalRays = (int)std::min<uint64_t>(raysExecuted, (uint64_t)INT32_MAX);
        // Skip temporal accumulation history reuse (makes little sense here) – treat as fresh frame
        haveHistory = false;
        // Jump to tone map path (reuse existing code after hdr filled)
        auto tTraceEndFan = clock::now();
        stats_.msTrace = std::chrono::duration<float,std::milli>(tTraceEndFan - t0).count();
        accum = hdr; // no temporal / denoise
    } else {
        // Normal path tracing branch (MULTITHREADED)
        std::vector<float> hdr(rtW*rtH*3,0.0f);
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

    // Convert obstacle Box list into pair list for BVH call
    std::vector<std::pair<Vec3,Vec3>> obsPairs; obsPairs.reserve(obsBoxes.size());
    for (auto &b: obsBoxes) obsPairs.emplace_back(b.bmin,b.bmax);
    buildOrRefitBVH(gs, ballCenters, ballRs, leftCenter,rightCenter, topCenter, bottomCenter,
            paddleHalfX,paddleHalfY,paddleThickness, horizHalfX,horizHalfY,horizThickness, useHoriz, obsPairs, useObs);

        auto traverseBVH = [&](Vec3 ro, Vec3 rd, Hit &best){
            if (bvhNodes.empty()) return false; bool hitAny=false; struct StackItem{int idx;}; std::array<StackItem,128> stack; int sp=0; float tMax=best.t; stack[sp++]={0};
            while(sp){ auto it=stack[--sp]; const BVHNode &n = bvhNodes[it.idx]; AABB box{{n.bmin[0],n.bmin[1],n.bmin[2]},{n.bmax[0],n.bmax[1],n.bmax[2]}}; float tBox=tMax; if(!intersectAABB(ro,rd,box,tBox)) continue; if (n.primCount>0){ for(int i=0;i<n.primCount;i++){ const BVHPrim &p = bvhPrims[n.primStart+i];
                        if (p.id < (int)ballCenters.size()) { Hit tmp; if(intersectSphere(ro,rd, ballCenters[p.id], ballRs[p.id], tMax, tmp, p.id==0?1:1)){ if(tmp.t<best.t){ best=tmp; hitAny=true; tMax=best.t; } } }
                        else { Vec3 mn{p.bmin[0],p.bmin[1],p.bmin[2]}; Vec3 mx{p.bmax[0],p.bmax[1],p.bmax[2]}; Hit tmp; if(intersectBox(ro,rd,mn,mx,tMax,tmp,p.mat==2?2:0)){ if(tmp.t<best.t){ best=tmp; hitAny=true; tMax=best.t; } } }
                    } }
            else { if (n.left!=-1) stack[sp++]={n.left}; if (n.right!=-1) stack[sp++]={n.right}; }
            }
            return hitAny; };

        auto worker = [&](int yStart, int yEnd){
            for (int y=yStart; y<yEnd; ++y) {
                for (int x=0; x<rtW; ++x) {
                    Vec3 col{0,0,0}; uint32_t seed = (x*1973) ^ (y*9277) ^ (frameCounter*26699u);
                    float sumL=0.0f, sumL2=0.0f; // luminance stats for adaptive sampling
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
                        int pixelMaxBounces = config.maxBounces;
                        if (bounce==0){ float lum0=(throughput.x*0.2126f + throughput.y*0.7152f + throughput.z*0.0722f); if (lum0<0.25f && pixelMaxBounces>3) pixelMaxBounces=3; }
                        for (; bounce<pixelMaxBounces; ++bounce) {
                            Hit best; best.t=1e30f; bool hit=false; Hit tmp;
                            // Planes still brute force (could add to BVH as big boxes):
                            if (intersectPlane(ro,rd, Vec3{0, 1.6f,0}, Vec3{0,-1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,-1.6f,0}, Vec3{0, 1,0}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            if (intersectPlane(ro,rd, Vec3{0,0, 1.8f}, Vec3{0,0,-1}, best.t, tmp, 0)){ best=tmp; hit=true; }
                            if (traverseBVH(ro,rd,best)) hit=true;
                            if (!hit) { float t = 0.5f*(rd.y+1.0f); Vec3 bgTop{0.26f,0.30f,0.38f}; Vec3 bgBottom{0.08f,0.10f,0.16f}; Vec3 bg=(1.0f-t)*bgBottom+t*bgTop; col = col + throughput * bg; terminated=true; break; }
                            if (best.mat==1) { Vec3 emit{2.2f,1.4f,0.8f}; emit=emit*config.emissiveIntensity; col = col + throughput * emit; terminated=true; break; }
                            if (best.mat==0) {
                                if (bounce==0){ // record first hit normal/albedo
                                    size_t fh = (size_t)(y*rtW + x)*3; firstHitNormal[fh+0]=best.n.x; firstHitNormal[fh+1]=best.n.y; firstHitNormal[fh+2]=best.n.z; firstHitAlbedo[fh+0]=0.62f; firstHitAlbedo[fh+1]=0.64f; firstHitAlbedo[fh+2]=0.67f; }
                                Vec3 n=best.n; float uA,uB; rng2(seed,uA,uB); float r1=2*3.1415926f*uA; float r2=uB; float r2s=std::sqrt(r2); Vec3 w=n; Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; Vec3 v=norm(cross(w,a)); Vec3 u=cross(v,w); Vec3 d=norm(u*(std::cos(r1)*r2s)+v*(std::sin(r1)*r2s)+w*std::sqrt(1-r2)); ro = best.pos + best.n*0.002f; rd=d; throughput=throughput*Vec3{0.62f,0.64f,0.67f}; Vec3 direct = sampleDirect(best.pos, n, rd, seed, false); col = col + throughput * direct; }
                            else if (best.mat==2) {
                                if (bounce==0){ size_t fh=(size_t)(y*rtW + x)*3; firstHitNormal[fh+0]=best.n.x; firstHitNormal[fh+1]=best.n.y; firstHitNormal[fh+2]=best.n.z; firstHitAlbedo[fh+0]=0.55f; firstHitAlbedo[fh+1]=0.60f; firstHitAlbedo[fh+2]=0.85f; }
                                Vec3 paddleColor{0.25f,0.32f,0.6f}; Vec3 n=best.n; float cosi=dot(rd,n); rd = rd - n*(2.0f*cosi); float rough=config.metallicRoughness; float uA,uB; rng2(seed,uA,uB); float r1=2*3.1415926f*uA; float r2=uB; float r2s=std::sqrt(r2); Vec3 w=norm(n); Vec3 a=(std::fabs(w.x)>0.1f)?Vec3{0,1,0}:Vec3{1,0,0}; Vec3 v=norm(cross(w,a)); Vec3 u=cross(v,w); Vec3 fuzz=norm(u*(std::cos(r1)*r2s)+v*(std::sin(r1)*r2s)+w*std::sqrt(1-r2)); rd=norm(rd*(1.0f-rough)+fuzz*rough); ro=best.pos+rd*0.002f; throughput=throughput*(Vec3{0.86f,0.88f,0.94f}*0.5f + paddleColor*0.5f); Vec3 direct = sampleDirect(best.pos, n, rd, seed, true) * paddleColor; col = col + throughput * direct; }
                            if (best.mat==0 || best.mat==2) { float maxT = std::max(throughput.x, std::max(throughput.y, throughput.z)); if (maxT < 1e-3f) { earlyExitAccum++; bounce++; break; } if (config.rouletteEnable && bounce >= config.rouletteStartBounce) { float p = std::max(config.rouletteMinProb, std::min(maxT, 0.95f)); float rrand=(xorshift(seed)&65535)/65535.0f; if(rrand>p){ rouletteAccum++; bounce++; break; } throughput = throughput / p; } }
                        }
                        if(!terminated){ Vec3 amb{0.05f,0.055f,0.06f}; col = col + throughput * amb; }
                        totalBounces.fetch_add(bounce, std::memory_order_relaxed); pathsTraced++;
                        // Adaptive sampling luminance accumulation
                        float lum = col.x*0.2126f + col.y*0.7152f + col.z*0.0722f;
                        sumL += lum; sumL2 += lum*lum;
                        if (config.adaptiveSamplingEnable && s+1 >= config.adaptiveMinSamples) {
                            float n = (float)(s+1);
                            float mean = sumL / n;
                            float var = (sumL2 / n) - mean*mean;
                            if (var < config.adaptiveVarianceThreshold * mean * mean) { // relative variance small
                                stats_.adaptiveShortCircuits++;
                                break; // early exit sampling loop for this pixel
                            }
                        }
                    }
                    col = col / (float)spp;
                    size_t pindex = (size_t)(y*rtW + x);
                    size_t idx = pindex*3; hdr[idx+0]=col.x; hdr[idx+1]=col.y; hdr[idx+2]=col.z;
                    // Capture first-hit data for reprojection (we recorded only final col; approximate using first bounce contributions is omitted for brevity)
                    // As a proxy, store the last surface normal encountered (best.n from loop) and simple albedo guess based on material
                    // (In a full implementation we'd separate first intersection shading path.)
                    // For now leave zero if no surface hit before background.
                }
            }
        };

        auto tTraceStart = clock::now();
        // Use persistent pool (main thread + pool workers)
        g_srPool.dispatch((unsigned)want, rtH, [&](int y){ worker(y, y+1); });
        auto tTraceEnd = clock::now();
        stats_.msTrace = std::chrono::duration<float,std::milli>(tTraceEnd - t0).count(); t0 = tTraceEnd;
        stats_.spp = spp; stats_.totalRays = spp * rtW * rtH;
    int pt = pathsTraced.load(); long long tb = totalBounces.load();
    stats_.avgBounceDepth = (pt>0)? (float)tb / (float)pt : 0.0f;
        stats_.earlyExitCount = earlyExitAccum.load();
        stats_.rouletteTerminations = rouletteAccum.load();
        // Temporal accumulation with per-pixel gating (approximate placeholder): if history exists and color delta large, blend with higher alpha
        if (config.reprojectionEnable) {
            size_t count = hdr.size()/3;
            if (!haveHistory){ accum = hdr; haveHistory=true; }
            else {
                float ndotThresh = config.reprojectionNormalDotThreshold;
                float albThresh2 = config.reprojectionAlbedoThreshold * config.reprojectionAlbedoThreshold;
                float a = config.accumAlpha;
                for(size_t i=0;i<count;i++){
                    size_t k=i*3;
                    float npx=prevFirstHitNormal[k+0], npy=prevFirstHitNormal[k+1], npz=prevFirstHitNormal[k+2];
                    float ncx=firstHitNormal[k+0], ncy=firstHitNormal[k+1], ncz=firstHitNormal[k+2];
                    float ndot = npx*ncx + npy*ncy + npz*ncz;
                    float apr=prevFirstHitAlbedo[k+0], apg=prevFirstHitAlbedo[k+1], apb=prevFirstHitAlbedo[k+2];
                    float acr=firstHitAlbedo[k+0], acg=firstHitAlbedo[k+1], acb=firstHitAlbedo[k+2];
                    float dR=apr-acr, dG=apg-acg, dB=apb-acb; float albDist2 = dR*dR + dG*dG + dB*dB;
                    if (ndot < ndotThresh || albDist2 > albThresh2){ // history invalid
                        accum[k+0]=hdr[k+0]; accum[k+1]=hdr[k+1]; accum[k+2]=hdr[k+2];
                    } else {
                        accum[k+0] = accum[k+0]*(1.0f-a) + hdr[k+0]*a;
                        accum[k+1] = accum[k+1]*(1.0f-a) + hdr[k+1]*a;
                        accum[k+2] = accum[k+2]*(1.0f-a) + hdr[k+2]*a;
                    }
                }
            }
            prevFirstHitNormal = firstHitNormal; prevFirstHitAlbedo = firstHitAlbedo;
        } else {
            temporalAccumulate(hdr); haveHistory=true; }
        auto tTempEnd = clock::now(); stats_.msTemporal = std::chrono::duration<float,std::milli>(tTempEnd - t0).count(); t0 = tTempEnd;
        bool skipDenoise = (spp >= 4) && config.denoiseStrength > 0.0f; if (skipDenoise) { stats_.denoiseSkipped = true; } else { spatialDenoise(); auto tDenoiseEnd = clock::now(); stats_.msDenoise = std::chrono::duration<float,std::milli>(tDenoiseEnd - t0).count(); t0 = tDenoiseEnd; }
    }
    // If we were in normal mode, hdr/accum already processed; fanout mode set accum directly.

    // Upscale (nearest) + tone map to outW/outH pixel32
    for (int y=0; y<outH; ++y) {
        int sy = std::min(rtH-1, (int)((float)y/outH * rtH));
        int syBase = sy*rtW*3;
        for (int x=0; x<outW; ++x) {
            int sx = std::min(rtW-1, (int)((float)x/outW * rtW));
            size_t si = (size_t)syBase + (size_t)sx*3;
            float r = accum[si+0];
            float g = accum[si+1];
            float b = accum[si+2];
            // ACES tone mapping (borrowed from removed segment tracer branch)
            auto ACESFilm = [](float R, float G, float B){
                auto f=[&](float x){ float a=2.51f,b=0.03f,c=2.43f,d=0.59f,e=0.14f; float num = x*(a*x + b); float den = x*(c*x + d) + e; float o = (den!=0.0f)? num/den : 0.0f; if(o<0)o=0; if(o>1)o=1; return o; };
                return std::tuple<float,float,float>(f(R),f(G),f(B));
            };
            std::tie(r,g,b) = ACESFilm(r,g,b);
            // Gamma
            r = std::pow(r, 1.0f/2.2f); g = std::pow(g, 1.0f/2.2f); b = std::pow(b, 1.0f/2.2f);
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
    float f = config.denoiseStrength;
    if (f <= 0.0001f) return; // skip work if disabled / negligible
    size_t needed = (size_t)rtW*rtH*3;
    if (history.size() != needed) history.resize(needed);
    const int w = rtW;
    const int h = rtH;
    for (int y=0; y<h; ++y) {
        int y0 = (y>0)? y-1 : y;
        int y1 = y;
        int y2 = (y<h-1)? y+1 : y;
        for (int x=0; x<w; ++x) {
            int x0 = (x>0)? x-1 : x;
            int x1 = x;
            int x2 = (x<w-1)? x+1 : x;
            // Accumulate 3x3 neighborhood (with edge replication) – unrolled
            size_t idxs[9] = {
                (size_t)(y0*w + x0)*3, (size_t)(y0*w + x1)*3, (size_t)(y0*w + x2)*3,
                (size_t)(y1*w + x0)*3, (size_t)(y1*w + x1)*3, (size_t)(y1*w + x2)*3,
                (size_t)(y2*w + x0)*3, (size_t)(y2*w + x1)*3, (size_t)(y2*w + x2)*3
            };
            float sum0=0,sum1=0,sum2=0;
            for(int k=0;k<9;k++){ sum0+=accum[idxs[k]+0]; sum1+=accum[idxs[k]+1]; sum2+=accum[idxs[k]+2]; }
            float avg0 = sum0 / 9.0f;
            float avg1 = sum1 / 9.0f;
            float avg2 = sum2 / 9.0f;
            size_t o = (size_t)(y*w + x)*3;
            history[o+0] = accum[o+0]*(1.0f-f) + avg0*f;
            history[o+1] = accum[o+1]*(1.0f-f) + avg1*f;
            history[o+2] = accum[o+2]*(1.0f-f) + avg2*f;
        }
    }
    accum.swap(history);
}

#endif // _WIN32
