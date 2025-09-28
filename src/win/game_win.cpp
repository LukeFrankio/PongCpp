// Clean minimal reconstructed implementation (fully deduplicated)

#include "../game.h"
#include "game_win.h"
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cstdio>

#include "../core/game_core.h"
#include "highscores.h"
#include "settings.h"
#include "rendering/classic_renderer.h"
#include "rendering/pt_renderer_adapter.h"
#include "rendering/hud_overlay.h"
#include "ui/high_scores_view.h"
#include "ui/ui_state.h"
#include "app/game_session.h"
#include "platform/backbuffer.h"
#include "input/input_router.h"
#include "input/input_state.h"
#include "ui/main_menu_view.h"
#include "ui/settings_panel.h"

static inline int iround(double v) { return (int)(v >= 0.0 ? v + 0.5 : v - 0.5); }

static const wchar_t CLASS_NAME[] = L"PongWindowClass";

struct WinState {
    int  width  = 960;
    int  height = 720;
    int  dpi    = 96;
    bool running = true;
    bool request_menu = false;
    bool suppressMenuClickDown = false;
    int  mouse_x = 0, mouse_y = 0;
    int  last_click_x = -1, last_click_y = -1;
    bool mouse_pressed = false;
    int  mouse_wheel_delta = 0;
    int  ui_mode = 0; // 0 gameplay, 1 menu, 2 modal
    int  menu_click_index = -1;
    BackBuffer   *backBuf = nullptr;
    HDC           memDC   = nullptr;
    InputRouter  *inputRouter = nullptr;
    HFONT         uiFont = nullptr;
    HFONT         uiOldFont = nullptr;
};

static UINT query_dpi(HWND hwnd, int current) {
    if (current != 96) return current; // already queried
    HMODULE u = LoadLibraryW(L"user32.dll");
    if (u) {
        auto p = (UINT (WINAPI*)(HWND))GetProcAddress(u, "GetDpiForWindow");
        if (p) current = p(hwnd);
        FreeLibrary(u);
    }
    return current;
}

// DrawCentered removed; responsibility moved to overlay / views

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinState *s = (WinState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto cs = (CREATESTRUCT*)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0; }
    case WM_SIZE: if (s) { s->width = LOWORD(lParam); s->height = HIWORD(lParam); if (s->backBuf) { HDC wdc=GetDC(hwnd); s->backBuf->resize(wdc,s->width,s->height); s->memDC=s->backBuf->dc(); if (s->uiFont) SelectObject(s->memDC,s->uiFont); ReleaseDC(hwnd,wdc);} } return 0;
    case WM_MOUSEMOVE: if (s){ s->mouse_x=GET_X_LPARAM(lParam); s->mouse_y=GET_Y_LPARAM(lParam); if (s->inputRouter) s->inputRouter->handle(msg,wParam,lParam);} return 0;
    case WM_LBUTTONDOWN: if (s){ s->mouse_pressed=true; if(!s->suppressMenuClickDown) s->menu_click_index=-1; if(s->inputRouter) s->inputRouter->handle(msg,wParam,lParam);} return 0;
    case WM_LBUTTONUP: if (s){ s->mouse_pressed=false; s->last_click_x=GET_X_LPARAM(lParam); s->last_click_y=GET_Y_LPARAM(lParam); if(s->inputRouter) s->inputRouter->handle(msg,wParam,lParam);} return 0;
    case WM_MOUSEWHEEL: if (s){ s->mouse_wheel_delta += GET_WHEEL_DELTA_WPARAM(wParam); if(s->inputRouter) s->inputRouter->handle(msg,wParam,lParam);} return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: if (s && s->inputRouter) s->inputRouter->handle(msg,wParam,lParam); return 0;
    case WM_CLOSE: if (s) s->running=false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// Blocking HighScoresModal removed; replaced by HighScoresView (non-blocking)

int run_win_pong(HINSTANCE inst, int show) {
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.hInstance=inst; wc.lpszClassName=CLASS_NAME; wc.hCursor=LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    WinState st; HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"PongCpp", WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT,CW_USEDEFAULT, st.width, st.height, nullptr,nullptr,inst,&st); if(!hwnd) return -1; ShowWindow(hwnd, show);
    st.backBuf = new BackBuffer(); HDC wdc = GetDC(hwnd); st.backBuf->resize(wdc, st.width, st.height); st.memDC = st.backBuf->dc(); ReleaseDC(hwnd, wdc);
    st.inputRouter = new InputRouter(); st.dpi = query_dpi(hwnd, 96);
    st.uiFont = CreateFontW(-MulDiv(16, st.dpi, 96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
    if (st.memDC && st.uiFont) { st.uiOldFont = (HFONT)SelectObject(st.memDC, st.uiFont); SetBkMode(st.memDC, TRANSPARENT); }

    // Paths / persistence
    wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath; size_t slash = exeDir.find_last_of(L"/\\"); if (slash!=std::wstring::npos) exeDir = exeDir.substr(0, slash+1);
    SettingsManager settingsMgr; HighScores hsMgr; Settings settings = settingsMgr.load(exeDir + L"settings.json");
    std::wstring hsPath = exeDir + L"highscores.json"; std::vector<HighScoreEntry> highs = hsMgr.load(hsPath, 10);

    GameSession session; session.core().reset();
    enum ControlMode { CTRL_KEYBOARD, CTRL_MOUSE }; enum RendererMode { R_CLASSIC, R_PATH }; enum AIDiff { AI_EASY, AI_NORMAL, AI_HARD };
    ControlMode ctrl = (settings.control_mode==0)?CTRL_KEYBOARD:CTRL_MOUSE; AIDiff ai = (settings.ai==0)?AI_EASY:(settings.ai==2)?AI_HARD:AI_NORMAL; RendererMode renderer=(settings.renderer==1)?R_PATH:R_CLASSIC;
    RendererMode prevRendererMode = renderer;

    bool settings_changed=false; int menuIndex=0; MainMenuView menu; menu.init(&settings,&settingsMgr,&hsMgr,&highs,&hsPath,&exeDir);
    SettingsPanel settingsPanel; bool settingsOpen=false;
    HighScoresView scoresView; bool scoresOpen=false;
    auto openScores=[&](){ scoresView.begin(&highs); scoresOpen=true; st.ui_mode=2; };
    auto openSettings=[&](){ if(renderer!=R_PATH) return; settingsPanel.begin(hwnd,inst,&settings,&settingsMgr,&exeDir); settingsOpen=true; st.ui_mode=2; };

    // Renderers / HUD / State machine loop
    ClassicRenderer classic; PTRendererAdapter ptAdapter; HudOverlay hud;    
    st.ui_mode = 1; // start in menu
    if(renderer==R_PATH) ptAdapter.resize(st.width, st.height); else classic.onResize(st.width, st.height);
    auto last = std::chrono::steady_clock::now(); const double target=1.0/60.0; static int lastW=-1,lastH=-1;
    while (st.running) {
        if (st.inputRouter) st.inputRouter->new_frame();
        MSG msg; while (PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now-last).count();
        if (dt < target) { std::this_thread::sleep_for(std::chrono::duration<double>(target-dt)); continue; }
        last = now;

        int winW = st.width, winH = st.height;
        int dpi  = query_dpi(hwnd, st.dpi);
        double ui = (double)dpi / 96.0;

        // Detect renderer mode change (e.g., user toggled in menu before starting game)
        if(prevRendererMode != renderer){
            if(renderer==R_PATH){
                ptAdapter.resize(winW,winH); // allocate internal buffers & reset history
            }
            prevRendererMode = renderer;
        }

        if (winW != lastW || winH != lastH) { if(renderer==R_PATH) ptAdapter.resize(winW,winH); else classic.onResize(winW,winH); lastW=winW; lastH=winH; }

        // Update mouse positional caches
        if(st.inputRouter){ const auto &is=st.inputRouter->get(); st.mouse_x=is.mx; st.mouse_y=is.my; if(is.wheel) st.mouse_wheel_delta+=is.wheel; }

        // State machine: 0 gameplay, 1 menu, 2 modal
            if(st.ui_mode == 1){ // MENU
            auto r=menu.updateAndRender(st.memDC,winW,winH,dpi, st.inputRouter?st.inputRouter->get():InputState{}, *(int*)&ctrl,*(int*)&ai,*(int*)&renderer, menuIndex, st.menu_click_index, st.suppressMenuClickDown);
            if(r.settingsChanged){ settings_changed=true; }
            if(r.action){
                switch(*r.action){
                    case MenuAction::Play: {
                        // Transition to gameplay: clear backbuffer and reset PT history so menu isn't blended over
                        st.ui_mode=0; 
                        HBRUSH black=(HBRUSH)GetStockObject(BLACK_BRUSH); RECT clr{0,0,winW,winH}; FillRect(st.memDC,&clr,black);
                        if(renderer==R_PATH) { ptAdapter.resize(winW,winH); } // triggers history reset inside resize
                        session.core().reset();
                        break; }
                    case MenuAction::Settings: openSettings(); break;
                    case MenuAction::Scores: openScores(); break;
                    case MenuAction::Quit: st.running=false; break;
                    case MenuAction::Back: break;
                }
            }
            if(settings_changed){ settingsMgr.save(exeDir+L"settings.json",settings); settings_changed=false; }
        } else if(st.ui_mode == 2){ // MODAL (settings or scores)
            if(settingsOpen){
                auto act = settingsPanel.frame(st.memDC, winW, winH, dpi, st.inputRouter?st.inputRouter->get():InputState{}, st.mouse_x, st.mouse_y, st.mouse_pressed, st.mouse_wheel_delta, st.last_click_x, st.last_click_y);
                if(act==SettingsPanel::Action::Commit){ if(settingsPanel.anyChangesSinceOpen()) settings_changed=true; settingsOpen=false; st.ui_mode=1; }
                else if(act==SettingsPanel::Action::Cancel){ settingsOpen=false; st.ui_mode=1; }
            } else if(scoresOpen){
                const InputState &is = st.inputRouter?st.inputRouter->get():InputState{};
                int deleted=-1; bool delReq = is.just_pressed(VK_DELETE) || (is.click && (GetKeyState(VK_CONTROL)&0x8000));
                scoresView.frame(st.memDC, winW, winH, dpi, is.mx, is.my, is.click, delReq, &deleted);
                if(deleted>=0 && deleted < (int)highs.size()){ highs.erase(highs.begin()+deleted); hsMgr.save(hsPath, highs); }
                if(is.just_pressed(VK_ESCAPE) || is.just_pressed(VK_RETURN)) { scoresOpen=false; st.ui_mode=1; hsMgr.save(hsPath, highs); }
            } else {
                st.ui_mode=1; // fallback
            }
        } else { // GAMEPLAY
            // allow returning to menu with Q
            if(st.inputRouter){ const auto &is=st.inputRouter->get(); if(is.just_pressed('Q')){ st.ui_mode=1; continue; } }
        }

        GameState &gs = session.core().state();
        bool renderGameplay = (st.ui_mode==0);
        if (ctrl == CTRL_KEYBOARD) {
            if (st.inputRouter) {
                const auto &is = st.inputRouter->get();
                if (renderGameplay && is.is_pressed('W')) session.core().move_left_by(-120.0*dt);
                if (renderGameplay && is.is_pressed('S')) session.core().move_left_by(120.0*dt);
            }
        } else {
            double my = (double)st.mouse_y / winH * gs.gh;
            if(renderGameplay) session.core().set_left_y(my - gs.paddle_h/2.0);
        }
        if (st.inputRouter) {
            const auto &is = st.inputRouter->get();
            if (renderGameplay && is.is_pressed(VK_UP)) session.core().move_right_by(-120.0*dt);
            if (renderGameplay && is.is_pressed(VK_DOWN)) session.core().move_right_by(120.0*dt);
        }
        if(renderGameplay){
            session.core().set_ai_speed(ai==AI_EASY?0.6:(ai==AI_NORMAL?1.0:1.6));
            session.update(dt);
        }
        // Rendering
        if(renderGameplay){
            if (renderer == R_PATH) {
                ptAdapter.render(gs, settings, UIState{}, st.memDC);
            } else {
                classic.render(gs, st.memDC, winW, winH, dpi);
            }
            int highScore = highs.empty()?0:highs.front().score;
            hud.draw(gs, renderer==R_PATH?ptAdapter.stats():nullptr, st.memDC, winW, winH, dpi, highScore);
        }
        // Menu or modal already drew into st.memDC; no HUD overlay in those modes.

        HDC hdc=GetDC(hwnd); BitBlt(hdc,0,0,winW,winH,st.memDC,0,0,SRCCOPY); ReleaseDC(hwnd,hdc);
    }

    if(st.memDC && st.uiOldFont) SelectObject(st.memDC, st.uiOldFont); if(st.uiFont) DeleteObject(st.uiFont); delete st.inputRouter; delete st.backBuf; return 0; }

