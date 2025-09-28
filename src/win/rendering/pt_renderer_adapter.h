#pragma once
#include <windows.h>
#include "../soft_renderer.h" // for SRConfig, SRStats
class SoftRenderer; struct GameState; struct Settings; struct UIState; 
class PTRendererAdapter { public: PTRendererAdapter(); ~PTRendererAdapter(); void configure(const Settings&); void resize(int w,int h); void render(const GameState&, const Settings&, const UIState&, HDC target); const SRStats* stats() const; private: SoftRenderer* impl=nullptr; SRConfig cfg{}; };
