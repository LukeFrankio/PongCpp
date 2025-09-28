// Main menu view abstraction extracted from monolithic game_win.cpp
// Responsible for:
//  - Drawing configuration menu (control mode, AI difficulty, renderer mode, path tracer settings entry)
//  - Handling keyboard + mouse interaction (using edge-detected InputState provided by caller)
//  - Applying changes directly to Settings & associated mode enums
//  - Emitting high level actions (Play, Open Settings Modal, High Scores Modal, Quit)
//  - Presenting a subset of the high score list (top 5) for context
//
// Design notes:
//  - The view is deliberately stateless with respect to timing; caller drives frame loop
//  - Menu selection (menuIndex) is kept externally so re-entry menus can preserve last selection
//  - Click hit testing is performed here; caller passes & then resets menu_click_index managed by WindowProc
//  - We avoid owning Windows resources (fonts, HDC); caller supplies HDC already set up (font selected)

// Ensure Windows headers don't define min/max macros that break std::max/std::min
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once
#include <windows.h>
#include <optional>
#include <vector>
#include <string>

struct Settings;              // from settings.h
class SettingsManager;        // forward decl
class HighScores;             // forward decl
struct HighScoreEntry;        // from highscores.h
struct InputState;            // forward decl (edge-detected input snapshot)

// High level semantic actions the menu can request
enum class MenuAction { Play, Settings, Scores, Quit, Back };

class MainMenuView {
public:
	struct Result {
		std::optional<MenuAction> action; // set when user triggers play/settings/scores/quit
		bool settingsChanged = false;     // control / ai / renderer toggles modified settings & need persistence
	};

	void init(Settings* settings,
			  SettingsManager* settingsMgr,
			  HighScores* hsMgr,
			  std::vector<HighScoreEntry>* highList,
			  const std::wstring* highScorePath,
			  const std::wstring* exeDirPath);

	// Render + process one frame of the main menu.
	// Parameters:
	//  - memDC: back buffer DC (font + bk mode already configured)
	//  - winW / winH: current window size
	//  - dpi: current window DPI for scaling
	//  - input: edge-detected input snapshot (after message pump & new_frame())
	//  - ctrlMode / aiDiff / rendererMode: references so we can mutate caller's mode variables
	//  - menuIndex: current highlighted index (0..6)
	//  - menu_click_index: index captured by WindowProc on mouse click press (will be consumed/reset)
	Result updateAndRender(HDC memDC,
						   int winW,
						   int winH,
						   int dpi,
						   const InputState& input,
						   int& ctrlMode,
						   int& aiDiff,
						   int& rendererMode,
						   int& menuIndex,
						   int& menu_click_index,
						   bool suppressClickDown);

private:
	// Bound external model pointers
	Settings* settings_ = nullptr;
	SettingsManager* settingsMgr_ = nullptr;
	HighScores* hsMgr_ = nullptr;
	std::vector<HighScoreEntry>* highList_ = nullptr;
	const std::wstring* hsPath_ = nullptr;
	const std::wstring* exeDir_ = nullptr; // used for future expansion (e.g., assets)

	// helpers
	void drawTextCentered(HDC hdc, const std::wstring& text, int x, int y);
};
