#include "pt_renderer_adapter.h"
#include "../soft_renderer.h"
#include "../settings.h"
#include "../../core/game_core.h"

PTRendererAdapter::PTRendererAdapter():impl(new SoftRenderer()){}
PTRendererAdapter::~PTRendererAdapter(){ delete impl; }

static void applySettings(SoftRenderer* r, SRConfig& cur, const Settings& s){
	bool changed=false; auto apply=[&](auto &dst, auto v){ if(dst!=v){ dst=v; changed=true; }};
	apply(cur.raysPerFrame, s.pt_rays_per_frame);
	apply(cur.maxBounces, s.pt_max_bounces);
	apply(cur.internalScalePct, s.pt_internal_scale);
	apply(cur.metallicRoughness, s.pt_roughness/100.0f);
	apply(cur.emissiveIntensity, s.pt_emissive/100.0f);
	apply(cur.paddleEmissiveIntensity, s.pt_paddle_emissive/100.0f);
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
	if(changed){ r->configure(cur); r->resetHistory(); }
}

void PTRendererAdapter::configure(const Settings& s){
	if(!impl) return; if(!cfg.enablePathTracing) cfg.enablePathTracing=true; applySettings(impl,cfg,s);
}
void PTRendererAdapter::resize(int w,int h){ if(impl) { impl->resize(w,h); impl->resetHistory(); } }
void PTRendererAdapter::render(const GameState& gs, const Settings& s, const UIState&, HDC target){
	if(!impl||!target) return;
	configure(s);
	impl->render(gs);
	const BITMAPINFO &bi=impl->getBitmapInfo();
	// Query target window size
	RECT cr{0,0,0,0}; HWND hwnd = WindowFromDC(target); int dw=(int)bi.bmiHeader.biWidth; int dh=(int)bi.bmiHeader.biHeight;
	if(hwnd){ GetClientRect(hwnd,&cr); dw = cr.right - cr.left; dh = cr.bottom - cr.top; }
	int srcW = (int)bi.bmiHeader.biWidth; int srcH = (int)bi.bmiHeader.biHeight;
	if(srcH < 0) srcH = -srcH; // top-down DIB: use absolute height for StretchDIBits source size
	if(dh < 0) dh = -dh; // ensure positive destination height when using memory DC without associated window
	if(srcW>0 && srcH>0){
		// Checkerboard debug heuristic disabled when experimental fan-out mode is active to avoid masking black accumulation during early depths.
		const uint32_t* px = impl->pixels();
		bool wantDebugChecker = !cfg.fanoutCombinatorial; // only when not in fan-out mode
		const uint32_t* blitSrc = px; std::vector<uint32_t> debugBuf;
		if(wantDebugChecker){
			bool allBlack=true; int sampleCount = (srcW*srcH); if(sampleCount>4000) sampleCount=4000;
			for(int i=0;i<sampleCount;i++){ if(px[i] & 0x00FFFFFF){ allBlack=false; break; }}
			if(allBlack){
				debugBuf.resize(srcW*srcH);
				for(int y=0;y<srcH;y++){
					for(int x=0;x<srcW;x++){
						bool c = ((x/8) ^ (y/8)) & 1; debugBuf[y*srcW+x] = 0xFF000000 | (c?0x00404040:0x00808080);
					}
				}
				blitSrc = debugBuf.data();
			}
		}
	int ret = StretchDIBits(target,0,0,dw,dh,0,0,srcW,srcH, blitSrc, &bi, DIB_RGB_COLORS, SRCCOPY);
#ifdef PONG_PT_DEBUG
	if(ret==0 || ret==GDI_ERROR){
	    OutputDebugStringA("[PT] StretchDIBits failed or drew 0 lines; drawing magenta fallback\n");
	    HBRUSH br = CreateSolidBrush(RGB(255,0,255)); RECT rct{0,0,dw,dh}; FillRect(target,&rct,br); DeleteObject(br);
	}
#endif
#ifdef PONG_PT_DEBUG
		if(ret==GDI_ERROR){ RECT dbg{0,0,dw,dh}; HBRUSH r=CreateSolidBrush(RGB(255,0,0)); FillRect(target,&dbg,r); DeleteObject(r);} 
#endif
	}
}
const SRStats* PTRendererAdapter::stats() const { return impl? &impl->stats(): nullptr; }
