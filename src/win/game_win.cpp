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
#include <filesystem>
#include <sstream>
#include <fstream>

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
#include "ui/game_mode_settings_view.h"

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

struct RecordingState {
    bool active = false;                // recording in progress
    std::wstring dir;                   // output directory
    int frameIndex = 0;                 // current frame number
    double simTime = 0.0;               // accumulated simulated time
    double fixedStep = 1.0/60.0;        // fixed timestep for recording
    int fps = 60;                       // target recording fps (derived from settings.recording_fps)
    int duration = 0;                   // recording duration in seconds (0=unlimited)
    std::chrono::steady_clock::time_point startTime; // real time start for FPS calculation
    int framesAtLastCheck = 0;          // frames at last FPS check
    double realFps = 0.0;               // actual frames per second being recorded
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
    GameModeSettingsView gameModeView; bool gameModeOpen=false;
    HighScoresView scoresView; bool scoresOpen=false;
    auto openScores=[&](){ scoresView.begin(&highs); scoresOpen=true; st.ui_mode=2; };
    auto openSettings=[&](){ if(renderer!=R_PATH) return; settingsPanel.begin(hwnd,inst,&settings,&settingsMgr,&exeDir); settingsOpen=true; st.ui_mode=2; };
    auto openGameMode=[&](){ gameModeView.begin(&settings.mode_config); gameModeOpen=true; st.ui_mode=2; };

    // Renderers / HUD / State machine loop
    ClassicRenderer classic; PTRendererAdapter ptAdapter; HudOverlay hud;    
    RecordingState rec; // initialized inactive
    st.ui_mode = 1; // start in menu
    if(renderer==R_PATH) ptAdapter.resize(st.width, st.height); else classic.onResize(st.width, st.height);
    auto last = std::chrono::steady_clock::now(); const double target=1.0/60.0; static int lastW=-1,lastH=-1;
    while (st.running) {
        if (st.inputRouter) st.inputRouter->new_frame();
        MSG msg; while (PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now-last).count();
        if (!rec.active) {
            if (dt < target) { std::this_thread::sleep_for(std::chrono::duration<double>(target-dt)); continue; }
            last = now;
        } else {
            // In recording (render) mode we ignore real time and drive fixed simulation steps as fast as possible.
            dt = rec.fixedStep; // force fixed step
            last = now; // keep message pump happy
        }

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
                        // Apply physics mode toggle (0 arcade, 1 physical)
                        session.core().set_physical_mode(settings.physics_mode==1);
                        // Apply speed mode toggle (0 normal, 1 speed mode)
                        session.core().set_speed_mode(settings.speed_mode==1);
                        // Reset clears state, then apply game mode configuration
                        session.core().reset();
                        session.core().apply_mode_config(
                            settings.mode_config.multiball,
                            settings.mode_config.obstacles,
                            settings.mode_config.obstacles_moving,
                            settings.mode_config.blackholes,
                            settings.mode_config.blackholes_moving,
                            settings.mode_config.blackhole_count,
                            settings.mode_config.multiball_count,
                            settings.mode_config.three_enemies,
                            settings.mode_config.obstacles_gravity,
                            settings.mode_config.blackholes_destroy_balls
                        );
                        // Transition to gameplay: clear backbuffer and reset PT history so menu isn't blended over
                        st.ui_mode=0; 
                        HBRUSH black=(HBRUSH)GetStockObject(BLACK_BRUSH); RECT clr{0,0,winW,winH}; FillRect(st.memDC,&clr,black);
                        if(renderer==R_PATH) { ptAdapter.resize(winW,winH); } // triggers history reset inside resize
                        // If recording mode toggle is on, initialize recording session
                        if(settings.recording_mode && !rec.active){
                            // Create output directory with timestamp
                            SYSTEMTIME stime; GetLocalTime(&stime);
                            wchar_t buf[128]; swprintf(buf,128,L"recording_%04d%02d%02d_%02d%02d%02d/", stime.wYear, stime.wMonth, stime.wDay, stime.wHour, stime.wMinute, stime.wSecond);
                            rec.dir = exeDir + buf;
                            std::error_code fec; std::filesystem::create_directories(rec.dir, fec);
                            rec.active = true; rec.frameIndex = 0; rec.simTime = 0.0;
                            rec.startTime = std::chrono::steady_clock::now();
                            rec.framesAtLastCheck = 0;
                            rec.realFps = 0.0;
                            // Apply user-selected recording FPS (clamped by persistence layer 15..60)
                            if(settings.recording_fps < 15) settings.recording_fps = 15; else if(settings.recording_fps > 60) settings.recording_fps = 60;
                            rec.fps = settings.recording_fps;
                            rec.duration = settings.recording_duration;
                            int clampFps = settings.recording_fps;
                            if(clampFps < 15) clampFps = 15; else if(clampFps > 60) clampFps = 60;
                            rec.fixedStep = 1.0 / (double)clampFps;
                        }
                        break; }
                    case MenuAction::Settings: openSettings(); break;
                    case MenuAction::Scores: openScores(); break;
                    case MenuAction::GameMode: openGameMode(); break;
                    case MenuAction::Quit: st.running=false; break;
                    case MenuAction::Back: break;
                }
            }
            if(settings_changed){ settingsMgr.save(exeDir+L"settings.json",settings); settings_changed=false; }
        } else if(st.ui_mode == 2){ // MODAL (settings or scores or game mode)
            if(settingsOpen){
                auto act = settingsPanel.frame(st.memDC, winW, winH, dpi, st.inputRouter?st.inputRouter->get():InputState{}, st.mouse_x, st.mouse_y, st.mouse_pressed, st.mouse_wheel_delta, st.last_click_x, st.last_click_y);
                if(act==SettingsPanel::Action::Commit){ if(settingsPanel.anyChangesSinceOpen()) settings_changed=true; settingsOpen=false; st.ui_mode=1; }
                else if(act==SettingsPanel::Action::Cancel){ settingsOpen=false; st.ui_mode=1; }
            } else if(gameModeOpen){
                auto act = gameModeView.frame(st.memDC, winW, winH, dpi, st.inputRouter?st.inputRouter->get():InputState{}, st.mouse_x, st.mouse_y, st.mouse_pressed, st.mouse_wheel_delta, st.last_click_x, st.last_click_y);
                if(act==GameModeSettingsView::Action::Commit){ if(gameModeView.anyChangesSinceOpen()) settings_changed=true; gameModeOpen=false; st.ui_mode=1; }
                else if(act==GameModeSettingsView::Action::Cancel){ gameModeOpen=false; st.ui_mode=1; }
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
        // Player mode logic:
        // player_mode: 0 = 1P vs AI (left human, right AI)
        //              1 = 2 Players (left + right human)
        //              2 = AI vs AI  (both AI, ignore input)
        int pmode = settings.player_mode;
        if(pmode < 0 || pmode > 2) pmode = 0;
        bool leftHuman  = (pmode != 2);
        bool rightHuman = (pmode == 1); // only 2P mode makes right human

        // Apply human control for left paddle
        if (renderGameplay && leftHuman) {
            if (ctrl == CTRL_KEYBOARD) {
                if (st.inputRouter) {
                    const auto &is = st.inputRouter->get();
                    if (is.is_pressed('W')) session.core().move_left_by(-120.0*dt);
                    if (is.is_pressed('S')) session.core().move_left_by(120.0*dt);
                }
            } else { // mouse
                double my = (double)st.mouse_y / winH * gs.gh;
                session.core().set_left_y(my - gs.paddle_h/2.0);
            }
        }

        // Apply human control for right paddle (arrow keys) only when rightHuman
        if (renderGameplay && rightHuman && st.inputRouter) {
            const auto &is = st.inputRouter->get();
            if (is.is_pressed(VK_UP)) session.core().move_right_by(-120.0*dt);
            if (is.is_pressed(VK_DOWN)) session.core().move_right_by(120.0*dt);
        }

        // Configure AI enable flags inside core each frame so live menu changes apply on return to gameplay
        // Left AI active only in AI vs AI mode; Right AI active in modes 0 (1P vs AI) and 2 (AI vs AI)
    session.core().enable_left_ai(pmode == 2);               // left AI only in AI vs AI
    session.core().enable_right_ai((pmode == 0) || (pmode == 2)); // right AI in 1P vs AI and AI vs AI
        if(renderGameplay){
            session.core().set_ai_speed(ai==AI_EASY?0.6:(ai==AI_NORMAL?1.0:1.6));
            if(rec.active){
                // Fixed-step simulation already enforced via dt override
                session.update(dt);
                rec.simTime += dt;
            } else {
                session.update(dt);
            }
        }
        // Rendering
        if(renderGameplay){
            if (renderer == R_PATH) {
                ptAdapter.render(gs, settings, UIState{}, st.memDC);
            } else {
                classic.render(gs, st.memDC, winW, winH, dpi);
            }
            int highScore = highs.empty()?0:highs.front().score;
            bool showHud = settings.hud_show_play!=0; // default for gameplay
            if(rec.active && settings.hud_show_record==0) showHud = false; // hide entirely while recording if user chose so
            if(showHud) {
                hud.draw(gs, renderer==R_PATH?ptAdapter.stats():nullptr, st.memDC, winW, winH, dpi, highScore);
            }
            if(rec.active){
                // Calculate actual recording FPS every second
                auto recNow = std::chrono::steady_clock::now();
                double recElapsed = std::chrono::duration<double>(recNow - rec.startTime).count();
                if(recElapsed >= 1.0 && rec.frameIndex > rec.framesAtLastCheck){
                    rec.realFps = (rec.frameIndex - rec.framesAtLastCheck) / recElapsed;
                    rec.startTime = recNow;
                    rec.framesAtLastCheck = rec.frameIndex;
                }
                
                // Recording info panel to the right of standard HUD (HUD width ~280px)
                int boxW = 260; int boxX = 300; int boxY = 0; int boxH = 150;
                HBRUSH rb = CreateSolidBrush(RGB(8,8,12)); RECT rr{boxX,boxY,boxX+boxW,boxY+boxH}; FillRect(st.memDC,&rr,rb); DeleteObject(rb);
                SetBkMode(st.memDC, TRANSPARENT); SetTextColor(st.memDC, RGB(255,80,80));
                wchar_t recTxt[128]; swprintf(recTxt,128,L"RECORDING %dfps", rec.fps);
                RECT r1{boxX+8,boxY+6,boxX+boxW-8,boxY+26}; DrawTextW(st.memDC, recTxt, -1, &r1, DT_LEFT|DT_TOP|DT_SINGLELINE);
                int fpsDiv = rec.fps < 1 ? 1 : rec.fps;
                double simSeconds = rec.frameIndex / (double)fpsDiv;
                wchar_t meta[128]; swprintf(meta,128,L"Frames: %d", rec.frameIndex);
                RECT r2{boxX+8,boxY+28,boxX+boxW-8,boxY+48}; DrawTextW(st.memDC, meta, -1, &r2, DT_LEFT|DT_TOP|DT_SINGLELINE);
                wchar_t meta2[128]; swprintf(meta2,128,L"Sim Time: %.1fs", simSeconds);
                RECT r3{boxX+8,boxY+48,boxX+boxW-8,boxY+68}; DrawTextW(st.memDC, meta2, -1, &r3, DT_LEFT|DT_TOP|DT_SINGLELINE);
                
                // Show actual recording FPS and time estimates
                if(rec.realFps > 0.1){
                    wchar_t fpsTxt[128]; swprintf(fpsTxt,128,L"Actual: %.1f fps", rec.realFps);
                    RECT r4{boxX+8,boxY+68,boxX+boxW-8,boxY+88}; DrawTextW(st.memDC, fpsTxt, -1, &r4, DT_LEFT|DT_TOP|DT_SINGLELINE);
                    
                    // Duration-based progress
                    if(rec.duration > 0){
                        int targetFrames = rec.duration * rec.fps;
                        int remaining = targetFrames - rec.frameIndex;
                        if(remaining < 0) remaining = 0;
                        double estSeconds = remaining / rec.realFps;
                        int mins = (int)(estSeconds / 60.0);
                        int secs = (int)(estSeconds) % 60;
                        wchar_t estTxt[128]; swprintf(estTxt,128,L"Est. Remaining: %dm %ds", mins, secs);
                        RECT r5{boxX+8,boxY+88,boxX+boxW-8,boxY+108}; DrawTextW(st.memDC, estTxt, -1, &r5, DT_LEFT|DT_TOP|DT_SINGLELINE);
                        
                        int pct = targetFrames > 0 ? (rec.frameIndex * 100) / targetFrames : 0;
                        if(pct > 100) pct = 100;
                        wchar_t pctTxt[128]; swprintf(pctTxt,128,L"Progress: %d%%", pct);
                        RECT r6{boxX+8,boxY+108,boxX+boxW-8,boxY+128}; DrawTextW(st.memDC, pctTxt, -1, &r6, DT_LEFT|DT_TOP|DT_SINGLELINE);
                    } else {
                        wchar_t unlimTxt[128]; swprintf(unlimTxt,128,L"Duration: Unlimited");
                        RECT r5{boxX+8,boxY+88,boxX+boxW-8,boxY+108}; DrawTextW(st.memDC, unlimTxt, -1, &r5, DT_LEFT|DT_TOP|DT_SINGLELINE);
                    }
                }
            }
        }
        // Menu or modal already drew into st.memDC; no HUD overlay in those modes.

        HDC hdc=GetDC(hwnd); BitBlt(hdc,0,0,winW,winH,st.memDC,0,0,SRCCOPY); ReleaseDC(hwnd,hdc);

        // Frame capture after present (use back buffer DC content)
        if(rec.active && renderGameplay){
            // Capture BMP (BGRA 32-bit) and pad to even dimensions (H.264 yuv420p requires even w/h)
            BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = winW; bmi.bmiHeader.biHeight = winH; // bottom-up
            bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
            std::vector<uint8_t> raw((size_t)winW * (size_t)winH * 4);
            if(GetDIBits(st.memDC, st.backBuf->getBitmap(), 0, (UINT)winH, raw.data(), &bmi, DIB_RGB_COLORS)){
                int recW = (winW & 1)? (winW+1): winW;
                int recH = (winH & 1)? (winH+1): winH;
                std::vector<uint8_t> pixels((size_t)recW * (size_t)recH * 4, 0);
                // Copy existing rows; bottom-up order preserved. Extra column/row left black.
                for(int y=0; y<winH; ++y){
                    const uint8_t* srcRow = &raw[(size_t)y * winW * 4];
                    uint8_t* dstRow = &pixels[(size_t)y * recW * 4];
                    std::memcpy(dstRow, srcRow, (size_t)winW * 4);
                }
                // Construct file path
                wchar_t fname[256]; swprintf(fname,256,L"frame_%06d.bmp", rec.frameIndex);
                std::wstring fpath = rec.dir + fname;
                uint32_t rowSize = recW * 4; // 4-byte aligned already
                uint32_t pixelDataSize = rowSize * recH;
                uint32_t fileSize = 14 + 40 + pixelDataSize;
                std::ofstream ofs(fpath, std::ios::binary);
                if(ofs){
                    uint8_t fh[14]; std::memset(fh,0,14); fh[0]='B'; fh[1]='M'; *reinterpret_cast<uint32_t*>(fh+2)=fileSize; *reinterpret_cast<uint32_t*>(fh+10)=14+40; ofs.write((char*)fh,14);
                    BITMAPINFOHEADER bih{}; bih.biSize=40; bih.biWidth=recW; bih.biHeight=recH; bih.biPlanes=1; bih.biBitCount=32; bih.biCompression=BI_RGB; bih.biSizeImage=pixelDataSize; ofs.write((char*)&bih,40);
                    ofs.write((char*)pixels.data(), pixelDataSize);
                }
                rec.frameIndex++;
            }
        }

        // Stop recording when duration reached, user leaves gameplay, or game ends
        bool durationReached = false;
        if(rec.active && rec.duration > 0){
            int targetFrames = rec.duration * rec.fps;
            if(rec.frameIndex >= targetFrames) durationReached = true;
        }
        if(rec.active && (st.ui_mode != 0 || durationReached)){
            // Write summary file
            std::wstring summary = rec.dir + L"recording_info.txt";
            std::ofstream s(summary);
            if(s){
                s << "Frames: " << rec.frameIndex << "\n";
                s << "FPS: " << rec.fps << "\n";
                s << "Note: Frames padded to even dimensions for H.264 compatibility.\n";
                s << "Suggested ffmpeg command (PowerShell):\n";
                s << "ffmpeg -framerate " << rec.fps << " -i frame_%06d.bmp -c:v libx264 -pix_fmt yuv420p output.mp4\n";
                s << "If you need HEVC: ffmpeg -framerate " << rec.fps << " -i frame_%06d.bmp -c:v libx265 -pix_fmt yuv420p10le output_hevc.mp4\n";
            }
            rec.active = false;
            // Automatically turn off recording toggle so user must re-enable explicitly
            settings.recording_mode = 0; settings_changed = true;
        }
    }

    if(st.memDC && st.uiOldFont) SelectObject(st.memDC, st.uiOldFont); if(st.uiFont) DeleteObject(st.uiFont); delete st.inputRouter; delete st.backBuf; return 0; }

