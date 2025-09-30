#include "pt_renderer_adapter.h"
#include "../soft_renderer.h"
#include "../d3d12_renderer.h"
#include "../settings.h"
#include "../../core/game_core.h"
#include <fstream>

// Helper function to write to log file
static void LogToFile(const char* message) {
    std::ofstream logFile("pong_renderer.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.close();
    }
}

PTRendererAdapter::PTRendererAdapter() {
    // Clear previous log
    std::ofstream logFile("pong_renderer.log", std::ios::trunc);
    logFile.close();
    
    LogToFile("===========================================");
    LogToFile("PTRendererAdapter: Starting initialization");
    LogToFile("===========================================");
    
    // Try to initialize D3D12 GPU renderer first
    D3D12Renderer* gpu = new D3D12Renderer();
    LogToFile("Created D3D12Renderer instance, calling initialize()...");
    
    if (gpu->initialize()) {
        gpuImpl_ = gpu;
        usingGPU_ = true;
        LogToFile("*** SUCCESS: D3D12 GPU ACCELERATION ACTIVE ***");
        LogToFile("GPU renderer initialized successfully");
        OutputDebugStringA("[PTRenderer] ========================================\n");
        OutputDebugStringA("[PTRenderer] *** USING D3D12 GPU ACCELERATION ***\n");
        OutputDebugStringA("[PTRenderer] ========================================\n");
    } else {
        LogToFile("*** D3D12 initialization FAILED ***");
        LogToFile("Falling back to CPU renderer");
        delete gpu;
        // Fall back to CPU renderer
        cpuImpl_ = new SoftRenderer();
        usingGPU_ = false;
        LogToFile("*** CPU FALLBACK ACTIVE ***");
        OutputDebugStringA("[PTRenderer] ========================================\n");
        OutputDebugStringA("[PTRenderer] *** USING CPU FALLBACK ***\n");
        OutputDebugStringA("[PTRenderer] Check debug output above for D3D12 errors\n");
        OutputDebugStringA("[PTRenderer] ========================================\n");
    }
    
    LogToFile("===========================================");
    LogToFile(usingGPU_ ? "Renderer: GPU (D3D12)" : "Renderer: CPU (Software)");
    LogToFile("===========================================");
}

PTRendererAdapter::~PTRendererAdapter() {
    if (gpuImpl_) {
        delete gpuImpl_;
        gpuImpl_ = nullptr;
    }
    if (cpuImpl_) {
        delete cpuImpl_;
        cpuImpl_ = nullptr;
    }
}

static void applySettings(SoftRenderer* r, D3D12Renderer* g, bool useGPU, SRConfig& cur, const Settings& s){
	bool changed=false; auto apply=[&](auto &dst, auto v){ if(dst!=v){ dst=v; changed=true; }};
	apply(cur.raysPerFrame, s.pt_rays_per_frame);
	apply(cur.maxBounces, s.pt_max_bounces);
	apply(cur.internalScalePct, s.pt_internal_scale);
	apply(cur.metallicRoughness, s.pt_roughness/100.0f);
	apply(cur.emissiveIntensity, s.pt_emissive/100.0f);
	apply(cur.accumAlpha, s.pt_accum_alpha/100.0f);
	apply(cur.denoiseStrength, s.pt_denoise_strength/100.0f);
	apply(cur.forceFullPixelRays, s.pt_force_full_pixel_rays!=0);
	apply(cur.useOrtho, s.pt_use_ortho!=0);
	apply(cur.rouletteEnable, s.pt_rr_enable!=0);
	apply(cur.rouletteStartBounce, s.pt_rr_start_bounce);
	apply(cur.rouletteMinProb, s.pt_rr_min_prob_pct/100.0f);
	// Soft shadow / PBR settings
	apply(cur.softShadowSamples, s.pt_soft_shadow_samples);
	float lr = s.pt_light_radius_pct / 100.0f; if(lr < 0.1f) lr = 0.1f; if(lr>5.0f) lr=5.0f; apply(cur.lightRadiusScale, lr);
	apply(cur.pbrEnable, s.pt_pbr_enable!=0);
	// Experimental fan-out settings
	bool wantFan = (s.pt_fanout_enable!=0);
	if(cur.fanoutCombinatorial != wantFan){ cur.fanoutCombinatorial = wantFan; changed=true; }
	if(s.pt_fanout_cap > 0 && (uint64_t) s.pt_fanout_cap != cur.fanoutMaxTotalRays){ cur.fanoutMaxTotalRays = (uint64_t)s.pt_fanout_cap; changed=true; }
	bool abortFlag = (s.pt_fanout_abort!=0);
	if(cur.fanoutAbortOnCap != abortFlag){ cur.fanoutAbortOnCap = abortFlag; changed=true; }
	// Defensive clamping for corrupted / legacy settings
	if(cur.raysPerFrame < 1){ cur.raysPerFrame = 1; changed=true; }
	if(cur.internalScalePct < 25){ cur.internalScalePct = 25; changed=true; }
	if(cur.accumAlpha < 0.01f){ cur.accumAlpha = 0.01f; changed=true; }
	if(cur.softShadowSamples < 1){ cur.softShadowSamples = 1; changed=true; }
	if(cur.softShadowSamples > 64){ cur.softShadowSamples = 64; changed=true; }
	if(cur.lightRadiusScale < 0.1f){ cur.lightRadiusScale = 0.1f; changed=true; }
	if(cur.lightRadiusScale > 5.0f){ cur.lightRadiusScale = 5.0f; changed=true; }
	if(changed){
        if(useGPU && g) { g->configure(cur); g->resetHistory(); }
        else if(!useGPU && r) { r->configure(cur); r->resetHistory(); }
    }
}

void PTRendererAdapter::configure(const Settings& s){
	if(!cfg.enablePathTracing) cfg.enablePathTracing=true;
    applySettings(cpuImpl_, gpuImpl_, usingGPU_, cfg, s);
}

void PTRendererAdapter::resize(int w,int h){
    if(usingGPU_ && gpuImpl_) {
        gpuImpl_->resize(w, h);
        gpuImpl_->resetHistory();
    } else if(cpuImpl_) {
        cpuImpl_->resize(w, h);
        cpuImpl_->resetHistory();
    }
}
void PTRendererAdapter::render(const GameState& gs, const Settings& s, const UIState&, HDC target){
	if(!target) return;
    if(!usingGPU_ && !cpuImpl_) return; // CPU fallback not initialized
    if(usingGPU_ && !gpuImpl_) return;  // GPU not initialized
	
    configure(s);
	
    // Get renderer interface for pixel data
    const uint32_t* px = nullptr;
    const BITMAPINFO* bi = nullptr;
    int srcW = 0, srcH = 0;

    if(usingGPU_ && gpuImpl_) {
        gpuImpl_->render(gs);
        bi = &gpuImpl_->getBitmapInfo();
        px = gpuImpl_->pixels();
    } else if(cpuImpl_) {
        cpuImpl_->render(gs);
        bi = &cpuImpl_->getBitmapInfo();
        px = cpuImpl_->pixels();
    }

    if(!bi || !px) return;

	// Query target window size
	RECT cr{0,0,0,0}; 
    HWND hwnd = WindowFromDC(target); 
    int dw = (int)bi->bmiHeader.biWidth; 
    int dh = (int)bi->bmiHeader.biHeight;
	if(hwnd) { 
        GetClientRect(hwnd, &cr); 
        dw = cr.right - cr.left; 
        dh = cr.bottom - cr.top; 
    }
	
    srcW = (int)bi->bmiHeader.biWidth; 
    srcH = (int)bi->bmiHeader.biHeight;
	if(srcH < 0) srcH = -srcH; // top-down DIB: use absolute height for StretchDIBits source size
	if(dh < 0) dh = -dh; // ensure positive destination height when using memory DC without associated window
	
    if(srcW > 0 && srcH > 0) {
		// Checkerboard debug heuristic disabled when experimental fan-out mode is active to avoid masking black accumulation during early depths.
		bool wantDebugChecker = !cfg.fanoutCombinatorial; // only when not in fan-out mode
		const uint32_t* blitSrc = px; 
        std::vector<uint32_t> debugBuf;
		
        if(wantDebugChecker) {
			bool allBlack = true; 
            int sampleCount = (srcW * srcH); 
            if(sampleCount > 4000) sampleCount = 4000;
			for(int i = 0; i < sampleCount; i++) { 
                if(px[i] & 0x00FFFFFF) { 
                    allBlack = false; 
                    break; 
                }
            }
			if(allBlack) {
				debugBuf.resize(srcW * srcH);
				for(int y = 0; y < srcH; y++) {
					for(int x = 0; x < srcW; x++) {
						bool c = ((x / 8) ^ (y / 8)) & 1; 
                        debugBuf[y * srcW + x] = 0xFF000000 | (c ? 0x00404040 : 0x00808080);
					}
				}
				blitSrc = debugBuf.data();
			}
		}
        
        int ret = StretchDIBits(target, 0, 0, dw, dh, 0, 0, srcW, srcH, blitSrc, bi, DIB_RGB_COLORS, SRCCOPY);
#ifdef PONG_PT_DEBUG
        if(ret == 0 || ret == GDI_ERROR) {
            OutputDebugStringA("[PT] StretchDIBits failed or drew 0 lines; drawing magenta fallback\n");
            HBRUSH br = CreateSolidBrush(RGB(255, 0, 255)); 
            RECT rct{0, 0, dw, dh}; 
            FillRect(target, &rct, br); 
            DeleteObject(br);
        }
#endif
	}
}

const SRStats* PTRendererAdapter::stats() const { 
    if(usingGPU_ && gpuImpl_) return &gpuImpl_->stats();
    else if(cpuImpl_) return &cpuImpl_->stats();
    return nullptr;
}
