#include "settings_panel.h"
#include "../settings.h"
#include "../input/input_state.h"
#include <algorithm>

void SettingsPanel::begin(HWND hwnd, HINSTANCE hInstance, Settings* settings, SettingsManager* mgr, const std::wstring* exeDirPath) {
	hwnd_ = hwnd; hInstance_ = hInstance; settings_ = settings; settingsMgr_ = mgr; exeDir_ = exeDirPath;
	original_ = *settings_; baselinePtr_ = settings_; changedSinceOpen_ = false;
	sel_ = 0; scrollOffset_ = 0; maxScroll_ = 0; saveFeedbackTicks_ = 0;
}

void SettingsPanel::clampSel() { if (sel_ < 0) sel_ = 0; if (sel_ > totalItems_() - 1) sel_ = totalItems_() - 1; }

void SettingsPanel::resetDefaults() {
	if (!settings_) return;
	settings_->pt_rays_per_frame = 1;
	settings_->pt_max_bounces = 1;
	settings_->pt_internal_scale = 10;
	settings_->pt_roughness = 0;
	settings_->pt_emissive = 100;
	settings_->pt_accum_alpha = 75;
	settings_->pt_denoise_strength = 25;
	settings_->pt_force_full_pixel_rays = 1;
	settings_->pt_use_ortho = 0;
	settings_->pt_rr_enable = 1;
	settings_->pt_rr_start_bounce = 2;
	settings_->pt_rr_min_prob_pct = 10;
	settings_->pt_soft_shadow_samples = 4;
	settings_->pt_light_radius_pct = 100;
	settings_->pt_pbr_enable = 1;
	// Segment tracer settings removed
	changedSinceOpen_ = true;
}

SettingsPanel::Action SettingsPanel::frame(HDC memDC,
										   int winW, int winH, int dpi,
										   const InputState& input,
										   int mouse_x, int mouse_y, bool mouse_pressed,
										   int& mouse_wheel_delta,
										   int& last_click_x, int& last_click_y) {
	if (!settings_ || !settingsMgr_) return Action::None;
	double ui_scale = (double)dpi / 96.0;
	// Background
	RECT bg {0,0,winW,winH}; HBRUSH b = CreateSolidBrush(RGB(15,15,25)); FillRect(memDC,&bg,b); DeleteObject(b);
	SetBkMode(memDC, TRANSPARENT); SetTextColor(memDC, RGB(235,235,245));
	// Title
	RECT trTitle {0,(int)(10*ui_scale),winW,(int)(90*ui_scale)};
	DrawTextW(memDC, L"Path Tracer Settings", -1, &trTitle, DT_CENTER|DT_TOP|DT_SINGLELINE);

	// Sliders (segment tracer samples removed)
	SliderInfo sliders[] = {
		{L"Rays / Frame", &settings_->pt_rays_per_frame, 1, 1000, 1},
		{L"Max Bounces", &settings_->pt_max_bounces, 1, 8, 1},
		{L"Internal Scale %", &settings_->pt_internal_scale, 1, 100, 1},
		{L"Metal Roughness %", &settings_->pt_roughness, 0, 100, 1},
		{L"Emissive %", &settings_->pt_emissive, 1, 500, 1},
		{L"Accum Alpha %", &settings_->pt_accum_alpha, 0, 100, 1},
		{L"Denoise %", &settings_->pt_denoise_strength, 0, 100, 1},
		{L"Soft Shadow Spp", &settings_->pt_soft_shadow_samples, 1, 64, 1},
		{L"Light Radius %", &settings_->pt_light_radius_pct, 10, 500, 1},
		{L"Recording FPS", &settings_->recording_fps, 15, 60, 1},
	};
	int sliderCount = (int)(sizeof(sliders)/sizeof(sliders[0]));
	int centerX = winW/2; int baseY = (int)(110*ui_scale + 0.5) - scrollOffset_; int rowH = (int)(46*ui_scale + 0.5);
	int barW = (int)(420*ui_scale + 0.5); if (barW < 100) barW = 100; int barH = (int)(10*ui_scale + 0.5); if (barH < 8) barH = 8;
	int bottomPanelH = (int)(130*ui_scale + 0.5);
	int panelTop = winH - bottomPanelH + (int)(6*ui_scale + 0.5);

	// Preliminary scroll computation (will be refined after all dynamic rows known)
	int topVisible = (int)(80*ui_scale + 0.5);
	int usableHeight = winH - bottomPanelH;
	int contentBottom = baseY + (kBaseSliderCount + 9)*rowH + (int)(80*ui_scale + 0.5);
	maxScroll_ = std::max(0, contentBottom - usableHeight + topVisible);

	// Mouse wheel
	if (mouse_wheel_delta != 0) {
		int steps = mouse_wheel_delta / 120;
		if (steps != 0) {
			scrollOffset_ -= steps * 40; if (scrollOffset_ < 0) scrollOffset_ = 0; if (scrollOffset_ > maxScroll_) scrollOffset_ = maxScroll_;
			mouse_wheel_delta -= steps * 120;
		}
	}

	// Draw sliders
	for (int i=0;i<sliderCount;i++) {
		int y = baseY + i*rowH; bool hot = (sel_ == i);
		SetTextColor(memDC, hot?RGB(255,240,160):RGB(200,200,210));
		std::wstring dyn = std::wstring(sliders[i].label) + L": " + std::to_wstring(*sliders[i].val);
		RECT lr { centerX - barW/2, y - (int)(14*ui_scale), centerX + barW/2, y };
		DrawTextW(memDC, dyn.c_str(), -1, &lr, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
		int bx = centerX - barW/2; int by = y + (int)(14*ui_scale + 0.5);
		RECT bar { bx, by, bx + barW, by + barH };
		HBRUSH brBase = CreateSolidBrush(RGB(50,60,80)); FillRect(memDC,&bar,brBase); DeleteObject(brBase);
		double t = double(*sliders[i].val - sliders[i].minv) / double(sliders[i].maxv - sliders[i].minv);
		RECT fill { bx, by, bx + (int)(barW * t), by + barH };
		HBRUSH brFill = CreateSolidBrush(hot?RGB(120,180,255):RGB(90,120,180)); FillRect(memDC,&fill,brFill); DeleteObject(brFill);
	}

	// Checkboxes & extra sliders (extended)
	int cyForce = baseY + kBaseSliderCount*rowH; bool forceHot = (sel_==idxForce_());
	int cyCam   = baseY + (kBaseSliderCount+1)*rowH; bool camHot = (sel_==idxCamera_());
	int cyRRE   = baseY + (kBaseSliderCount+2)*rowH; bool rrHot = (sel_==idxRREnable_());
	int cyRRStart = baseY + (kBaseSliderCount+3)*rowH; bool rrStartHot = (sel_==idxRRStart_());
	int cyRRMin   = baseY + (kBaseSliderCount+4)*rowH; bool rrMinHot = (sel_==idxRRMin_());
	int cyPBR   = baseY + (kBaseSliderCount+5)*rowH; bool pbrHot = (sel_==idxPBREnable_());
	int cyFanoutEnable = baseY + (kBaseSliderCount+6)*rowH; bool fanEnableHot = (sel_==idxFanoutEnable_());
	int cyFanoutCap = baseY + (kBaseSliderCount+7)*rowH; bool fanCapHot = (sel_==idxFanoutCap_());
	int cyFanoutAbort = baseY + (kBaseSliderCount+8)*rowH; bool fanAbortHot = (sel_==idxFanoutAbort_());
	// Recalculate scroll range using last dynamic row (fanout abort)
	int contentBottom2 = cyFanoutAbort + rowH + (int)(80*ui_scale + 0.5);
	maxScroll_ = std::max(0, contentBottom2 - usableHeight + topVisible);
	auto drawCenterLine=[&](const std::wstring &txt,int cy,bool hot){ SetTextColor(memDC, hot?RGB(255,240,160):RGB(200,200,210)); RECT r{0,cy-16,winW,cy+16}; DrawTextW(memDC,txt.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE); };
	drawCenterLine(std::wstring(L"Force 1 ray / pixel: ") + (settings_->pt_force_full_pixel_rays?L"ON":L"OFF"), cyForce, forceHot);
	drawCenterLine(std::wstring(L"Camera: ") + (settings_->pt_use_ortho?L"Orthographic":L"Perspective"), cyCam, camHot);
	drawCenterLine(std::wstring(L"Russian Roulette: ") + (settings_->pt_rr_enable?L"ON":L"OFF"), cyRRE, rrHot);
	drawCenterLine(std::wstring(L"PBR: ") + (settings_->pt_pbr_enable?L"ON":L"OFF"), cyPBR, pbrHot);
	drawCenterLine(std::wstring(L"Fan-Out Mode: ") + (settings_->pt_fanout_enable?L"ON":L"OFF"), cyFanoutEnable, fanEnableHot);
	drawCenterLine(std::wstring(L"Fan-Out Abort On Cap: ") + (settings_->pt_fanout_abort?L"ON":L"OFF"), cyFanoutAbort, fanAbortHot);
	// Segment tracer toggle removed
	// RR sliders
	auto drawExtraSlider=[&](const std::wstring& label,int value,int minv,int maxv,int cy,bool hot){
		SetTextColor(memDC, hot?RGB(255,240,160):RGB(200,200,210)); RECT r{0,cy-16,winW,cy+16}; DrawTextW(memDC,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		int bx = centerX - barW/2; int by = cy + (int)(14*ui_scale); RECT bar{bx,by,bx+barW,by+barH}; HBRUSH bb=CreateSolidBrush(RGB(50,60,80)); FillRect(memDC,&bar,bb); DeleteObject(bb);
		double t = double(value - minv)/double(maxv - minv); RECT fill{bx,by,bx+(int)(barW*t),by+barH}; HBRUSH bf=CreateSolidBrush(hot?RGB(120,180,255):RGB(90,120,180)); FillRect(memDC,&fill,bf); DeleteObject(bf);
	};
	drawExtraSlider(L"RR Start Bounce: " + std::to_wstring(settings_->pt_rr_start_bounce), settings_->pt_rr_start_bounce, 1, 16, cyRRStart, rrStartHot);
	drawExtraSlider(L"RR Min Prob %: " + std::to_wstring(settings_->pt_rr_min_prob_pct), settings_->pt_rr_min_prob_pct, 1, 90, cyRRMin, rrMinHot);
	drawExtraSlider(L"Fan-Out Ray Cap: " + std::to_wstring(settings_->pt_fanout_cap), settings_->pt_fanout_cap, 1000, 10000000, cyFanoutCap, fanCapHot);

	// Bottom panel
	RECT panelR { (int)(30*ui_scale), panelTop, winW - (int)(30*ui_scale), winH - (int)(6*ui_scale) };
	HBRUSH pb = CreateSolidBrush(RGB(22,22,34)); FillRect(memDC,&panelR,pb); DeleteObject(pb); FrameRect(memDC,&panelR,(HBRUSH)GetStockObject(GRAY_BRUSH));
	int btnAreaH = (int)(48*ui_scale); RECT btnRow { panelR.left + (int)(12*ui_scale), panelR.top + (int)(10*ui_scale), panelR.right - (int)(12*ui_scale), panelR.top + btnAreaH };
	int btnGap = (int)(20*ui_scale); int btnW = (btnRow.right - btnRow.left - btnGap)/2;
	RECT resetBtnR { btnRow.left, btnRow.top, btnRow.left + btnW, btnRow.bottom };
	RECT saveBtnR  { resetBtnR.right + btnGap, btnRow.top, resetBtnR.right + btnGap + btnW, btnRow.bottom };
	auto drawButton=[&](RECT r,const wchar_t* label,bool hot,COLORREF base,COLORREF hotCol){ HBRUSH bb=CreateSolidBrush(hot?hotCol:base); FillRect(memDC,&r,bb); DeleteObject(bb); FrameRect(memDC,&r,(HBRUSH)GetStockObject(GRAY_BRUSH)); RECT tr=r; SetBkMode(memDC,TRANSPARENT); SetTextColor(memDC,RGB(235,235,245)); DrawTextW(memDC,label,-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE); };
	bool hoverReset = (mouse_x>=resetBtnR.left && mouse_x<=resetBtnR.right && mouse_y>=resetBtnR.top && mouse_y<=resetBtnR.bottom);
	bool hoverSave  = (mouse_x>=saveBtnR.left  && mouse_x<=saveBtnR.right  && mouse_y>=saveBtnR.top  && mouse_y<=saveBtnR.bottom);
	if (saveFeedbackTicks_>0) saveFeedbackTicks_--;
	const wchar_t* saveLabel = (saveFeedbackTicks_>0)?L"Saved":L"Save Settings";
	drawButton(resetBtnR, L"Reset Defaults", hoverReset, RGB(60,35,35), RGB(90,50,50));
	drawButton(saveBtnR,  saveLabel, hoverSave,  RGB(35,55,70), RGB(55,85,110));
	RECT legendR { panelR.left + (int)(10*ui_scale), btnRow.bottom + (int)(6*ui_scale), panelR.right - (int)(10*ui_scale), panelR.bottom - (int)(10*ui_scale) };
	std::wstring legend = L"Enter=Close  Esc=Cancel  Arrows/Drag adjust  PgUp/PgDn/Wheel  Ctrl+Click numeric";
	SetTextColor(memDC, RGB(200,200,215)); DrawTextW(memDC, legend.c_str(), -1, &legendR, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

	// Simple tooltip mapping
	// Tooltip content (keyboard selection based)
	std::wstring tipSel;
	auto tooltipForIndex=[&](int idx)->std::wstring{
		if(idx < sliderCount){
			switch(idx){
				case 0: return L"Rays / Frame: Primary samples each frame.";
				case 1: return L"Max Bounces: Path depth cap.";
				case 2: return L"Internal Scale: Internal resolution %.";
				case 3: return L"Metal Roughness: Highlight spread.";
				case 4: return L"Emissive %: Light intensity.";
				case 5: return L"Accum Alpha: History blend factor.";
				case 6: return L"Denoise %: 3x3 blur strength.";
				// case 7 removed (segment samples)
			}
		} else if(idx==idxForce_()) return L"Force 1 Ray: RaysPerFrame treated as per‑pixel.";
		else if(idx==idxCamera_()) return L"Camera: Ortho or Perspective.";
		else if(idx==idxRREnable_()) return L"Russian Roulette enable toggle.";
		else if(idx==idxRRStart_()) return L"RR Start: Bounce to begin termination.";
		else if(idx==idxRRMin_()) return L"RR Min Prob: Survival probability clamp.";
			else if(idx==idxPBREnable_()) return L"Physically Based: Energy conserving diffuse + Fresnel specular.";
			else if(idx==idxFanoutEnable_()) return L"Fan-Out Mode: Exponential combinatorial rays (dangerous).";
					else if(idx==idxFanoutCap_()) return L"Fan-Out Cap: Safety limit on total rays spawned.";
					else if(idx==idxFanoutAbort_()) return L"Abort On Cap: Stop spawning when limit reached.";
		// Segment tracer tooltip removed
		else if(idx==idxReset_()) return L"Reset defaults (non‑destructive until Save).";
		return L"";
	};
	if(sel_>=0) tipSel = tooltipForIndex(sel_);

	// Hover detection for mouse tooltip (independent of selection)
	int hoverIdx = -1;
	// Slider hover bars and labels
	for(int i=0;i<sliderCount;i++){
		int y = baseY + i*rowH; int bx = centerX - barW/2; int byLblTop = y - (int)(20*ui_scale); int byBarTop = y + (int)(4*ui_scale); RECT bar{bx, y + (int)(14*ui_scale + 0.5), bx+barW, y + (int)(14*ui_scale + 0.5) + barH};
		RECT labelR{ centerX - barW/2, y - (int)(24*ui_scale), centerX + barW/2, y };
		if(mouse_x>=bar.left && mouse_x<=bar.right && mouse_y>=bar.top && mouse_y<=bar.bottom) hoverIdx=i; else if(mouse_x>=labelR.left && mouse_x<=labelR.right && mouse_y>=labelR.top && mouse_y<=labelR.bottom) hoverIdx=i;
	}
	// Checkbox style entries
	auto hoCheck=[&](int cyLine,int idx){ RECT r{centerX - barW/2, cyLine - (int)(18*ui_scale), centerX + barW/2, cyLine + (int)(18*ui_scale)}; if(mouse_x>=r.left && mouse_x<=r.right && mouse_y>=r.top && mouse_y<=r.bottom) hoverIdx=idx; };
	hoCheck(cyForce, idxForce_()); hoCheck(cyCam, idxCamera_()); hoCheck(cyRRE, idxRREnable_()); hoCheck(cyRRStart, idxRRStart_()); hoCheck(cyRRMin, idxRRMin_()); hoCheck(cyPBR, idxPBREnable_()); hoCheck(cyFanoutEnable, idxFanoutEnable_()); hoCheck(cyFanoutAbort, idxFanoutAbort_());
	// Buttons (assign reset/save indices just for tooltip mapping)
	int resetIdx = idxReset_(); if(mouse_x>=resetBtnR.left && mouse_x<=resetBtnR.right && mouse_y>=resetBtnR.top && mouse_y<=resetBtnR.bottom) hoverIdx=resetIdx; 
	// (save button intentionally reuses same tooltip as its own action text)

	std::wstring tip = tooltipForIndex( (hoverIdx!=-1)? hoverIdx : sel_ );
	if(!tip.empty()){
		SIZE sz{0,0}; GetTextExtentPoint32W(memDC, tip.c_str(), (int)tip.size(), &sz);
		int pad=(int)(6*ui_scale); int tx = mouse_x + (int)(20*ui_scale); int ty = mouse_y + (int)(24*ui_scale);
		if(tx + sz.cx + pad*2 > winW) tx = winW - sz.cx - pad*2; if(ty + sz.cy + pad*2 > winH) ty = winH - sz.cy - pad*2;
		RECT tr{tx,ty,tx+sz.cx+pad*2,ty+sz.cy+pad*2}; HBRUSH tb=CreateSolidBrush(RGB(32,36,54)); FillRect(memDC,&tr,tb); DeleteObject(tb); FrameRect(memDC,&tr,(HBRUSH)GetStockObject(GRAY_BRUSH));
		SetTextColor(memDC, RGB(210,220,235)); RECT txtr{tr.left+pad,tr.top+pad,tr.right-pad,tr.bottom-pad}; DrawTextW(memDC, tip.c_str(), (int)tip.size(), &txtr, DT_LEFT|DT_TOP|DT_NOPREFIX|DT_SINGLELINE);
	}

	// Keyboard navigation
	if (input.just_pressed(VK_DOWN)) { sel_++; clampSel(); }
	if (input.just_pressed(VK_UP)) { sel_--; clampSel(); }
	if (sel_ < kBaseSliderCount) {
		if (input.just_pressed(VK_LEFT)) { *sliders[sel_].val = std::max(sliders[sel_].minv, *sliders[sel_].val - sliders[sel_].step); changedSinceOpen_ = true; }
		if (input.just_pressed(VK_RIGHT)) { *sliders[sel_].val = std::min(sliders[sel_].maxv, *sliders[sel_].val + sliders[sel_].step); changedSinceOpen_ = true; }
	} else if (sel_==idxForce_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_force_full_pixel_rays = settings_->pt_force_full_pixel_rays?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxCamera_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_use_ortho = settings_->pt_use_ortho?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxRREnable_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_rr_enable = settings_->pt_rr_enable?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxPBREnable_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_pbr_enable = settings_->pt_pbr_enable?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxFanoutEnable_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_fanout_enable = settings_->pt_fanout_enable?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxFanoutAbort_()) {
		if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) { settings_->pt_fanout_abort = settings_->pt_fanout_abort?0:1; changedSinceOpen_ = true; }
	} else if (sel_==idxRRStart_()) {
		if (input.just_pressed(VK_LEFT)) { settings_->pt_rr_start_bounce = std::max(1, settings_->pt_rr_start_bounce - 1); changedSinceOpen_ = true; }
		if (input.just_pressed(VK_RIGHT)) { settings_->pt_rr_start_bounce = std::min(16, settings_->pt_rr_start_bounce + 1); changedSinceOpen_ = true; }
	} else if (sel_==idxRRMin_()) {
		if (input.just_pressed(VK_LEFT)) { settings_->pt_rr_min_prob_pct = std::max(1, settings_->pt_rr_min_prob_pct - 1); changedSinceOpen_ = true; }
		if (input.just_pressed(VK_RIGHT)) { settings_->pt_rr_min_prob_pct = std::min(90, settings_->pt_rr_min_prob_pct + 1); changedSinceOpen_ = true; }
	}
	if (input.just_pressed(VK_PRIOR)) { scrollOffset_ -= (int)(winH*0.5); if (scrollOffset_<0) scrollOffset_=0; }
	if (input.just_pressed(VK_NEXT)) { scrollOffset_ += (int)(winH*0.5); if (scrollOffset_>maxScroll_) scrollOffset_=maxScroll_; }
	if (input.just_pressed(VK_ESCAPE)) { *settings_ = original_; changedSinceOpen_ = false; return Action::Cancel; }
	if (input.just_pressed(VK_RETURN)) { return Action::Commit; }

	// Ctrl+Click numeric entry (simplified: only when clicking slider bar area)
	if (last_click_x!=-1 && (GetKeyState(VK_CONTROL) & 0x8000)) {
		int mx = last_click_x; int my = last_click_y; last_click_x = last_click_y = -1;
		// try each slider bar
		for (int i=0;i<sliderCount;i++) {
			int y = baseY + i*rowH; int bx = centerX - barW/2; int by = y + (int)(14*ui_scale + 0.5); RECT bar{bx,by,bx+barW,by+barH};
			if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) {
				HWND edit = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW, L"EDIT", std::to_wstring(*sliders[i].val).c_str(), WS_VISIBLE|WS_CHILD|ES_LEFT,
										   bx, by - 28, 160, 24, hwnd_, NULL, hInstance_, NULL);
				if (edit) {
					bool done=false; std::wstring buffer; while(!done) { MSG em; while(PeekMessage(&em,nullptr,0,0,PM_REMOVE)){ if (em.message==WM_KEYDOWN && em.wParam==VK_RETURN){done=true;} else if (em.message==WM_KEYDOWN && em.wParam==VK_ESCAPE){buffer.clear();done=true;} TranslateMessage(&em); DispatchMessage(&em);} Sleep(10); }
					int len = GetWindowTextLengthW(edit); if (len>0){ buffer.resize(len+1); GetWindowTextW(edit,&buffer[0],len+1); buffer.resize(len);} DestroyWindow(edit);
					if (!buffer.empty()) { try { int nv = std::stoi(buffer); if (nv<sliders[i].minv) nv=sliders[i].minv; if (nv>sliders[i].maxv) nv=sliders[i].maxv; *sliders[i].val=nv; sel_=i; changedSinceOpen_=true; } catch(...) {} }
				}
				break;
			}
		}
	}

	// Mouse drag sliders
	if (mouse_pressed && mouse_y < panelTop) {
		for (int i=0;i<sliderCount;i++) {
			int y = baseY + i*rowH; int bx = centerX - barW/2; int by = y + (int)(14*ui_scale + 0.5); RECT bar{bx,by,bx+barW,by+barH};
			if (mouse_x>=bar.left && mouse_x<=bar.right && mouse_y>=bar.top && mouse_y<=bar.bottom) {
				double tt = double(mouse_x - bar.left)/barW; if (tt<0) tt=0; if (tt>1) tt=1; int val = sliders[i].minv + (int)(tt*(sliders[i].maxv - sliders[i].minv) + 0.5); val = (val/sliders[i].step)*sliders[i].step; if (val<sliders[i].minv) val=sliders[i].minv; if (val>sliders[i].maxv) val=sliders[i].maxv; *sliders[i].val = val; sel_=i; changedSinceOpen_=true; }
		}
		// RR Start bounce
		{
			int bx = centerX - barW/2; int by = (baseY + (kBaseSliderCount+3)*rowH) + (int)(14*ui_scale + 0.5); RECT bar{bx,by,bx+barW,by+barH};
			if (mouse_x>=bar.left && mouse_x<=bar.right && mouse_y>=bar.top && mouse_y<=bar.bottom) { double tt=double(mouse_x-bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1; int val = 1 + (int)(tt*(16-1)+0.5); if(val<1) val=1; if(val>16) val=16; settings_->pt_rr_start_bounce=val; sel_=idxRRStart_(); changedSinceOpen_=true; }
		}
		// RR Min prob
		{
			int bx = centerX - barW/2; int by = (baseY + (kBaseSliderCount+4)*rowH) + (int)(14*ui_scale + 0.5); RECT bar{bx,by,bx+barW,by+barH};
			if (mouse_x>=bar.left && mouse_x<=bar.right && mouse_y>=bar.top && mouse_y<=bar.bottom) { double tt=double(mouse_x-bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1; int val = 1 + (int)(tt*(90-1)+0.5); if(val<1) val=1; if(val>90) val=90; settings_->pt_rr_min_prob_pct=val; sel_=idxRRMin_(); changedSinceOpen_=true; }
		}
		// Fan-out cap slider
		{
			int bx = centerX - barW/2; int by = (baseY + (kBaseSliderCount+6)*rowH) + (int)(14*ui_scale + 0.5); RECT bar{bx,by,bx+barW,by+barH};
			if (mouse_x>=bar.left && mouse_x<=bar.right && mouse_y>=bar.top && mouse_y<=bar.bottom) { double tt=double(mouse_x-bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1; int val = 1000 + (int)(tt*(10000000-1000)+0.5); if(val<1000) val=1000; if(val>10000000) val=10000000; settings_->pt_fanout_cap=val; sel_=idxFanoutCap_(); changedSinceOpen_=true; }
		}
	}

	// Mouse click checkboxes / buttons (on release) using last_click_x/y
	if (last_click_x != -1) {
		int cx = last_click_x; int cy = last_click_y; last_click_x = last_click_y = -1;
		auto hitMid = [&](int cyLine){ RECT r{centerX - (int)(220*ui_scale), cyLine - (int)(16*ui_scale), centerX + (int)(220*ui_scale), cyLine + (int)(16*ui_scale)}; return (cx>=r.left && cx<=r.right && cy>=r.top && cy<=r.bottom); };
		if (cy < panelTop && hitMid(cyForce)) { settings_->pt_force_full_pixel_rays = settings_->pt_force_full_pixel_rays?0:1; sel_=idxForce_(); changedSinceOpen_=true; }
		else if (cy < panelTop && hitMid(cyCam))   { settings_->pt_use_ortho = settings_->pt_use_ortho?0:1; sel_=idxCamera_(); changedSinceOpen_=true; }
		else if (cy < panelTop && hitMid(cyRRE))   { settings_->pt_rr_enable = settings_->pt_rr_enable?0:1; sel_=idxRREnable_(); changedSinceOpen_=true; }
		else if (cy < panelTop && hitMid(cyPBR))   { settings_->pt_pbr_enable = settings_->pt_pbr_enable?0:1; sel_=idxPBREnable_(); changedSinceOpen_=true; }
		else if (cy < panelTop && hitMid(cyFanoutEnable)) { settings_->pt_fanout_enable = settings_->pt_fanout_enable?0:1; sel_=idxFanoutEnable_(); changedSinceOpen_=true; }
		else if (cy < panelTop && hitMid(cyFanoutAbort)) { settings_->pt_fanout_abort = settings_->pt_fanout_abort?0:1; sel_=idxFanoutAbort_(); changedSinceOpen_=true; }
		// Segment tracer click removed
		else if (cy >= panelTop) {
			if (cx>=resetBtnR.left && cx<=resetBtnR.right && cy>=resetBtnR.top && cy<=resetBtnR.bottom) { resetDefaults(); sel_=idxReset_(); original_ = *settings_; }
			else if (cx>=saveBtnR.left && cx<=saveBtnR.right && cy>=saveBtnR.top && cy<=saveBtnR.bottom) { settingsMgr_->save(*exeDir_ + L"settings.json", *settings_); original_ = *settings_; saveFeedbackTicks_ = 60; }
		}
	}

	// Simple tooltip omitted in this first extraction (can be re-added modularly later)
	return Action::None;
}
