/**
 * @file settings_panel.h
 * @brief Path tracer settings panel for Windows GUI
 * 
 * Path Tracer Settings Panel abstraction extracted from monolithic game_win.cpp.
 * Handles rendering and interaction for path tracer configuration settings.
 */

// Path Tracer Settings Panel abstraction extracted from monolithic game_win.cpp
// Responsibilities:
//  - Render all path tracer tunables (sliders, checkboxes, buttons, tooltips)
//  - Handle interaction via edge-detected InputState + mouse state provided by caller
//  - Persist settings file immediately on Save button; accumulate change flag for caller otherwise
//  - Provide Commit (Enter) / Cancel (Esc) semantics with baseline restore on cancel

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "../../win/settings.h"

// Settings already included above
class SettingsManager;      // forward
struct InputState;          // from input_state.h

class SettingsPanel {
public:
	enum class Action { None, Commit, Cancel };

	void begin(HWND hwnd, HINSTANCE hInstance, Settings* settings, SettingsManager* mgr, const std::wstring* exeDirPath);

	// Run one frame of the panel. Returns action when the panel should close.
	// mouse_wheel_delta and last_click_{x,y} are consumed / mutated similarly to prior inline modal logic.
	Action frame(HDC memDC,
				 int winW, int winH, int dpi,
				 const InputState& input,
				 int mouse_x, int mouse_y, bool mouse_pressed,
				 int& mouse_wheel_delta,
				 int& last_click_x, int& last_click_y);

	bool anyChangesSinceOpen() const { return changedSinceOpen_; }

private:
	// bound context
	HWND hwnd_ = nullptr;
	HINSTANCE hInstance_ = nullptr;
	Settings* settings_ = nullptr;
	SettingsManager* settingsMgr_ = nullptr;
	const std::wstring* exeDir_ = nullptr; // path prefix for settings.json

	// modal state
	Settings* baselinePtr_ = nullptr; // pointer to settings_ (for clarity)
	Settings  original_;              // baseline copy for Cancel
	bool changedSinceOpen_ = false;
	int sel_ = 0;                     // selection index
	int scrollOffset_ = 0;
	int maxScroll_ = 0;
	int saveFeedbackTicks_ = 0;

	// Slider meta
	struct SliderInfo { const wchar_t* label; int *val; int minv; int maxv; int step; };
	static constexpr int kBaseSliderCount = 10; // + recording FPS slider

	// index mapping after base sliders
	int idxForce_() const { return kBaseSliderCount; }
	int idxCamera_() const { return kBaseSliderCount + 1; }
	int idxForce4Wide_() const { return kBaseSliderCount + 2; }
	int idxRREnable_() const { return kBaseSliderCount + 3; }
	int idxRRStart_() const { return kBaseSliderCount + 4; }
	int idxRRMin_() const { return kBaseSliderCount + 5; }
	int idxPBREnable_() const { return kBaseSliderCount + 6; }
	int idxFanoutEnable_() const { return kBaseSliderCount + 7; }
	int idxFanoutCap_() const { return kBaseSliderCount + 8; }
	int idxFanoutAbort_() const { return kBaseSliderCount + 9; }
	int idxReset_() const { return kBaseSliderCount + 10; }
	int totalItems_() const { return kBaseSliderCount + 10; }

	void clampSel();
	void resetDefaults();
};
