/**
 * @file game_win.cpp
 * @brief Windows GUI implementation of PongCpp using Win32 and GDI
 * 
 * This file implements a complete Windows GUI version of Pong using
 * native Win32 APIs and GDI for rendering. Features include:
 * - DPI-aware window management
 * - Real-time mouse and keyboard input
 * - Settings and high score persistence
 * - Context menus for configuration
 * - Smooth GDI-based graphics rendering
 */

#include "../game.h"
#include "game_win.h"

#include <windows.h>
#include <string>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cwchar>
#include "../core/game_core.h"
#include "highscores.h"
#include "settings.h"
#include "soft_renderer.h"

/// Window class name for registration
static const wchar_t CLASS_NAME[] = L"PongWindowClass";

struct WinState {
    int width = 800;
    int height = 600;
    int dpi = 96;
    Game *game = nullptr;
    bool running = true;
    bool request_menu = false; // when true, main loop will re-enter menu instead of quitting
    bool suppressMenuClickDown = false; // suppress WM_LBUTTONDOWN menu_click_index (re-entry menu uses release only)
    // input state
    bool key_down[256] = {};
    int mouse_x = 0;
    int mouse_y = 0;
    int last_click_x = -1;
    int last_click_y = -1;
    bool mouse_pressed = false;
    int mouse_wheel_delta = 0; // accumulated wheel delta (120 units per notch)
    // name entry modal
    bool capture_name = false;
    std::wstring name_buf;
    // UI mode: 0 = gameplay, 1 = menu, 2 = name entry
    int ui_mode = 0;
    // when user clicks a menu option, store index here (-1=no click)
    int menu_click_index = -1;
    // backbuffer handles (stored here for WM_SIZE recreate)
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HBITMAP oldBmp = nullptr;
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    WinState *st = (WinState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (uMsg) {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        if (st) {
            int w = LOWORD(lParam); int h = HIWORD(lParam);
            st->width = w; st->height = h;
            // recreate backbuffer if present
            if (st->memDC && st->memBmp) {
                HDC hdc = GetDC(hwnd);
                SelectObject(st->memDC, st->oldBmp);
                DeleteObject(st->memBmp);
                st->memBmp = CreateCompatibleBitmap(hdc, w, h);
                st->oldBmp = (HBITMAP)SelectObject(st->memDC, st->memBmp);
                ReleaseDC(hwnd, hdc);
            }
        }
        return 0;
    case WM_DPICHANGED:
        if (st) {
            // lParam is a RECT* to suggested new window rect, wParam contains new DPI
            int newDpi = (int)LOWORD(wParam);
            st->dpi = newDpi;
            // apply suggested new size/position
            RECT* prc = (RECT*)lParam;
            if (prc) {
                SetWindowPos(hwnd, NULL, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                st->width = prc->right - prc->left;
                st->height = prc->bottom - prc->top;
                // recreate backbuffer
                if (st->memDC && st->memBmp) {
                    HDC hdc = GetDC(hwnd);
                    SelectObject(st->memDC, st->oldBmp);
                    DeleteObject(st->memBmp);
                    st->memBmp = CreateCompatibleBitmap(hdc, st->width, st->height);
                    st->oldBmp = (HBITMAP)SelectObject(st->memDC, st->memBmp);
                    ReleaseDC(hwnd, hdc);
                }
            }
        }
        return 0;
    case WM_DESTROY:
        if (st) st->running = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (st) {
            st->key_down[wParam & 0xFF] = true;
            if (wParam == 'Q') {
                // Only trigger menu return if currently in gameplay (ui_mode==0)
                if (st->ui_mode == 0) st->request_menu = true;
            }
        }
        return 0;
    case WM_CHAR:
        if (st && st->capture_name) {
            wchar_t ch = (wchar_t)wParam;
            if (ch == 8) { // backspace
                if (!st->name_buf.empty()) st->name_buf.pop_back();
            } else if (ch == L'\r' || ch == L'\n') {
                // Enter finalizes capture; we'll handle it in the main loop
                st->key_down[VK_RETURN] = true;
            } else if (ch >= 32 && st->name_buf.size() < 32) {
                st->name_buf.push_back(ch);
            }
        }
        return 0;
    case WM_KEYUP:
        if (st) st->key_down[wParam & 0xFF] = false;
        return 0;
    case WM_MOUSEMOVE:
        if (st) {
            // GET_Y_LPARAM macro may be missing; extract Y from lParam
            st->mouse_x = (int)(short)LOWORD(lParam);
            st->mouse_y = (int)(short)HIWORD(lParam);
            // ...existing code...
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) {
            SetFocus(hwnd);
            SetForegroundWindow(hwnd);
            // record mouse pressed for visual feedback; actual click will be recorded on LBUTTONUP
            st->mouse_pressed = true;
            // ...existing code...
            // detect which menu option was clicked (do this regardless of ui_mode so clicks don't get lost)
            {
                int mx = (int)(short)LOWORD(lParam);
                int my = (int)(short)HIWORD(lParam);
                int w = st->width; int h = st->height;
                int baseX = w/2 - 170; // adjusted for wider menu
                // compute dpi/ui_scale locally (WindowProc can't access run-time ui_scale)
                int dpi_local = 96;
                HMODULE user32_local = LoadLibraryW(L"user32.dll");
                if (user32_local) {
                    auto pGetDpiForWindow_local = (UINT(WINAPI*)(HWND))GetProcAddress(user32_local, "GetDpiForWindow");
                    if (pGetDpiForWindow_local) dpi_local = pGetDpiForWindow_local(hwnd);
                    FreeLibrary(user32_local);
                }
                double ui_scale_local = (double)dpi_local / 96.0;
                // Updated to 7 items (control, ai, renderer, quality, start, highscores, quit)
                int ys[7] = { (int)(120 * ui_scale_local + 0.5), (int)(170 * ui_scale_local + 0.5), (int)(220 * ui_scale_local + 0.5), (int)(270 * ui_scale_local + 0.5), (int)(330 * ui_scale_local + 0.5), (int)(380 * ui_scale_local + 0.5), (int)(430 * ui_scale_local + 0.5) };
                if (!st->suppressMenuClickDown) {
                    for (int i=0;i<7;i++) {
                        int pad = (int)max(6.0, 10.0 * ui_scale_local);
                        int wbox = (int)max(300.0, 300.0 * ui_scale_local);
                        RECT rb = { baseX - pad, ys[i] - (int)(6*ui_scale_local + 0.5), baseX + wbox, ys[i] + (int)(34*ui_scale_local + 0.5) };
                        if (mx >= rb.left && mx <= rb.right && my >= rb.top && my <= rb.bottom) {
                            st->menu_click_index = i;
                            break;
                        }
                    }
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (st) {
            st->mouse_pressed = false;
            st->last_click_x = (int)(short)LOWORD(lParam);
            st->last_click_y = (int)(short)HIWORD(lParam);
            // ...existing code...
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (st) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam); // +120 up, -120 down
            st->mouse_wheel_delta += delta;
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// very small helper to draw text
static void DrawTextCentered(HDC hdc, const std::wstring &text, int x, int y) {
    // center text around (x,y) with a reasonably sized rect so vertical alignment is consistent
    RECT r = { x - 400, y - 16, x + 400, y + 16 };
    DrawTextW(hdc, text.c_str(), (int)text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

int run_win_pong(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    WinState state;

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Pong (Win32)", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, state.width, state.height, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)&state);

    ShowWindow(hwnd, nCmdShow);
    // make sure the window gets keyboard focus and is foreground
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    UpdateWindow(hwnd);

    // Create a back buffer compatible DC and store in state for WM_SIZE handling
    HDC hdc = GetDC(hwnd);
    state.memDC = CreateCompatibleDC(hdc);
    state.memBmp = CreateCompatibleBitmap(hdc, state.width, state.height);
    state.oldBmp = (HBITMAP)SelectObject(state.memDC, state.memBmp);
    // create a DPI-aware font and select into memDC
    int dpi = 96;
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        auto pGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(user32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
        FreeLibrary(user32);
    }
    int fontSize = max(10, (dpi * 10) / 96);
    HFONT uiFont = CreateFontW(-MulDiv(fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(state.memDC, uiFont);
    // compute UI scale from DPI for consistent sizing on high-DPI displays
    double ui_scale = (double)dpi / 96.0;
    HDC memDC = state.memDC;
    HBITMAP memBmp = state.memBmp;
    HBITMAP oldBmp = state.oldBmp;

    // Simple game instance using local variables. We'll add a small config menu,
    // mouse control option, AI difficulty and high-score persistence.

    // game timing
    auto last = std::chrono::steady_clock::now();
    const double target_dt = 1.0/60.0;

    // create core game
    GameCore core;

    // we will use core.state() for left/right/ball/score

    // config
    enum ControlMode { CTRL_KEYBOARD=0, CTRL_MOUSE=1 } ctrl = CTRL_KEYBOARD;
    enum AIDifficulty { AI_EASY=0, AI_NORMAL=1, AI_HARD=2 } ai = AI_NORMAL;
    enum RendererMode { R_CLASSIC=0, R_PATH=1 } rendererMode = R_CLASSIC;
    int quality = 1; // legacy (unused)

    // scores & high score handling via HighScores
    HighScores hsMgr;
    std::wstring exeDir;
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        std::wstring sp(path);
        size_t pos = sp.find_last_of(L"\\/");
        exeDir = (pos==std::wstring::npos) ? L"" : sp.substr(0,pos+1);
    }
    // settings
    SettingsManager settingsMgr;
    Settings settings = settingsMgr.load(exeDir + L"settings.json");
    // apply loaded settings
    if (settings.control_mode==1) ctrl = CTRL_MOUSE;
    if (settings.ai==0) ai = AI_EASY; else if (settings.ai==2) ai = AI_HARD; else ai = AI_NORMAL;
    if (settings.renderer==1) rendererMode = R_PATH; else rendererMode = R_CLASSIC;
    if (settings.quality>=0 && settings.quality<=2) quality = settings.quality;

    bool settings_changed = false;
    std::wstring hsPath = exeDir + L"highscores.json";
    auto highList = hsMgr.load(hsPath, 10);
    int high_score = (highList.empty()) ? 0 : highList.front().score;

    // --- Restored main menu state & helpers (removed during prior refactor) ---
    bool inMenu = true;                // whether we are currently inside the main menu loop
    int  menuIndex = 0;                // currently selected menu item (0..6)
    auto clamp_menu = [](int &v, int lo, int hi){ if (v < lo) v = lo; if (v > hi) v = hi; };

    // High Scores management modal (view & optional clear)
    auto manageHighScoresModal = [&](){
        state.ui_mode = 2; // modal
        bool running = true;
        while (running && state.running) {
            MSG mm; while (PeekMessage(&mm,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&mm); DispatchMessage(&mm);}            
            int w = state.width, h = state.height; int dpi_now = state.dpi; if (dpi_now==96){ HMODULE u=LoadLibraryW(L"user32.dll"); if(u){ auto p=(UINT(WINAPI*)(HWND))GetProcAddress(u,"GetDpiForWindow"); if(p) dpi_now=p(hwnd); FreeLibrary(u);} }
            double ui_scale_local = (double)dpi_now / 96.0; HDC mem = state.memDC; HDC hdcLocal = GetDC(hwnd);
            RECT bg={0,0,w,h}; HBRUSH b=CreateSolidBrush(RGB(18,18,28)); FillRect(mem,&bg,b); DeleteObject(b);
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, RGB(240,240,250));
            DrawTextCentered(mem, L"High Scores", w/2, (int)round(40*ui_scale_local));
            if (highList.empty()) {
                DrawTextCentered(mem, L"(No scores yet)", w/2, (int)round(90*ui_scale_local));
            } else {
                for (size_t i=0;i<highList.size() && i<10;i++) {
                    std::wstring line = std::to_wstring(i+1) + L"  " + highList[i].name + L"  " + std::to_wstring(highList[i].score);
                    DrawTextCentered(mem, line, w/2, (int)round(90*ui_scale_local + i*32*ui_scale_local));
                }
            }
            SetTextColor(mem, RGB(180,180,200));
            DrawTextCentered(mem, L"Enter/Esc = Close   C = Clear All", w/2, h - (int)round(50*ui_scale_local));
            BitBlt(hdcLocal,0,0,w,h,mem,0,0,SRCCOPY); ReleaseDC(hwnd, hdcLocal);
            // input
            if (state.key_down[VK_ESCAPE] || state.key_down[VK_RETURN]) { state.key_down[VK_ESCAPE]=state.key_down[VK_RETURN]=false; running=false; }
            if (state.key_down['C']) { state.key_down['C']=false; highList.clear(); hsMgr.save(hsPath, highList); running=false; }
            if (state.last_click_x!=-1) { // any click closes
                state.last_click_x = state.last_click_y = -1; running=false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        state.ui_mode = 1; // back to menu
    };

    // Reusable path tracer settings modal (sliders + checkboxes)
    auto runPathTracerSettingsModal = [&]() {
        if (rendererMode!=R_PATH) return;
        // Elements order (for navigation): 7 base sliders, force rays checkbox, camera checkbox, RR enable checkbox,
        // RR start bounce slider, RR min prob slider, Reset Defaults button.
        state.ui_mode = 2; bool editing=true; int sel=0; 
        // Keep a baseline copy for Cancel (Esc). If user hits Save we advance baseline.
        Settings originalSettings = settings; 
        bool cancelled = false; // set when Esc pressed
        int saveFeedbackTicks = 0; // frames to show "Saved" feedback on button
        const int baseSliderCount = 7;
        const int totalItems = baseSliderCount + 1 /*force*/ + 1 /*camera*/ + 1 /*RR enable*/ + 1 /*RR start*/ + 1 /*RR min*/ + 1 /*Reset*/;
        auto clampSel=[&](int &v){ if(v<0)v=0; if(v>totalItems-1)v=totalItems-1; };
        int scrollOffset = 0; // vertical scroll in pixels
        int maxScroll = 0;    // computed each frame
        const int scrollStep = 40; // pixels per wheel notch / key scroll
        while (editing && state.running) {
            MSG m; while (PeekMessage(&m,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessage(&m);}                        
            int w=state.width, h=state.height; int dpi_now=state.dpi; if (dpi_now==96){ HMODULE u=LoadLibraryW(L"user32.dll"); if(u){ auto p=(UINT(WINAPI*)(HWND))GetProcAddress(u,"GetDpiForWindow"); if(p) dpi_now=p(hwnd); FreeLibrary(u);} }
            double ui_scale = (double)dpi_now / 96.0; HDC memDC = state.memDC; HDC hdcLocal = GetDC(hwnd);
            RECT bg={0,0,w,h}; HBRUSH b=CreateSolidBrush(RGB(15,15,25)); FillRect(memDC,&bg,b); DeleteObject(b);
            SetBkMode(memDC,TRANSPARENT); SetTextColor(memDC, RGB(235,235,245));
            DrawTextCentered(memDC,L"Path Tracer Settings", w/2, (int)round(40*ui_scale));
            struct SliderInfo { const wchar_t* label; int *val; int minv; int maxv; int step; };                            
            SliderInfo sliders[] = {
                {L"Rays / Frame", &settings.pt_rays_per_frame, 1, 1000, 1},
                {L"Max Bounces", &settings.pt_max_bounces, 1, 8, 1},
                {L"Internal Scale %", &settings.pt_internal_scale, 1, 100, 1},
                {L"Metal Roughness %", &settings.pt_roughness, 0, 100, 1},
                {L"Emissive %", &settings.pt_emissive, 1, 500, 1},
                {L"Accum Alpha %", &settings.pt_accum_alpha, 0, 100, 1},
                {L"Denoise %", &settings.pt_denoise_strength, 0, 100, 1}
            };
            int sliderCount = (int)(sizeof(sliders)/sizeof(sliders[0]));
            int centerX = w/2; int baseY = (int)round(110*ui_scale) - scrollOffset; // apply scroll
            int rowH = (int)round(46*ui_scale);
            int barW = (int)round(420*ui_scale);
            int barH = max(8,(int)round(10*ui_scale));
            // Extended items indices mapping
            int idxForce = baseSliderCount;
            int idxCamera = baseSliderCount + 1;
            int idxRREnable = baseSliderCount + 2;
            int idxRRStart = baseSliderCount + 3;
            int idxRRMin = baseSliderCount + 4;
            int idxReset = baseSliderCount + 5;

            // compute content height for scroll bounds (up to last RR slider)
            int cyRRMinTemp = baseY + (baseSliderCount+4)*rowH; 
            int contentBottom = cyRRMinTemp + (int)round(80*ui_scale);
            int topVisible = (int)round(80*ui_scale);
            int bottomPanelH = (int)round(130*ui_scale); // static panel height (buttons + legend)
            int usableHeight = h - bottomPanelH;
            maxScroll = (std::max)(0, contentBottom - usableHeight + topVisible);
            int panelTop = h - bottomPanelH + (int)round(6*ui_scale);

            // Tooltip detection: determine which element mouse is over (include new items)
            int hoverItem = -1; // 0..sliderCount-1 sliders, others mapped above, reset=12, save=13
            int mx = state.mouse_x; int my = state.mouse_y;
            bool pointerInPanel = my >= panelTop;
            if (!pointerInPanel) {
                // sliders regions (bar height area) - ignore those fully hidden behind panel
                for (int i=0;i<sliderCount;i++) {
                    int y = baseY + i*rowH; int bx = centerX - barW/2; int by = y + (int)round(14*ui_scale);
                    int byBottom = by + barH;
                    if (by >= panelTop || byBottom >= panelTop) {
                        // Skip: hidden or clipped by panel
                    } else {
                        RECT bar={bx,by,bx+barW,by+barH};
                        if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) { hoverItem = i; break; }
                    }
                }
                if (hoverItem==-1) {
                    int cyForce = baseY + baseSliderCount*rowH; int cyCam = baseY + (baseSliderCount+1)*rowH; int cyRRE = baseY + (baseSliderCount+2)*rowH; int cyRRStart = baseY + (baseSliderCount+3)*rowH; int cyRRMin = baseY + (baseSliderCount+4)*rowH; int cyReset = baseY + (baseSliderCount+5)*rowH; 
                    auto midRectHit=[&](int cy){ return cy < panelTop && mx >= centerX - (int)(220*ui_scale) && mx <= centerX + (int)(220*ui_scale) && my >= cy - (int)(16*ui_scale) && my <= cy + (int)(16*ui_scale); };
                    if (midRectHit(cyForce)) hoverItem=idxForce;
                    else if (midRectHit(cyCam)) hoverItem=idxCamera;
                    else if (midRectHit(cyRRE)) hoverItem=idxRREnable;
                    else {
                        // RR start slider bar
                        int byS = cyRRStart + (int)round(14*ui_scale);
                        if (cyRRStart < panelTop && my>=byS && my<=byS+barH && mx>=centerX-barW/2 && mx<=centerX+barW/2) hoverItem=idxRRStart; 
                        int byM = cyRRMin + (int)round(14*ui_scale);
                        if (hoverItem==-1 && cyRRMin < panelTop && my>=byM && my<=byM+barH && mx>=centerX-barW/2 && mx<=centerX+barW/2) hoverItem=idxRRMin;
                        int cyResetCheck = cyReset; // old reset (still allow tooltip if visible)
                        if (hoverItem==-1 && cyResetCheck < panelTop && mx>=centerX - (int)(160*ui_scale) && mx<=centerX + (int)(160*ui_scale) && my>=cyResetCheck - (int)(18*ui_scale) && my<=cyResetCheck + (int)(18*ui_scale)) hoverItem=idxReset;
                    }
                }
            }
            // (panel buttons tooltip detection later after panel drawn sets hoverItem to 12/13)
            for (int i=0;i<sliderCount;i++) {
                int y = baseY + i*rowH;
                bool hot = (sel==i);
                SetTextColor(memDC, hot?RGB(255,240,160):RGB(200,200,210));
                // Compose label with current value (percent-aware for all except rays/bounces)
                int v = *sliders[i].val;
                std::wstring dynLabel = std::wstring(sliders[i].label) + L": " + std::to_wstring(v);
                DrawTextCentered(memDC, dynLabel, centerX, y);
                int bx = centerX - barW/2; int by = y + (int)round(14*ui_scale);
                RECT bar={bx,by,bx+barW,by+barH};
                HBRUSH bb=CreateSolidBrush(RGB(50,60,80)); FillRect(memDC,&bar,bb); DeleteObject(bb);
                double t = double(*sliders[i].val - sliders[i].minv)/(double)(sliders[i].maxv - sliders[i].minv);
                RECT fill={bx,by,bx+(int)(barW*t),by+barH}; HBRUSH bf=CreateSolidBrush(hot?RGB(120,180,255):RGB(90,120,180)); FillRect(memDC,&fill,bf); DeleteObject(bf);
            }
            // Checkboxes & extra sliders
            int cyForce = baseY + baseSliderCount*rowH; bool forceHot = (sel==idxForce);
            std::wstring forceTxt = std::wstring(L"Force 1 ray / pixel: ") + (settings.pt_force_full_pixel_rays?L"ON":L"OFF");
            SetTextColor(memDC, forceHot?RGB(255,240,160):RGB(200,200,210)); DrawTextCentered(memDC, forceTxt, centerX, cyForce);
            int cyCam = baseY + (baseSliderCount+1)*rowH; bool camHot = (sel==idxCamera);
            std::wstring camTxt = std::wstring(L"Camera: ") + (settings.pt_use_ortho?L"Orthographic":L"Perspective");
            SetTextColor(memDC, camHot?RGB(255,240,160):RGB(200,200,210)); DrawTextCentered(memDC, camTxt, centerX, cyCam);
            int cyRRE = baseY + (baseSliderCount+2)*rowH; bool rreHot = (sel==idxRREnable);
            std::wstring rreTxt = std::wstring(L"Russian Roulette: ") + (settings.pt_rr_enable?L"ON":L"OFF");
            SetTextColor(memDC, rreHot?RGB(255,240,160):RGB(200,200,210)); DrawTextCentered(memDC, rreTxt, centerX, cyRRE);
            // RR Start bounce slider label+bar
            int cyRRStart = baseY + (baseSliderCount+3)*rowH; bool rrStartHot = (sel==idxRRStart);
            SetTextColor(memDC, rrStartHot?RGB(255,240,160):RGB(200,200,210));
            std::wstring rrStartLabel = L"RR Start Bounce: " + std::to_wstring(settings.pt_rr_start_bounce);
            DrawTextCentered(memDC, rrStartLabel, centerX, cyRRStart);
            {
                int bx = centerX - barW/2; int by = cyRRStart + (int)round(14*ui_scale);
                RECT bar={bx,by,bx+barW,by+barH}; HBRUSH bb2=CreateSolidBrush(RGB(50,60,80)); FillRect(memDC,&bar,bb2); DeleteObject(bb2);
                double t = double(settings.pt_rr_start_bounce - 1) / double(16 - 1);
                RECT fill={bx,by,bx+(int)(barW*t),by+barH}; HBRUSH bf2=CreateSolidBrush(rrStartHot?RGB(120,180,255):RGB(90,120,180)); FillRect(memDC,&fill,bf2); DeleteObject(bf2);
            }
            // RR Min probability slider
            int cyRRMin = baseY + (baseSliderCount+4)*rowH; bool rrMinHot = (sel==idxRRMin);
            SetTextColor(memDC, rrMinHot?RGB(255,240,160):RGB(200,200,210));
            std::wstring rrMinLabel = L"RR Min Prob %: " + std::to_wstring(settings.pt_rr_min_prob_pct);
            DrawTextCentered(memDC, rrMinLabel, centerX, cyRRMin);
            {
                int bx = centerX - barW/2; int by = cyRRMin + (int)round(14*ui_scale);
                RECT bar={bx,by,bx+barW,by+barH}; HBRUSH bb2=CreateSolidBrush(RGB(50,60,80)); FillRect(memDC,&bar,bb2); DeleteObject(bb2);
                double t = double(settings.pt_rr_min_prob_pct - 1) / double(90 - 1); if (t<0) t=0; if (t>1) t=1;
                RECT fill={bx,by,bx+(int)(barW*t),by+barH}; HBRUSH bf2=CreateSolidBrush(rrMinHot?RGB(120,180,255):RGB(90,120,180)); FillRect(memDC,&fill,bf2); DeleteObject(bf2);
            }
            // Avoid Windows macro collision with 'max' already handled above when computing maxScroll

            // Mouse wheel scrolling (accumulated deltas)
            if (state.mouse_wheel_delta != 0) {
                int steps = state.mouse_wheel_delta / 120; // Windows wheel notch
                if (steps != 0) {
                    scrollOffset -= steps * scrollStep; // wheel up (positive delta) scrolls up (reduce offset)
                    if (scrollOffset < 0) scrollOffset = 0;
                    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
                    state.mouse_wheel_delta -= steps * 120; // consume whole notches only
                }
            }

            // Draw scrollbar if scrollable
            if (maxScroll > 0) {
                int trackMargin = (int)round(8*ui_scale);
                int trackW = (int)round(12*ui_scale);
                int trackX = w - trackMargin - trackW;
                int trackY0 = (int)round(80*ui_scale);
                int trackY1 = h - bottomPanelH + (int)round(10*ui_scale); // end above static bottom panel
                int trackH = trackY1 - trackY0;
                RECT trackR = { trackX, trackY0, trackX + trackW, trackY1 };
                HBRUSH trBrush = CreateSolidBrush(RGB(30,30,45)); FillRect(memDC,&trackR,trBrush); DeleteObject(trBrush);
                FrameRect(memDC,&trackR,(HBRUSH)GetStockObject(GRAY_BRUSH));
                double visibleFrac = (double)h / (double)(contentBottom + (int)round(40*ui_scale));
                if (visibleFrac < 0.05) visibleFrac = 0.05; if (visibleFrac > 1.0) visibleFrac = 1.0;
                int thumbH = (int)(std::max)( (double)(trackH * visibleFrac), 20.0 );
                double scrollFrac = (maxScroll>0)? (double)scrollOffset / (double)maxScroll : 0.0;
                int thumbY = trackY0 + (int)((trackH - thumbH) * scrollFrac + 0.5);
                RECT thumbR = { trackX+2, thumbY, trackX + trackW - 2, thumbY + thumbH };
                bool overThumb = (state.mouse_x>=thumbR.left && state.mouse_x<=thumbR.right && state.mouse_y>=thumbR.top && state.mouse_y<=thumbR.bottom);
                static bool draggingThumb = false; static int dragStartY=0; static int dragStartOffset=0;
                if (state.mouse_pressed && overThumb && !draggingThumb) { draggingThumb=true; dragStartY=state.mouse_y; dragStartOffset=scrollOffset; }
                if (!state.mouse_pressed && draggingThumb) draggingThumb=false;
                if (draggingThumb) {
                    int dy = state.mouse_y - dragStartY;
                    double trackMovable = (double)(trackH - thumbH);
                    double newFrac = (trackMovable>0)? ( (double)dragStartOffset + dy * (double)maxScroll / trackMovable ) / (double)maxScroll : 0.0;
                    if (newFrac < 0) newFrac = 0; if (newFrac > 1) newFrac = 1;
                    scrollOffset = (int)(newFrac * maxScroll + 0.5);
                }
                HBRUSH thBrush = CreateSolidBrush(draggingThumb?RGB(160,120,90):(overThumb?RGB(120,90,70):RGB(90,70,55)));
                FillRect(memDC,&thumbR,thBrush); DeleteObject(thBrush);
            }

            // Static bottom panel (buttons + legend)
            RECT panelR = { (int)round(30*ui_scale), panelTop, w - (int)round(30*ui_scale), h - (int)round(6*ui_scale) };
            HBRUSH pb = CreateSolidBrush(RGB(22,22,34)); FillRect(memDC,&panelR,pb); DeleteObject(pb);
            FrameRect(memDC,&panelR,(HBRUSH)GetStockObject(GRAY_BRUSH));
            int btnAreaH = (int)round(48*ui_scale);
            RECT btnRow = { panelR.left + (int)round(12*ui_scale), panelR.top + (int)round(10*ui_scale), panelR.right - (int)round(12*ui_scale), panelR.top + btnAreaH };
            int btnGap = (int)round(20*ui_scale);
            int btnW = (btnRow.right - btnRow.left - btnGap)/2;
            RECT resetBtnR = { btnRow.left, btnRow.top, btnRow.left + btnW, btnRow.bottom };
            RECT saveBtnR  = { resetBtnR.right + btnGap, btnRow.top, resetBtnR.right + btnGap + btnW, btnRow.bottom };
            auto drawButton=[&](RECT r,const wchar_t* label,bool hot,COLORREF base,COLORREF hotCol){
                HBRUSH bb=CreateSolidBrush(hot?hotCol:base); FillRect(memDC,&r,bb); DeleteObject(bb);
                FrameRect(memDC,&r,(HBRUSH)GetStockObject(GRAY_BRUSH));
                SetBkMode(memDC, TRANSPARENT); SetTextColor(memDC, RGB(235,235,245)); RECT tr=r; DrawTextW(memDC,label,-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            };
            bool hoverReset = (state.mouse_x>=resetBtnR.left && state.mouse_x<=resetBtnR.right && state.mouse_y>=resetBtnR.top && state.mouse_y<=resetBtnR.bottom);
            bool hoverSave  = (state.mouse_x>=saveBtnR.left  && state.mouse_x<=saveBtnR.right  && state.mouse_y>=saveBtnR.top  && state.mouse_y<=saveBtnR.bottom);
            if (saveFeedbackTicks>0) saveFeedbackTicks--; // countdown feedback
            const wchar_t* saveLabel = (saveFeedbackTicks>0)?L"Saved":L"Save Settings";
            drawButton(resetBtnR, L"Reset Defaults", hoverReset, RGB(60,35,35), RGB(90,50,50));
            drawButton(saveBtnR,  saveLabel, hoverSave,  RGB(35,55,70), RGB(55,85,110));
            RECT legendR = { panelR.left + (int)round(10*ui_scale), btnRow.bottom + (int)round(6*ui_scale), panelR.right - (int)round(10*ui_scale), panelR.bottom - (int)round(10*ui_scale) };
            std::wstring legend = L"Enter=Close  Esc=Cancel  Arrows/Drag adjust  PgUp/PgDn/Wheel  Ctrl+Click numeric entry";
            SetTextColor(memDC, RGB(200,200,215)); DrawTextW(memDC, legend.c_str(), -1, &legendR, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

            // If pointer is inside panel, assign hoverItem for tooltips AFTER drawing
            if (pointerInPanel) {
                if (hoverReset) hoverItem = 12; else if (hoverSave) hoverItem = 13; // map to tooltip indices
            }

            // Tooltip rendering near cursor with semi-transparent background
            if (hoverItem!=-1) {
                const wchar_t* tip = L"";
                switch(hoverItem) {
                    case 0: tip = L"Total rays budget per frame (or per pixel if forced). Higher = smoother, slower."; break;
                    case 1: tip = L"Maximum path bounce depth. More bounces capture more indirect light but cost time."; break;
                    case 2: tip = L"Internal rendering resolution percent. Lower improves speed, softer image."; break;
                    case 3: tip = L"Metallic surface roughness (0=mirror, 100=diffuse-ish)."; break;
                    case 4: tip = L"Relative emissive intensity of the ball glow."; break;
                    case 5: tip = L"Temporal accumulation alpha (blend weight). Lower = more smoothing, slower response."; break;
                    case 6: tip = L"Spatial denoise blend percent. Higher = blurrier, hides noise."; break;
                    case 7: tip = L"Force interpreting Rays/Frame as rays-per-pixel instead of global budget."; break;
                    case 8: tip = L"Switch between Orthographic and Perspective camera projection."; break;
                    case 9: tip = L"Enable probabilistic early termination to reduce average path length."; break;
                    case 10: tip = L"Bounce depth at which Russian roulette begins to test termination."; break;
                    case 11: tip = L"Minimum survival probability clamp for roulette (higher keeps more paths)."; break;
                    case 12: tip = L"Reset all settings to defaults."; break;
                    case 13: tip = L"Save current settings to file immediately."; break;
                }
                if (tip && *tip) {
                    SIZE sz={0,0}; GetTextExtentPoint32W(memDC, tip, (int)wcslen(tip), &sz);
                    int lineW = (int)round(320*ui_scale);
                    // rough multi-line height estimate if width exceeds lineW
                    int boxW = (std::min)(lineW, (int)(sz.cx + 16));
                    int boxH = (int)(sz.cy + 16);
                    // if text wider than boxW, add extra height for wrapping approximation
                    if (sz.cx > boxW) {
                        int lines = (sz.cx + boxW - 1) / boxW;
                        boxH = lines * (sz.cy + 4) + 12;
                    }
                    int mx = state.mouse_x + 18; int my = state.mouse_y + 18; // offset from cursor
                    RECT tr = { mx, my, mx + boxW, my + boxH };
                    if (tr.right > w-8) { int d = tr.right - (w-8); tr.left -= d; tr.right -= d; }
                    if (tr.bottom > h-8) { int d = tr.bottom - (h-8); tr.top -= d; tr.bottom -= d; }
                    HBRUSH tb = CreateSolidBrush(RGB(30,30,50)); FillRect(memDC,&tr,tb); DeleteObject(tb);
                    FrameRect(memDC,&tr,(HBRUSH)GetStockObject(GRAY_BRUSH));
                    SetBkMode(memDC, TRANSPARENT);
                    SetTextColor(memDC, RGB(215,215,235));
                    RECT textRect = { tr.left + 6, tr.top + 4, tr.right - 6, tr.bottom - 4 };
                    DrawTextW(memDC, tip, -1, &textRect, DT_LEFT | DT_WORDBREAK | DT_TOP);
                }
            }
            BitBlt(hdcLocal,0,0,w,h,memDC,0,0,SRCCOPY); ReleaseDC(hwnd, hdcLocal);
            // Input
            if (state.key_down[VK_DOWN]) { sel++; clampSel(sel); state.key_down[VK_DOWN]=false; }
            if (state.key_down[VK_UP]) { sel--; clampSel(sel); state.key_down[VK_UP]=false; }
            if (sel < baseSliderCount) {
                if (state.key_down[VK_LEFT]) { *sliders[sel].val = (std::max)(sliders[sel].minv, *sliders[sel].val - sliders[sel].step); state.key_down[VK_LEFT]=false; }
                if (state.key_down[VK_RIGHT]) { *sliders[sel].val = (std::min)(sliders[sel].maxv, *sliders[sel].val + sliders[sel].step); state.key_down[VK_RIGHT]=false; }
            } else if (sel==idxForce && (state.key_down[VK_LEFT]||state.key_down[VK_RIGHT])) { settings.pt_force_full_pixel_rays = settings.pt_force_full_pixel_rays?0:1; state.key_down[VK_LEFT]=state.key_down[VK_RIGHT]=false; }
            else if (sel==idxCamera && (state.key_down[VK_LEFT]||state.key_down[VK_RIGHT])) { settings.pt_use_ortho = settings.pt_use_ortho?0:1; state.key_down[VK_LEFT]=state.key_down[VK_RIGHT]=false; }
            else if (sel==idxRREnable && (state.key_down[VK_LEFT]||state.key_down[VK_RIGHT])) { settings.pt_rr_enable = settings.pt_rr_enable?0:1; state.key_down[VK_LEFT]=state.key_down[VK_RIGHT]=false; }
            else if (sel==idxRRStart) {
                if (state.key_down[VK_LEFT]) { settings.pt_rr_start_bounce = (std::max)(1, settings.pt_rr_start_bounce - 1); state.key_down[VK_LEFT]=false; }
                if (state.key_down[VK_RIGHT]) { settings.pt_rr_start_bounce = (std::min)(16, settings.pt_rr_start_bounce + 1); state.key_down[VK_RIGHT]=false; }
            } else if (sel==idxRRMin) {
                if (state.key_down[VK_LEFT]) { settings.pt_rr_min_prob_pct = (std::max)(1, settings.pt_rr_min_prob_pct - 1); state.key_down[VK_LEFT]=false; }
                if (state.key_down[VK_RIGHT]) { settings.pt_rr_min_prob_pct = (std::min)(90, settings.pt_rr_min_prob_pct + 1); state.key_down[VK_RIGHT]=false; }
            }
            // Scroll keys
            if (state.key_down[VK_PRIOR]) { // PageUp
                scrollOffset -= (int)(h*0.5); if (scrollOffset<0) scrollOffset=0; state.key_down[VK_PRIOR]=false; }
            if (state.key_down[VK_NEXT]) { // PageDown
                scrollOffset += (int)(h*0.5); if (scrollOffset>maxScroll) scrollOffset=maxScroll; state.key_down[VK_NEXT]=false; }
            if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; cancelled=true; settings = originalSettings; editing=false; }
            if (state.key_down[VK_RETURN]) { state.key_down[VK_RETURN]=false; editing=false; }
            // Ctrl+Click on a slider: direct numeric entry (basic modal capture)
            if (state.last_click_x!=-1 && (GetKeyState(VK_CONTROL) & 0x8000)) {
                int mx = state.last_click_x; int my = state.last_click_y; state.last_click_x=-1; state.last_click_y=-1;
                if (my < panelTop) { // only allow numeric entry if not inside panel
                for (int i=0;i<7;i++) {
                    int y = baseY + i*rowH; int bx = centerX - barW/2; int by = y + (int)round(14*ui_scale);
                    RECT bar={bx,by,bx+barW,by+barH};
                    if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) {
                        // create lightweight input box
                        int boxW = 160; int boxH = 24;
                        // Avoid std::min/std::max because Windows headers may define macros
                        // that collide with these names. Use explicit clamping instead.
                        int bxw = bx;
                        if (bxw < 10) bxw = 10;
                        int bxw_max = w - boxW - 10;
                        if (bxw > bxw_max) bxw = bxw_max;
                        int bxy = by - 30;
                        if (bxy < 10) bxy = 10;
                        HWND edit = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW, L"EDIT", std::to_wstring(*sliders[i].val).c_str(), WS_VISIBLE|WS_CHILD|ES_LEFT,
                            bxw, bxy, boxW, boxH, hwnd, NULL, hInstance, NULL);
                        if (edit) {
                            HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0); if (f) SendMessageW(edit, WM_SETFONT, (WPARAM)f, TRUE);
                            bool done=false; std::wstring buffer;
                            while(!done && state.running) {
                                MSG em; while(PeekMessage(&em,nullptr,0,0,PM_REMOVE)) { if (em.message==WM_KEYDOWN && em.wParam==VK_RETURN) { done=true; } else if (em.message==WM_KEYDOWN && em.wParam==VK_ESCAPE) { buffer.clear(); done=true; } TranslateMessage(&em); DispatchMessage(&em);}                                
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                            int len = GetWindowTextLengthW(edit); if (len>0){ buffer.resize(len+1); GetWindowTextW(edit,&buffer[0],len+1); buffer.resize(len);} else buffer.clear();
                            DestroyWindow(edit);
                            if (!buffer.empty()) {
                                try { int nv = std::stoi(buffer); if (nv < sliders[i].minv) nv=sliders[i].minv; if (nv>sliders[i].maxv) nv=sliders[i].maxv; *sliders[i].val = nv; sel=i; } catch(...) {}
                            }
                        }
                        break;
                    }
                }
                }
            }
            // Mouse drag sliders
            if (state.mouse_pressed && state.mouse_y < panelTop) {
                int mx = state.mouse_x; int my = state.mouse_y;
                for (int i=0;i<sliderCount;i++) {
                    int y = baseY + i*rowH; int bx = centerX - barW/2; int by = y + (int)round(14*ui_scale);
                    RECT bar={bx,by,bx+barW,by+barH};
                    if (by >= panelTop) continue; // hidden behind panel
                    if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) {
                        double tt = double(mx - bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1;
                        int val = sliders[i].minv + (int)std::round(tt*(sliders[i].maxv - sliders[i].minv));
                        int step = sliders[i].step; val = (val/step)*step;
                        if (val < sliders[i].minv) val = sliders[i].minv; if (val > sliders[i].maxv) val = sliders[i].maxv;
                        *sliders[i].val = val; sel=i;
                    }
                }
                // RR sliders
                {
                    int bx = centerX - barW/2; int by = (baseY + (baseSliderCount+3)*rowH) + (int)round(14*ui_scale);
                    RECT bar={bx,by,bx+barW,by+barH};
                    if (by < panelTop)
                    if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) {
                        double tt = double(mx - bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1;
                        int val = 1 + (int)std::round(tt*(16 - 1)); if (val<1) val=1; if (val>16) val=16;
                        settings.pt_rr_start_bounce = val; sel=idxRRStart;
                    }
                }
                {
                    int bx = centerX - barW/2; int by = (baseY + (baseSliderCount+4)*rowH) + (int)round(14*ui_scale);
                    RECT bar={bx,by,bx+barW,by+barH};
                    if (by < panelTop)
                    if (mx>=bar.left && mx<=bar.right && my>=bar.top && my<=bar.bottom) {
                        double tt = double(mx - bar.left)/barW; if(tt<0)tt=0; if(tt>1)tt=1;
                        int val = 1 + (int)std::round(tt*(90 - 1)); if (val<1) val=1; if (val>90) val=90;
                        settings.pt_rr_min_prob_pct = val; sel=idxRRMin;
                    }
                }
            }
            // Mouse click checkboxes on release
            if (state.last_click_x!=-1) {
                int cx = state.last_click_x; int cy = state.last_click_y;
                auto hitRect=[&](int yCenter, int halfH){ if (yCenter+halfH >= panelTop) return false; RECT r={centerX - (int)(220*ui_scale), yCenter - halfH, centerX + (int)(220*ui_scale), yCenter + halfH}; return cx>=r.left && cx<=r.right && cy>=r.top && cy<=r.bottom; };
                if (cy < panelTop && hitRect(cyForce, (int)(16*ui_scale))) { settings.pt_force_full_pixel_rays = settings.pt_force_full_pixel_rays?0:1; sel=idxForce; }
                else if (cy < panelTop && hitRect(cyCam, (int)(16*ui_scale))) { settings.pt_use_ortho = settings.pt_use_ortho?0:1; sel=idxCamera; }
                else if (cy < panelTop && hitRect(cyRRE, (int)(16*ui_scale))) { settings.pt_rr_enable = settings.pt_rr_enable?0:1; sel=idxRREnable; }
                // static bottom panel buttons (only if click inside panel)
                else if (cy >= panelTop) {
                    int btnAreaH = (int)round(48*ui_scale);
                    RECT btnRow = { panelR.left + (int)round(12*ui_scale), panelR.top + (int)round(10*ui_scale), panelR.right - (int)round(12*ui_scale), panelR.top + btnAreaH };
                    int btnGap = (int)round(20*ui_scale);
                    int btnW = (btnRow.right - btnRow.left - btnGap)/2;
                    RECT resetBtnR2 = { btnRow.left, btnRow.top, btnRow.left + btnW, btnRow.bottom };
                    RECT saveBtnR2  = { resetBtnR2.right + btnGap, btnRow.top, resetBtnR2.right + btnGap + btnW, btnRow.bottom };
                    if (cx>=resetBtnR2.left && cx<=resetBtnR2.right && cy>=resetBtnR2.top && cy<=resetBtnR2.bottom) {
                        settings.pt_rays_per_frame = 1; settings.pt_max_bounces = 5; settings.pt_internal_scale = 10; settings.pt_roughness = 0; settings.pt_emissive = 100; settings.pt_accum_alpha = 75; settings.pt_denoise_strength = 25; settings.pt_force_full_pixel_rays = 1; settings.pt_use_ortho = 1; settings.pt_rr_enable = 1; settings.pt_rr_start_bounce = 2; settings.pt_rr_min_prob_pct = 10; originalSettings = settings; sel=idxReset; }
                    else if (cx>=saveBtnR2.left && cx<=saveBtnR2.right && cy>=saveBtnR2.top && cy<=saveBtnR2.bottom) { settingsMgr.save(exeDir + L"settings.json", settings); originalSettings = settings; saveFeedbackTicks = 60; }
                }
                state.last_click_x=-1; state.last_click_y=-1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        state.ui_mode = 1; 
        if (!cancelled) {
            // Mark changed so caller persists once; Save button already persisted to disk
            settings_changed = true; 
        } else {
            // Cancelled: do not mark changed (settings already restored to baseline)
        }
    };

    // Ensure WindowProc knows we're in the menu so mouse clicks map to menu options
    state.ui_mode = 1;
    // Main configuration/menu loop
    while (inMenu && state.running) {
        // pump messages
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }

        int winW = state.width; int winH = state.height;
        int dpi_now = state.dpi;
        if (dpi_now == 96) {
            HMODULE user32_q = LoadLibraryW(L"user32.dll");
            if (user32_q) {
                auto pGetDpiForWindow_q = (UINT(WINAPI*)(HWND))GetProcAddress(user32_q, "GetDpiForWindow");
                if (pGetDpiForWindow_q) dpi_now = pGetDpiForWindow_q(hwnd);
                FreeLibrary(user32_q);
            }
        }
        double ui_scale = (double)dpi_now / 96.0;

        // draw menu background - subtle gradient imitation using two rects
        RECT rtop = {0,0,winW, winH/2}; RECT rbot = {0, winH/2, winW, winH};
        HBRUSH btop = CreateSolidBrush(RGB(20,20,30));
        HBRUSH bbot = CreateSolidBrush(RGB(10,10,20));
        FillRect(memDC, &rtop, btop);
        FillRect(memDC, &rbot, bbot);
        DeleteObject(btop); DeleteObject(bbot);

        SetTextColor(memDC, RGB(220,220,220));
        SetBkMode(memDC, TRANSPARENT);
        DrawTextCentered(memDC, L"Pong - Configuration", winW/2, (int)round(40*ui_scale));

        // compute hover index from current mouse position
        int hoverIndex = -1;
        {
            int mx = state.mouse_x; int my = state.mouse_y;
            int baseX = winW/2 - (int)round(170*ui_scale);
            int ys[7] = { (int)(120 * ui_scale + 0.5), (int)(170 * ui_scale + 0.5), (int)(220 * ui_scale + 0.5), (int)(270 * ui_scale + 0.5), (int)(330 * ui_scale + 0.5), (int)(380 * ui_scale + 0.5), (int)(430 * ui_scale + 0.5) };
            for (int i=0;i<7;i++) {
                int pad = (int)max(6.0, 10.0 * ui_scale);
                int wbox = (int)max(260.0, 260.0 * ui_scale);
                RECT rb = { baseX - pad, ys[i] - (int)(6*ui_scale + 0.5), baseX + wbox, ys[i] + (int)(34*ui_scale + 0.5) };
                if (mx >= rb.left && mx <= rb.right && my >= rb.top && my <= rb.bottom) { hoverIndex = i; break; }
            }
        }

        // options with highlight (keyboard selection or mouse hover)
        auto drawOption = [&](int idx, const std::wstring &text, int x, int y){
            int pad = (int)max(6.0, 10.0 * ui_scale);
            int wbox = (int)max(260.0, 260.0 * ui_scale);
            RECT rb = {x - pad, y - (int)round(6*ui_scale), x + wbox, y + (int)round(34*ui_scale)};
            if (menuIndex == idx) {
                HBRUSH sel = CreateSolidBrush(RGB(60,60,90));
                FillRect(memDC, &rb, sel);
                DeleteObject(sel);
                SetTextColor(memDC, RGB(255,255,200));
            } else if (hoverIndex == idx) {
                HBRUSH sel = CreateSolidBrush(RGB(40,40,70));
                FillRect(memDC, &rb, sel);
                DeleteObject(sel);
                SetTextColor(memDC, RGB(230,230,200));
            } else {
                SetTextColor(memDC, RGB(200,200,200));
            }
            // center text inside the option rect
            DrawTextCentered(memDC, text, (rb.left + rb.right)/2, (rb.top + rb.bottom)/2);
        };

    drawOption(0, (ctrl==CTRL_KEYBOARD)?L"Control: Keyboard":L"Control: Mouse", winW/2 - (int)round(170*ui_scale), (int)round(120*ui_scale));
    drawOption(1, (ai==AI_EASY)?L"AI: Easy":(ai==AI_NORMAL)?L"AI: Normal":L"AI: Hard", winW/2 - (int)round(170*ui_scale), (int)round(170*ui_scale));
    drawOption(2, (rendererMode==R_CLASSIC)?L"Renderer: Classic":L"Renderer: Path Tracer", winW/2 - (int)round(170*ui_scale), (int)round(220*ui_scale));
    drawOption(3, L"Path Tracer Settings...", winW/2 - (int)round(170*ui_scale), (int)round(270*ui_scale));
    drawOption(4, L"Start Game", winW/2 - (int)round(170*ui_scale), (int)round(330*ui_scale));
    drawOption(5, L"Manage High Scores", winW/2 - (int)round(170*ui_scale), (int)round(380*ui_scale));
    drawOption(6, L"Quit", winW/2 - (int)round(170*ui_scale), (int)round(430*ui_scale));

        // show top 5 highscores on right
        SetTextColor(memDC, RGB(180,180,220));
        DrawTextCentered(memDC, L"High Scores", winW - (int)round(220*ui_scale), (int)round(60*ui_scale));
        for (size_t i=0;i<highList.size() && i<5;i++) {
            const auto &e = highList[i];
            std::wstring line = std::to_wstring(i+1) + L"  " + e.name + L"  " + std::to_wstring(e.score);
            DrawTextCentered(memDC, line, winW - (int)round(220*ui_scale), (int)round(100*ui_scale) + (int)(i*30*ui_scale));
        }

        // blit and present
        BitBlt(hdc, 0, 0, winW, winH, memDC, 0, 0, SRCCOPY);

        // handle simple keyboard navigation and mouse clicks
        if (state.key_down[VK_DOWN]) { menuIndex++; clamp_menu(menuIndex, 0, 6); state.key_down[VK_DOWN]=false; }
        if (state.key_down[VK_UP]) { menuIndex--; clamp_menu(menuIndex, 0, 6); state.key_down[VK_UP]=false; }
        if (state.key_down[VK_LEFT]) {
            if (menuIndex==0) { ctrl = CTRL_KEYBOARD; settings.control_mode = 0; settings_changed = true; }
            else if (menuIndex==1) { if (ai>0) { ai=(AIDifficulty)(ai-1); settings.ai = ai; settings_changed = true; } }
            else if (menuIndex==2) { rendererMode = R_CLASSIC; settings.renderer = 0; settings_changed = true; }
            else if (menuIndex==3) { }
            state.key_down[VK_LEFT]=false;
        }
        if (state.key_down[VK_RIGHT]) {
            if (menuIndex==0) { ctrl = CTRL_MOUSE; settings.control_mode = 1; settings_changed = true; }
            else if (menuIndex==1) { if (ai<2) { ai=(AIDifficulty)(ai+1); settings.ai = ai; settings_changed = true; } }
            else if (menuIndex==2) { rendererMode = R_PATH; settings.renderer = 1; settings_changed = true; }
            else if (menuIndex==3) { }
            state.key_down[VK_RIGHT]=false;
        }
        if (state.key_down[VK_RETURN]) {
            state.key_down[VK_RETURN]=false;
            if (menuIndex==3) { if (rendererMode==R_PATH) { runPathTracerSettingsModal(); settings_changed=true; } }
            else if (menuIndex==4) { inMenu = false; state.ui_mode = 0; }
            else if (menuIndex==5) { manageHighScoresModal(); hsMgr.save(hsPath, highList); }
            else if (menuIndex==6) { state.running = false; break; }
        }
        if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; state.running=false; break; }

        // process mouse clicks (menu_click_index)
        if (state.menu_click_index != -1) {
            int clicked = state.menu_click_index; state.menu_click_index = -1;
            switch(clicked) {
                case 0: ctrl = (ctrl==CTRL_KEYBOARD)?CTRL_MOUSE:CTRL_KEYBOARD; settings.control_mode = (ctrl==CTRL_MOUSE)?1:0; settings_changed=true; break;
                case 1: ai = (AIDifficulty)((ai+1)%3); settings.ai = ai; settings_changed=true; break;
                case 2: rendererMode = (rendererMode==R_CLASSIC)?R_PATH:R_CLASSIC; settings.renderer=(rendererMode==R_PATH)?1:0; settings_changed=true; break;
                case 3: { if (rendererMode==R_PATH) { runPathTracerSettingsModal(); settings_changed = true; } } break;
                case 4: inMenu=false; break;
                case 5: manageHighScoresModal(); hsMgr.save(hsPath, highList); break;
                case 6: state.running=false; break;
            }
        }

    // leaving menu; ensure UI mode returns to gameplay
    state.ui_mode = 0;

        if (settings_changed) {
            settingsMgr.save(exeDir + L"settings.json", settings);
            settings_changed = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Initialize software renderer here (after menu selection)
    SoftRenderer soft;
    SRConfig srCfg;
    srCfg.enablePathTracing = (rendererMode==R_PATH);
    srCfg.raysPerFrame = settings.pt_rays_per_frame;
    srCfg.maxBounces = settings.pt_max_bounces;
    srCfg.internalScalePct = settings.pt_internal_scale;
    srCfg.metallicRoughness = settings.pt_roughness / 100.0f;
    srCfg.emissiveIntensity = settings.pt_emissive / 100.0f; // 100% == 1.0
    srCfg.accumAlpha = settings.pt_accum_alpha / 100.0f;
    srCfg.denoiseStrength = settings.pt_denoise_strength / 100.0f;
    srCfg.forceFullPixelRays = settings.pt_force_full_pixel_rays!=0;
    srCfg.useOrtho = settings.pt_use_ortho!=0;
    srCfg.rouletteEnable = settings.pt_rr_enable!=0;
    srCfg.rouletteStartBounce = settings.pt_rr_start_bounce;
    srCfg.rouletteMinProb = settings.pt_rr_min_prob_pct / 100.0f;
    soft.configure(srCfg);
    soft.resize(state.width, state.height);

    while (state.running) {
        if (state.request_menu) {
            state.request_menu = false;
            // Full main menu re-entry (same logic as initial menu phase)
            inMenu = true; state.ui_mode = 1; state.menu_click_index = -1; // reset selection
            state.suppressMenuClickDown = true; // prevent double toggle on press+release
            while (inMenu && state.running) {
                MSG msgM; while (PeekMessage(&msgM,NULL,0,0,PM_REMOVE)){ TranslateMessage(&msgM); DispatchMessage(&msgM);}                
                int w = state.width; int h = state.height; int dpi_now2 = state.dpi; if (dpi_now2==96){ HMODULE u=LoadLibraryW(L"user32.dll"); if(u){ auto p=(UINT(WINAPI*)(HWND))GetProcAddress(u,"GetDpiForWindow"); if(p) dpi_now2=p(hwnd); FreeLibrary(u);} }
                double ui_scale2 = (double)dpi_now2 / 96.0; HDC memDC2 = state.memDC; HDC hdc2 = GetDC(hwnd);
                RECT bg={0,0,w,h}; HBRUSH bb=CreateSolidBrush(RGB(12,12,20)); FillRect(memDC2,&bg,bb); DeleteObject(bb);
                SetBkMode(memDC2, TRANSPARENT);
                SetTextColor(memDC2, RGB(235,235,245));
                DrawTextCentered(memDC2, L"PongCpp", w/2, (int)round(60*ui_scale2));
                std::wstring items[7];
                items[0] = std::wstring(L"Control: ") + ((ctrl==CTRL_KEYBOARD)?L"Keyboard":L"Mouse");
                items[1] = std::wstring(L"AI: ") + ((ai==AI_EASY)?L"Easy": (ai==AI_NORMAL)?L"Normal":L"Hard");
                items[2] = std::wstring(L"Renderer: ") + ((rendererMode==R_CLASSIC)?L"Classic":L"Path Tracer");
                items[3] = L"Path Tracer Settings";
                items[4] = L"Start Game";
                items[5] = L"High Scores";
                items[6] = L"Quit";
                int baseY = (int)round(120*ui_scale2);
                for (int i=0;i<7;i++) {
                    bool hot = (i==menuIndex);
                    SetTextColor(memDC2, hot?RGB(255,240,160):RGB(190,190,200));
                    DrawTextCentered(memDC2, items[i], w/2, baseY + i*(int)round(50*ui_scale2));
                }
                BitBlt(hdc2,0,0,w,h,memDC2,0,0,SRCCOPY);
                ReleaseDC(hwnd, hdc2);
                // Keyboard navigation
                if (state.key_down[VK_DOWN]) { state.key_down[VK_DOWN]=false; menuIndex = (menuIndex+1)%7; }
                if (state.key_down[VK_UP]) { state.key_down[VK_UP]=false; menuIndex = (menuIndex+6)%7; }
                if (state.key_down[VK_LEFT]) {
                    state.key_down[VK_LEFT]=false;
                    if (menuIndex==0) { ctrl = (ctrl==CTRL_KEYBOARD)?CTRL_MOUSE:CTRL_KEYBOARD; settings.control_mode = (ctrl==CTRL_MOUSE)?1:0; settings_changed=true; }
                    else if (menuIndex==1) { ai = (AIDifficulty)((ai+2)%3); settings.ai = ai; settings_changed=true; }
                    else if (menuIndex==2) { rendererMode = (rendererMode==R_CLASSIC)?R_PATH:R_CLASSIC; settings.renderer=(rendererMode==R_PATH)?1:0; settings_changed=true; }
                }
                if (state.key_down[VK_RIGHT]) {
                    state.key_down[VK_RIGHT]=false;
                    if (menuIndex==0) { ctrl = (ctrl==CTRL_KEYBOARD)?CTRL_MOUSE:CTRL_KEYBOARD; settings.control_mode = (ctrl==CTRL_MOUSE)?1:0; settings_changed=true; }
                    else if (menuIndex==1) { ai = (AIDifficulty)((ai+1)%3); settings.ai = ai; settings_changed=true; }
                    else if (menuIndex==2) { rendererMode = (rendererMode==R_CLASSIC)?R_PATH:R_CLASSIC; settings.renderer=(rendererMode==R_PATH)?1:0; settings_changed=true; }
                }
                if (state.key_down[VK_RETURN]) {
                    state.key_down[VK_RETURN]=false;
                    if (menuIndex==3) {
                        runPathTracerSettingsModal();
                    } else if (menuIndex==4) { inMenu=false; }
                    else if (menuIndex==5) { manageHighScoresModal(); hsMgr.save(hsPath, highList); }
                    else if (menuIndex==6) { state.running=false; }
                }
                if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; state.running=false; break; }
                // Mouse click detection (reuse original hit testing simplified)
                if (state.last_click_x!=-1) {
                    int cx = state.last_click_x; int cy = state.last_click_y; state.last_click_x=-1; state.last_click_y=-1;
                    // Simple vertical band detection
                    for (int i=0;i<7;i++) {
                        int y = baseY + i*(int)round(50*ui_scale2);
                        RECT rItem = { w/2 - 250, y - 20, w/2 + 250, y + 20 };
                        if (cx>=rItem.left && cx<=rItem.right && cy>=rItem.top && cy<=rItem.bottom) {
                            menuIndex = i; state.menu_click_index = i; break; }
                    }
                }
                if (state.menu_click_index != -1) {
                    int clicked = state.menu_click_index; state.menu_click_index=-1;
                    switch(clicked) {
                        case 0: ctrl = (ctrl==CTRL_KEYBOARD)?CTRL_MOUSE:CTRL_KEYBOARD; settings.control_mode=(ctrl==CTRL_MOUSE)?1:0; settings_changed=true; break;
                        case 1: ai = (AIDifficulty)((ai+1)%3); settings.ai=ai; settings_changed=true; break;
                        case 2: rendererMode = (rendererMode==R_CLASSIC)?R_PATH:R_CLASSIC; settings.renderer=(rendererMode==R_PATH)?1:0; settings_changed=true; break;
                        case 3: { if (rendererMode==R_PATH) { runPathTracerSettingsModal(); } } break;
                        case 4: inMenu=false; break;
                        case 5: manageHighScoresModal(); hsMgr.save(hsPath, highList); break;
                        case 6: state.running=false; break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            state.ui_mode = 0;
            state.suppressMenuClickDown = false;
            if (settings_changed) { settingsMgr.save(exeDir + L"settings.json", settings); settings_changed=false; }
            // Sync renderer enable switch & reset history after menu changes
            bool newEnable = (rendererMode==R_PATH);
            if (srCfg.enablePathTracing != newEnable) { srCfg.enablePathTracing = newEnable; soft.configure(srCfg); soft.resetHistory(); }
        }
        // message pump
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // timing
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last;
        double dt = elapsed.count();
        if (dt < target_dt) {
            std::this_thread::sleep_for(std::chrono::duration<double>(target_dt - dt));
            continue;
        }
        last = now;

        // get latest size/dpi and clear backbuffer
        int winW = state.width;
        int winH = state.height;
        int dpi_now = state.dpi;
        if (dpi_now == 96) {
            HMODULE user32_q = LoadLibraryW(L"user32.dll");
            if (user32_q) {
                auto pGetDpiForWindow_q = (UINT(WINAPI*)(HWND))GetProcAddress(user32_q, "GetDpiForWindow");
                if (pGetDpiForWindow_q) dpi_now = pGetDpiForWindow_q(hwnd);
                FreeLibrary(user32_q);
            }
        }
        double ui_scale = (double)dpi_now / 96.0;

        // Resize renderer if needed (track last known size explicitly)
        static int lastRenderW = -1, lastRenderH = -1;
        if (winW != lastRenderW || winH != lastRenderH) {
            soft.resize(winW, winH);
            soft.resetHistory();
            lastRenderW = winW; lastRenderH = winH;
        }

        // Expose game state for rendering and UI (move out of renderer-specific blocks so it's available later)
        GameState &gs = core.state();

        // ----- Input & Simulation (runs for both renderers) -----
        if (ctrl == CTRL_KEYBOARD) {
            if (state.key_down['W']) core.move_left_by(-120.0 * dt);
            if (state.key_down['S']) core.move_left_by(120.0 * dt);
        } else {
            double my = (double)state.mouse_y / winH * gs.gh;
            core.set_left_y(my - gs.paddle_h/2.0);
        }
        if (state.key_down[VK_UP]) core.move_right_by(-120.0 * dt);
        if (state.key_down[VK_DOWN]) core.move_right_by(120.0 * dt);

        if (ai == AI_EASY) core.set_ai_speed(0.6);
        else if (ai == AI_NORMAL) core.set_ai_speed(1.0);
        else core.set_ai_speed(1.6);
        core.update(dt);

        // Detect quality / renderer changes (from settings file or future hotkeys) - not currently dynamic but safe guard
        // (If we later allow runtime toggle outside menu, we can set a flag to trigger resetHistory())

        if (rendererMode == R_PATH) {
             // Update SRConfig from current settings if changed
             bool changed=false;
             auto applyIf=[&](auto &dst, auto v){ if (dst!=v){ dst=v; changed=true;} };
            applyIf(srCfg.raysPerFrame, settings.pt_rays_per_frame);
            applyIf(srCfg.maxBounces, settings.pt_max_bounces);
            applyIf(srCfg.internalScalePct, settings.pt_internal_scale);
            applyIf(srCfg.metallicRoughness, settings.pt_roughness/100.0f);
            applyIf(srCfg.emissiveIntensity, settings.pt_emissive/100.0f);
            applyIf(srCfg.accumAlpha, settings.pt_accum_alpha/100.0f);
            applyIf(srCfg.denoiseStrength, settings.pt_denoise_strength/100.0f);
            bool forceFlag = settings.pt_force_full_pixel_rays!=0; applyIf(srCfg.forceFullPixelRays, forceFlag);
            bool orthoFlag = settings.pt_use_ortho!=0; applyIf(srCfg.useOrtho, orthoFlag);
            bool rrEnable = settings.pt_rr_enable!=0; applyIf(srCfg.rouletteEnable, rrEnable);
            applyIf(srCfg.rouletteStartBounce, settings.pt_rr_start_bounce);
            float rrMinProb = settings.pt_rr_min_prob_pct / 100.0f; applyIf(srCfg.rouletteMinProb, rrMinProb);
            if (!srCfg.enablePathTracing) { srCfg.enablePathTracing=true; changed=true; }
             if (changed) { soft.configure(srCfg); soft.resetHistory(); }
            soft.render(core.state());
            // Blit software renderer output into memDC
            const BITMAPINFO &bi = soft.getBitmapInfo();
            const void *pix = soft.pixels();
            StretchDIBits(memDC, 0,0, winW, winH, 0,0, winW, winH, pix, &bi, DIB_RGB_COLORS, SRCCOPY);
            // JSON performance logging (append line)
            {
                static FILE* perfFile = nullptr;
                if (!perfFile) {
                    std::wstring path = exeDir + L"perf_log.json";
                    FILE* fTemp = nullptr;
                    if (_wfopen_s(&fTemp, path.c_str(), L"ab") == 0) {
                        perfFile = fTemp; // success
                    }
                }
                if (perfFile) {
                    const SRStats &st = soft.stats();
                    // serialize current SRConfig & stats (subset) as JSON line
                    fprintf(perfFile,
                        "{\"frame\":%u,\"ms\":{\"total\":%.3f,\"trace\":%.3f,\"temporal\":%.3f,\"denoise\":%.3f,\"upscale\":%.3f},\"res\":{\"w\":%d,\"h\":%d},\"spp\":%d,\"rays\":%d,\"avgBounce\":%.3f,\"config\":{\"raysPerFrame\":%d,\"maxBounces\":%d,\"internalScalePct\":%d,\"roughness\":%.3f,\"emissive\":%.3f,\"accumAlpha\":%.3f,\"denoise\":%.3f,\"forceFullPixelRays\":%s,\"useOrtho\":%s}}\n",
                        st.frame, st.msTotal, st.msTrace, st.msTemporal, st.msDenoise, st.msUpscale,
                        st.internalW, st.internalH, st.spp, st.totalRays, st.avgBounceDepth,
                        srCfg.raysPerFrame, srCfg.maxBounces, srCfg.internalScalePct, srCfg.metallicRoughness, srCfg.emissiveIntensity,
                        srCfg.accumAlpha, srCfg.denoiseStrength, srCfg.forceFullPixelRays?"true":"false", srCfg.useOrtho?"true":"false");
                    fflush(perfFile);
                }
            }
            // Stats overlay (top-left)
            const SRStats &st = soft.stats();
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(230,230,180));
            wchar_t buf[256];
            swprintf(buf, 256, L"PT %.2fms (trace %.2f t%.2f d%.2f u%.2f) Rays %d spp %d %dx%d avgB %.2f",
                st.msTotal, st.msTrace, st.msTemporal, st.msDenoise, st.msUpscale,
                st.totalRays, st.spp, st.internalW, st.internalH, st.avgBounceDepth);
            RECT rStats = { 8, 8, winW/2, 100 };
            DrawTextW(memDC, buf, -1, &rStats, DT_LEFT | DT_TOP | DT_NOCLIP);
            // Overlay paddles & ball outlines for clearer gameplay feedback
            GameState &ogs = core.state();
            const int gw = 80, gh = 24;
            auto mapX = [&](double gx){ return (int)(gx / gw * winW); };
            auto mapY = [&](double gy){ return (int)(gy / gh * winH); };
            HPEN padPen = CreatePen(PS_SOLID, max(1,(int)round(2*ui_scale)), RGB(200,220,255));
            HPEN oldP = (HPEN)SelectObject(memDC, padPen);
            HBRUSH hollow = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldB = (HBRUSH)SelectObject(memDC, hollow);
            // Left paddle
            int lp1 = mapX(1); int lp2 = mapX(3);
            int lpt = mapY(ogs.left_y); int lpb = mapY(ogs.left_y + ogs.paddle_h);
            RoundRect(memDC, lp1, lpt, lp2, lpb, 8, 8);
            // Right paddle
            int rp1 = mapX(gw-3); int rp2 = mapX(gw-1);
            int rpt = mapY(ogs.right_y); int rpb = mapY(ogs.right_y + ogs.paddle_h);
            RoundRect(memDC, rp1, rpt, rp2, rpb, 8, 8);
            // Ball highlight circle
            int bx = mapX(ogs.ball_x); int by = mapY(ogs.ball_y); int br = max(4,(int)round(8*ui_scale));
            Ellipse(memDC, bx-br, by-br, bx+br, by+br);
            SelectObject(memDC, oldP); SelectObject(memDC, oldB);
            DeleteObject(padPen);
        } else {
            HBRUSH bg = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT r = {0,0,winW,winH};
            FillRect(memDC, &r, bg);
        }

    // draw center dashed line with glow (two passes) (always on top of path traced background)
    int thin_w = max(1, (int)round(2 * ui_scale));
    int glow_w = max(3, (int)round(6 * ui_scale));
    HPEN penThin = CreatePen(PS_SOLID, thin_w, RGB(200,200,200));
    HPEN penGlow = CreatePen(PS_SOLID, glow_w, RGB(100,100,120));
    HPEN oldPen = (HPEN)SelectObject(memDC, penGlow);
    int dash_h = max(12, (int)round(20 * ui_scale));
    int dash_seg = max(6, (int)round(10 * ui_scale));
    for (int y=0;y<winH;y+=dash_h) { MoveToEx(memDC, winW/2, y, NULL); LineTo(memDC, winW/2, y+dash_seg); }
    SelectObject(memDC, penThin);
    for (int y=0;y<winH;y+=dash_h) { MoveToEx(memDC, winW/2, y, NULL); LineTo(memDC, winW/2, y+dash_seg); }

        if (rendererMode == R_CLASSIC) {
        // draw paddles and ball only in classic mode; path tracer already rendered them
        // We'll map game coordinates (width=80,height=24) to window size
    const int gw = 80, gh = 24;
    auto mapX = [&](double gx){ return (int)(gx / gw * winW); };
    auto mapY = [&](double gy){ return (int)(gy / gh * winH); };

    // draw left paddle as thicker rounded rect
    RECT pr;
    int px1 = mapX(1); int px2 = mapX(3);
    pr.left = px1; pr.right = px2;
    pr.top = mapY(gs.left_y); pr.bottom = mapY(gs.left_y + gs.paddle_h);
    // draw rounded ends by drawing rectangles and circles
    HBRUSH paddleBrush = CreateSolidBrush(RGB(240,240,240));
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, paddleBrush);
    // use a null pen so ellipse outlines don't draw with default pen color
    HPEN nullPen = (HPEN)GetStockObject(NULL_PEN);
    HPEN oldPenLocal = (HPEN)SelectObject(memDC, nullPen);
    FillRect(memDC, &pr, paddleBrush);
    int cx = (pr.left + pr.right)/2;
    int rad = max(1, (int)round((pr.right - pr.left)/2.0 * ui_scale));
    Ellipse(memDC, pr.left - rad, pr.top, pr.left + rad, pr.bottom); // left cap
    Ellipse(memDC, pr.right - rad, pr.top, pr.right + rad, pr.bottom); // right cap
    // restore pen/brush
    SelectObject(memDC, oldPenLocal);
    SelectObject(memDC, oldBrush);
    DeleteObject(paddleBrush);

    // draw right paddle
    int rx1 = mapX(gw-3); int rx2 = mapX(gw-1);
    pr.left = rx1; pr.right = rx2;
    pr.top = mapY(gs.right_y); pr.bottom = mapY(gs.right_y + gs.paddle_h);
    HBRUSH paddleBrush2 = CreateSolidBrush(RGB(240,240,240));
    HBRUSH oldBrush2 = (HBRUSH)SelectObject(memDC, paddleBrush2);
    HPEN oldPen2 = (HPEN)SelectObject(memDC, nullPen);
    FillRect(memDC, &pr, paddleBrush2);
    Ellipse(memDC, pr.left - rad, pr.top, pr.left + rad, pr.bottom);
    Ellipse(memDC, pr.right - rad, pr.top, pr.right + rad, pr.bottom);
    SelectObject(memDC, oldPen2);
    SelectObject(memDC, oldBrush2);
    DeleteObject(paddleBrush2);

    // draw ball slightly larger and with a subtle highlight
    int bx = mapX(gs.ball_x); int by = mapY(gs.ball_y);
    int ball_px_r = max(4, (int)round(8 * ui_scale));
    HBRUSH ballBrush = CreateSolidBrush(RGB(250,220,220));
    HBRUSH ballShade = CreateSolidBrush(RGB(200,80,80));
    Ellipse(memDC, bx-ball_px_r, by-ball_px_r, bx+ball_px_r, by+ball_px_r);
    // small inner shade
    SelectObject(memDC, ballShade);
    Ellipse(memDC, bx-ball_px_r/2, by-ball_px_r/2, bx+ball_px_r/2, by+ball_px_r/2);
    DeleteObject(ballBrush); DeleteObject(ballShade);
    } // end classic renderer objects

    // draw scores
    std::wstring scoreTxt = std::to_wstring(gs.score_left) + L" - " + std::to_wstring(gs.score_right);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255,255,255));
        DrawTextCentered(memDC, scoreTxt, (int)round(10*ui_scale), (int)round(10*ui_scale));

    // show high score (from list)
    std::wstring hs = L"High: " + ((highList.empty())?L"0":std::to_wstring(highList.front().score));
    DrawTextCentered(memDC, hs, winW - (int)round(220*ui_scale), (int)round(10*ui_scale));

    // blit backbuffer to screen
    BitBlt(hdc, 0, 0, winW, winH, memDC, 0, 0, SRCCOPY);
    // cleanup pens
    SelectObject(memDC, oldPen);
    DeleteObject(penThin); DeleteObject(penGlow);
    }

    // cleanup
    if (state.memDC) {
        SelectObject(state.memDC, state.oldBmp);
        // restore old font
        SelectObject(state.memDC, oldFont);
        DeleteObject(state.memBmp);
        DeleteDC(state.memDC);
        state.memDC = nullptr; state.memBmp = nullptr; state.oldBmp = nullptr;
    }
    // destroy UI font
    DeleteObject(uiFont);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    // on exit: if beaten, prompt in-console name entry is not ideal for GUI; instead
    // we will add the player as "Player" automatically if beaten and show full list.
    int leftScore = core.state().score_left;
    if (leftScore > high_score) {
        // create a native EDIT control inside the window for robust text input
        int winW = state.width; int winH = state.height;
        int dpi_now = state.dpi;
        if (dpi_now == 96) {
            HMODULE user32_q = LoadLibraryW(L"user32.dll");
            if (user32_q) {
                auto pGetDpiForWindow_q = (UINT(WINAPI*)(HWND))GetProcAddress(user32_q, "GetDpiForWindow");
                if (pGetDpiForWindow_q) dpi_now = pGetDpiForWindow_q(hwnd);
                FreeLibrary(user32_q);
            }
        }
        double ui_scale = (double)dpi_now / 96.0;
        int modalW = max(300, (int)round(400 * ui_scale));
        int modalH = max(120, (int)round(160 * ui_scale));
        int mx = winW/2 - modalW/2; int my = winH/2 - modalH/2;
        int edit_h = max(20, (int)round(24 * ui_scale));
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            mx + (int)round(20*ui_scale), my + (int)round(50*ui_scale), modalW - (int)round(40*ui_scale), edit_h, hwnd, NULL, hInstance, NULL);
        if (edit) {
            // set font
            SendMessageW(edit, WM_SETFONT, (WPARAM)uiFont, TRUE);
            SetFocus(edit);
            // draw modal background once beneath the edit control
            HBRUSH modalBg = CreateSolidBrush(RGB(20,20,30));
            RECT r = {mx, my, mx + modalW, my + modalH};
            FillRect(memDC, &r, modalBg);
            DeleteObject(modalBg);
            SetTextColor(memDC, RGB(240,240,240));
            SetBkMode(memDC, TRANSPARENT);
            DrawTextCentered(memDC, L"New High Score! Enter your name:", mx + (int)round(20*ui_scale), my + (int)round(10*ui_scale));
            DrawTextCentered(memDC, L"Press Enter to confirm", mx + (int)round(20*ui_scale), my + modalH - (int)round(40*ui_scale));
            BitBlt(hdc, 0, 0, winW, winH, memDC, 0, 0, SRCCOPY);

            // message loop until Enter pressed in edit control or window closed
            bool done = false;
            while (!done && state.running) {
                MSG msg;
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
                        // treat Enter
                        done = true; break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // read the text
            int len = GetWindowTextLengthW(edit);
            std::wstring finalName;
            if (len > 0) {
                finalName.resize(len + 1);
                GetWindowTextW(edit, &finalName[0], len + 1);
                finalName.resize(len);
            }
            if (finalName.empty()) finalName = L"Player";
            highList = hsMgr.add_and_get(hsPath, finalName, leftScore, 10);
            DestroyWindow(edit);
        } else {
            // fallback: add default name
            highList = hsMgr.add_and_get(hsPath, L"Player", leftScore, 10);
        }
    }
    return 0;
}
