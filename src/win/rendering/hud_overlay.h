#pragma once
#include <windows.h>
struct GameState; struct SRStats; struct UIState;
class HudOverlay {
public:
	void draw(const GameState&, const SRStats*, HDC dc, int w, int h, int dpi, int highScore);
};
