#include "main_menu_view.h"
#include "../settings.h"
#include "../highscores.h"
#include "../input/input_state.h"
#include <algorithm>
#include <cmath>

void MainMenuView::init(Settings* settings,
						SettingsManager* settingsMgr,
						HighScores* hsMgr,
						std::vector<HighScoreEntry>* highList,
						const std::wstring* highScorePath,
						const std::wstring* exeDirPath) {
	settings_ = settings;
	settingsMgr_ = settingsMgr;
	hsMgr_ = hsMgr;
	highList_ = highList;
	hsPath_ = highScorePath;
	exeDir_ = exeDirPath;
}

void MainMenuView::drawTextCentered(HDC hdc, const std::wstring& text, int x, int y) {
	RECT r { x - 400, y - 16, x + 400, y + 16 };
	DrawTextW(hdc, text.c_str(), (int)text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

MainMenuView::Result MainMenuView::updateAndRender(HDC memDC,
												   int winW,
												   int winH,
												   int dpi,
												   const InputState& input,
												   int& ctrlMode,
												   int& aiDiff,
												   int& rendererMode,
												   int& menuIndex,
												   int& menu_click_index,
												   bool suppressClickDown) {
	Result result; // default empty
	if (!settings_ || !settingsMgr_ || !highList_ || !hsMgr_ || !hsPath_) return result;

	double ui_scale = (double)dpi / 96.0;

	// Background gradient (two rects)
	RECT rtop {0,0,winW, winH/2}; RECT rbot {0, winH/2, winW, winH};
	HBRUSH btop = CreateSolidBrush(RGB(20,20,30));
	HBRUSH bbot = CreateSolidBrush(RGB(10,10,20));
	FillRect(memDC, &rtop, btop); FillRect(memDC, &rbot, bbot);
	DeleteObject(btop); DeleteObject(bbot);

	SetTextColor(memDC, RGB(220,220,220));
	SetBkMode(memDC, TRANSPARENT);
	drawTextCentered(memDC, L"Pong - Configuration", winW/2, (int)(40*ui_scale + 0.5));

	// Hover detection
	// Rely on last known InputState mouse (mx,my) provided by caller
	int mx = input.mx; int my = input.my;
	int hoverIndex = -1;
	int baseX = winW/2 - (int)(170*ui_scale + 0.5);
	// 0 control,1 AI diff,2 renderer,3 game mode button,4 player mode,5 physics mode,6 HUD gameplay,7 HUD record,8 speed mode,9 path settings,10 start,11 scores,12 quit,13 recording
	const int kCount = 14;
	int ys[kCount] = { (int)(120 * ui_scale + 0.5), (int)(165 * ui_scale + 0.5), (int)(210 * ui_scale + 0.5), (int)(255 * ui_scale + 0.5), (int)(300 * ui_scale + 0.5), (int)(345 * ui_scale + 0.5), (int)(390 * ui_scale + 0.5), (int)(435 * ui_scale + 0.5), (int)(480 * ui_scale + 0.5), (int)(525 * ui_scale + 0.5), (int)(570 * ui_scale + 0.5), (int)(615 * ui_scale + 0.5), (int)(660 * ui_scale + 0.5), (int)(705 * ui_scale + 0.5) };
	for (int i=0;i<kCount;i++) {
	int pad = (int)((10.0 * ui_scale > 6.0)? (10.0 * ui_scale) : 6.0);
	int wboxRef = (int)(260.0 * ui_scale + 0.5);
	if (wboxRef < 260) wboxRef = 260;
	RECT rb { baseX - pad, ys[i] - (int)(6*ui_scale + 0.5), baseX + wboxRef, ys[i] + (int)(34*ui_scale + 0.5) };
		if (mx >= rb.left && mx <= rb.right && my >= rb.top && my <= rb.bottom) { hoverIndex = i; break; }
	}

	// Drawing helper
	auto drawOption = [&](int idx, const std::wstring &text, int x, int y){
		int pad = (int)((10.0 * ui_scale > 6.0)? (10.0 * ui_scale) : 6.0);
		int wbox = (int)(260.0 * ui_scale + 0.5); if (wbox < 260) wbox = 260;
		RECT rb {x - pad, y - (int)(6*ui_scale + 0.5), x + wbox, y + (int)(34*ui_scale + 0.5)};
		if (menuIndex == idx) {
			HBRUSH sel = CreateSolidBrush(RGB(60,60,90)); FillRect(memDC, &rb, sel); DeleteObject(sel); SetTextColor(memDC, RGB(255,255,200));
		} else if (hoverIndex == idx) {
			HBRUSH sel = CreateSolidBrush(RGB(40,40,70)); FillRect(memDC, &rb, sel); DeleteObject(sel); SetTextColor(memDC, RGB(230,230,200));
		} else SetTextColor(memDC, RGB(200,200,200));
		drawTextCentered(memDC, text, (rb.left + rb.right)/2, (rb.top + rb.bottom)/2);
	};

	// Acquire current game mode string from settings (use new config system)
	std::wstring gmLabel = L"Game Mode: ";
	std::string desc = settings_->mode_config.getDescription();
	for (char c : desc) gmLabel += (wchar_t)c;
	
	std::wstring pmodeLabel;
	int pm = settings_->player_mode;
	if(pm<0||pm>2) pm=0;
	pmodeLabel = (pm==0)?L"Players: 1P vs AI" : (pm==1? L"Players: 2P" : L"Players: AI vs AI");
	drawOption(0, (ctrlMode==0)?L"Control: Keyboard":L"Control: Mouse", baseX, ys[0]);
	drawOption(1, (aiDiff==0)?L"AI: Easy":(aiDiff==1)?L"AI: Normal":L"AI: Hard", baseX, ys[1]);
	drawOption(2, (rendererMode==0)?L"Renderer: Classic":L"Renderer: Path Tracer", baseX, ys[2]);
	drawOption(3, gmLabel + L" (Click to Configure)", baseX, ys[3]);
	drawOption(4, pmodeLabel, baseX, ys[4]);
	drawOption(5, settings_->physics_mode?L"Physics: Physical":L"Physics: Arcade", baseX, ys[5]);
	drawOption(6, settings_->hud_show_play?L"HUD Gameplay: ON":L"HUD Gameplay: OFF", baseX, ys[6]);
	drawOption(7, settings_->hud_show_record?L"HUD Recording: ON":L"HUD Recording: OFF", baseX, ys[7]);
	drawOption(8, settings_->speed_mode?L"Speed Mode: I AM SPEED":L"Speed Mode: Normal", baseX, ys[8]);
	drawOption(9, L"Path Tracer Settings...", baseX, ys[9]);
	drawOption(10, L"Start Game", baseX, ys[10]);
	drawOption(11, L"Manage High Scores", baseX, ys[11]);
	drawOption(12, L"Quit", baseX, ys[12]);
	drawOption(13, settings_->recording_mode ? L"Recording Mode: ON" : L"Recording Mode: OFF", baseX, ys[13]);

	// High scores right side (top 5)
	SetTextColor(memDC, RGB(180,180,220));
	drawTextCentered(memDC, L"High Scores", winW - (int)(220*ui_scale + 0.5), (int)(60*ui_scale + 0.5));
	for (size_t i=0;i<highList_->size() && i<5;i++) {
		const auto &e = (*highList_)[i];
		std::wstring line = std::to_wstring(i+1) + L"  " + e.name + L"  " + std::to_wstring(e.score);
	drawTextCentered(memDC, line, winW - (int)(220*ui_scale + 0.5), (int)(100*ui_scale + 0.5) + (int)(i*30*ui_scale));
	}

	// Keyboard navigation
	// Mouse click: if user released button this frame (InputState.click set on WM_LBUTTONUP)
	if (input.click && hoverIndex != -1) {
		// Adopt hovered index as active selection and mark for action processing
		menuIndex = hoverIndex;
		menu_click_index = hoverIndex;
	}

	if (input.just_pressed(VK_DOWN)) { menuIndex++; if (menuIndex>13) menuIndex=13; }
	if (input.just_pressed(VK_UP)) { menuIndex--; if (menuIndex<0) menuIndex=0; }
	if (input.just_pressed(VK_LEFT)) {
		if (menuIndex==0) { ctrlMode = 0; settings_->control_mode = 0; result.settingsChanged = true; }
		else if (menuIndex==1) { if (aiDiff>0) { aiDiff--; settings_->ai = aiDiff; result.settingsChanged = true; } }
		else if (menuIndex==2) { rendererMode = 0; settings_->renderer = 0; result.settingsChanged = true; }
		else if (menuIndex==4) { if (settings_->player_mode>0) { settings_->player_mode--; result.settingsChanged = true; } }
		else if (menuIndex==5) { settings_->physics_mode = 0; result.settingsChanged = true; }
		else if (menuIndex==6) { settings_->hud_show_play = 0; result.settingsChanged = true; }
		else if (menuIndex==7) { settings_->hud_show_record = 0; result.settingsChanged = true; }
		else if (menuIndex==8) { settings_->speed_mode = 0; result.settingsChanged = true; }
	}
	if (input.just_pressed(VK_RIGHT)) {
		if (menuIndex==0) { ctrlMode = 1; settings_->control_mode = 1; result.settingsChanged = true; }
		else if (menuIndex==1) { if (aiDiff<2) { aiDiff++; settings_->ai = aiDiff; result.settingsChanged = true; } }
		else if (menuIndex==2) { rendererMode = 1; settings_->renderer = 1; result.settingsChanged = true; }
		else if (menuIndex==4) { if (settings_->player_mode<2) { settings_->player_mode++; result.settingsChanged = true; } }
		else if (menuIndex==5) { settings_->physics_mode = 1; result.settingsChanged = true; }
		else if (menuIndex==6) { settings_->hud_show_play = 1; result.settingsChanged = true; }
		else if (menuIndex==7) { settings_->hud_show_record = 1; result.settingsChanged = true; }
		else if (menuIndex==8) { settings_->speed_mode = 1; result.settingsChanged = true; }
	}
	if (input.just_pressed(VK_ESCAPE)) { result.action = MenuAction::Quit; }
	if (input.just_pressed(VK_RETURN)) {
		switch(menuIndex) {
			case 3: result.action = MenuAction::GameMode; break; // Open game mode settings
			case 9: if (rendererMode==1) result.action = MenuAction::Settings; break; // path tracer settings
			case 10: result.action = MenuAction::Play; break;
			case 11: result.action = MenuAction::Scores; break;
			case 12: result.action = MenuAction::Quit; break;
			case 13: settings_->recording_mode = settings_->recording_mode ? 0 : 1; result.settingsChanged = true; break;
		}
	}

	// Mouse click consumption (index captured at WM_LBUTTONDOWN unless suppressed)
	if (menu_click_index != -1) {
		int clicked = menu_click_index; menu_click_index = -1;
		switch(clicked) {
			case 0: ctrlMode = (ctrlMode==0)?1:0; settings_->control_mode = ctrlMode; result.settingsChanged = true; break;
			case 1: aiDiff = (aiDiff+1)%3; settings_->ai = aiDiff; result.settingsChanged = true; break;
			case 2: rendererMode = (rendererMode==0)?1:0; settings_->renderer = rendererMode; result.settingsChanged = true; break;
			case 3: result.action = MenuAction::GameMode; break; // Open game mode settings
			case 4: settings_->player_mode = (settings_->player_mode+1)%3; result.settingsChanged = true; break;
			case 5: settings_->physics_mode = settings_->physics_mode?0:1; result.settingsChanged = true; break;
			case 6: settings_->hud_show_play = settings_->hud_show_play?0:1; result.settingsChanged = true; break;
			case 7: settings_->hud_show_record = settings_->hud_show_record?0:1; result.settingsChanged = true; break;
			case 8: settings_->speed_mode = settings_->speed_mode?0:1; result.settingsChanged = true; break;
			case 9: if (rendererMode==1) result.action = MenuAction::Settings; break;
			case 10: result.action = MenuAction::Play; break;
			case 11: result.action = MenuAction::Scores; break;
			case 12: result.action = MenuAction::Quit; break;
			case 13: settings_->recording_mode = settings_->recording_mode ? 0 : 1; result.settingsChanged = true; break;
		}
	}

	// Tooltip (restored behaviour): small box near cursor, sized by text
	if(hoverIndex >= 0) {
		std::wstring tip;
		switch(hoverIndex){
			case 0: tip = L"Toggle control method"; break;
			case 1: tip = L"Cycle AI difficulty"; break;
			case 2: tip = L"Switch renderer"; break;
			case 3: tip = L"Open game mode settings (MultiBall, Obstacles, Black Holes, etc.)"; break;
			case 4: tip = L"Select player/AI configuration"; break;
			case 5: tip = L"Toggle physics model"; break;
			case 6: tip = L"Toggle HUD during gameplay"; break;
			case 7: tip = L"Toggle HUD while recording"; break;
			case 8: tip = L"Toggle speed mode (no max speed, auto-acceleration)"; break;
			case 9: tip = (rendererMode==1)?L"Open path tracer settings":L"(Enable path tracer to edit settings)"; break;
			case 10: tip = L"Start the game"; break;
			case 11: tip = L"View / delete high scores"; break;
			case 12: tip = L"Exit the game"; break;
			case 13: tip = settings_->recording_mode ? L"Disable recording mode" : L"Enable recording mode (records gameplay)"; break;
		}
		if(!tip.empty()) {
			SIZE ts{0,0}; GetTextExtentPoint32W(memDC, tip.c_str(), (int)tip.size(), &ts);
			int pad = (int)(6*ui_scale + 0.5);
			int tx = mx + (int)(18*ui_scale); int ty = my + (int)(22*ui_scale);
			// Keep inside window
			if(tx + ts.cx + pad*2 > winW) tx = winW - ts.cx - pad*2;
			if(ty + ts.cy + pad*2 > winH) ty = winH - ts.cy - pad*2;
			RECT tr{ tx, ty, tx + ts.cx + pad*2, ty + ts.cy + pad*2 };
			HBRUSH bb = CreateSolidBrush(RGB(40,40,70)); FillRect(memDC,&tr,bb); DeleteObject(bb);
			FrameRect(memDC,&tr,(HBRUSH)GetStockObject(GRAY_BRUSH));
			SetTextColor(memDC, RGB(235,235,240));
			RECT textR{ tr.left + pad, tr.top + pad, tr.right - pad, tr.bottom - pad };
			DrawTextW(memDC, tip.c_str(), (int)tip.size(), &textR, DT_LEFT|DT_TOP|DT_NOPREFIX|DT_SINGLELINE);
		}
	}

	// NOTE: persistence of settings is handled by caller (game_win.cpp) to centralize save path logic.
	// We intentionally do not call settingsMgr_->save here to avoid mixing concerns.
	return result;
}
