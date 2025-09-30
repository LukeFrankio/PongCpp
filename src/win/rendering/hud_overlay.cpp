#include "hud_overlay.h"
#include "../../core/game_core.h"
#include "../soft_renderer.h"
#include <string>
#include <cwchar>

static void drawText(HDC dc, const std::wstring& txt, int x, int y) {
	RECT r{ x,y,x+1200,y+40 }; DrawTextW(dc, txt.c_str(), -1, &r, DT_LEFT|DT_TOP|DT_NOPREFIX|DT_SINGLELINE);
}

void HudOverlay::draw(const GameState& gs, const SRStats* stats, HDC dc, int w, int h, int dpi, int highScore, bool isGPU) {
	if(!dc) return;
	double ui = (double)dpi/96.0;
	int xPad = (int)(10*ui);
	int yPad = xPad;
	std::wstring score = std::to_wstring(gs.score_left) + L" - " + std::to_wstring(gs.score_right);
	// Semi-transparent background for readability
	HBRUSH back = CreateSolidBrush(RGB(8,8,12)); RECT bg{0,0,(int)(280*ui),(int)(180*ui)}; FillRect(dc,&bg,back); DeleteObject(back);
	
	// GPU/CPU indicator badge - PROMINENT
	if (isGPU) {
		HBRUSH gpuBrush = CreateSolidBrush(RGB(0,200,0)); // Bright green for GPU
		RECT gpuRect{xPad, yPad, xPad + (int)(60*ui), yPad + (int)(20*ui)};
		FillRect(dc, &gpuRect, gpuBrush);
		DeleteObject(gpuBrush);
		SetTextColor(dc, RGB(0,0,0));
		SetBkMode(dc, TRANSPARENT);
		RECT textRect{xPad, yPad, xPad + (int)(60*ui), yPad + (int)(20*ui)};
		DrawTextW(dc, L"GPU", -1, &textRect, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		SetTextColor(dc, RGB(255,255,255)); // Reset to white
	} else {
		HBRUSH cpuBrush = CreateSolidBrush(RGB(200,100,0)); // Orange for CPU
		RECT cpuRect{xPad, yPad, xPad + (int)(60*ui), yPad + (int)(20*ui)};
		FillRect(dc, &cpuRect, cpuBrush);
		DeleteObject(cpuBrush);
		SetTextColor(dc, RGB(255,255,255));
		SetBkMode(dc, TRANSPARENT);
		RECT textRect{xPad, yPad, xPad + (int)(60*ui), yPad + (int)(20*ui)};
		DrawTextW(dc, L"CPU", -1, &textRect, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	}
	
	// Move score to the right of GPU/CPU badge
	drawText(dc, score, xPad + (int)(70*ui), yPad);
	int lineH = (int)(18*ui + 0.5);
	int line = 1;
	// Game mode line (requires GameMode enum names; rely on order in game_core.h)
	std::wstring modeName = L"Classic";
	// Heuristic: determine mode by presence of obstacles / extra paddles / multiple balls
	if (gs.mode == GameMode::ThreeEnemies) modeName = L"Three Enemies";
	else if (gs.mode == GameMode::Obstacles) modeName = L"Obstacles";
	else if (gs.mode == GameMode::MultiBall) modeName = L"MultiBall";
	drawText(dc, L"Mode: " + modeName, xPad, yPad + lineH*line++);
	if(stats){
		wchar_t buf[256];
		swprintf(buf,256,L"PT %.1fms | %d spp", stats->msTotal, stats->spp); drawText(dc, buf, xPad, yPad + lineH*line++);
		swprintf(buf,256,L"Trace %.1f  Temp %.1f  Denoise %.1f", stats->msTrace, stats->msTemporal, stats->msDenoise); drawText(dc, buf, xPad, yPad + lineH*line++);
		swprintf(buf,256,L"Upscale %.1f  Bnc %.1f", stats->msUpscale, stats->avgBounceDepth); drawText(dc, buf, xPad, yPad + lineH*line++);
		// Extra diagnostics: internal resolution & first pixel sample (posted after tone map in adapter)
		// We can't read pixel data here directly; adapter will overlay if zero. So just show internal dims.
		swprintf(buf,256,L"Internal %dx%d", stats->internalW, stats->internalH); drawText(dc, buf, xPad, yPad + lineH*line++);
		if(stats->projectedRays>0){
			swprintf(buf,256,L"FanOut proj %lld exec %d%s", (long long)stats->projectedRays, stats->totalRays, stats->fanoutAborted?L" (ABORT)":L"");
			drawText(dc, buf, xPad, yPad + lineH*line++);
		}
	}
	// High score displayed on right side
	std::wstring hs = L"High: " + std::to_wstring(highScore);
	SIZE sz{0,0}; GetTextExtentPoint32W(dc, hs.c_str(), (int)hs.size(), &sz);
	int xRight = w - sz.cx - xPad;
	drawText(dc, hs, xRight, yPad);
}
