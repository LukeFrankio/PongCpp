#pragma once
#include <windows.h>
struct GameState;

// Classic GDI renderer for Pong gameplay.
// Stateless w.r.t game logic but caches GDI resources sized by DPI.
class ClassicRenderer {
public:
	ClassicRenderer();
	~ClassicRenderer();
	void render(const GameState&, HDC dc, int winW, int winH, int dpi);
	void onResize(int winW, int winH); // currently no-op but kept for future
private:
	void ensureResources(int dpi);
	int cachedDpi = 0;
	HPEN penThin = nullptr;
	HPEN penGlow = nullptr;
};
