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

static const wchar_t CLASS_NAME[] = L"PongWindowClass";

struct WinState {
    int width = 800;
    int height = 600;
    int dpi = 96;
    Game *game = nullptr;
    bool running = true;
    // input state
    bool key_down[256] = {};
    int mouse_x = 0;
    int mouse_y = 0;
    int last_click_x = -1;
    int last_click_y = -1;
    bool mouse_pressed = false;
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
            if (wParam == 'Q') st->running = false;
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
                int baseX = w/2 - 150;
                // compute dpi/ui_scale locally (WindowProc can't access run-time ui_scale)
                int dpi_local = 96;
                HMODULE user32_local = LoadLibraryW(L"user32.dll");
                if (user32_local) {
                    auto pGetDpiForWindow_local = (UINT(WINAPI*)(HWND))GetProcAddress(user32_local, "GetDpiForWindow");
                    if (pGetDpiForWindow_local) dpi_local = pGetDpiForWindow_local(hwnd);
                    FreeLibrary(user32_local);
                }
                double ui_scale_local = (double)dpi_local / 96.0;
                int ys[5] = { (int)(120 * ui_scale_local + 0.5), (int)(180 * ui_scale_local + 0.5), (int)(260 * ui_scale_local + 0.5), (int)(320 * ui_scale_local + 0.5), (int)(380 * ui_scale_local + 0.5) };
                for (int i=0;i<5;i++) {
                    int pad = (int)max(6.0, 10.0 * ui_scale_local);
                    int wbox = (int)max(260.0, 260.0 * ui_scale_local);
                    RECT rb = { baseX - pad, ys[i] - (int)(6*ui_scale_local + 0.5), baseX + wbox, ys[i] + (int)(34*ui_scale_local + 0.5) };
                    if (mx >= rb.left && mx <= rb.right && my >= rb.top && my <= rb.bottom) {
                        st->menu_click_index = i;
                        break;
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

    bool settings_changed = false;
    std::wstring hsPath = exeDir + L"highscores.json";
    auto highList = hsMgr.load(hsPath, 10);
    int high_score = (highList.empty()) ? 0 : highList.front().score;

    // Improved config menu: keyboard navigation via state.key_down and simple selection
    bool inMenu = true;
    int menuIndex = 0; // 0: control, 1: ai, 2: start, 3: manage highscores, 4: quit
    auto clamp_menu = [&](int &v, int lo, int hi){ if (v<lo) v=lo; if (v>hi) v=hi; };
    // prepare a reusable modal function (declared here so both keyboard and mouse paths can call it)
    auto manageHighScoresModal = [&]() {
        state.ui_mode = 2;
        bool manage = true; int sel = 0;
        while (manage && state.running) {
            // Pump messages so WindowProc updates state.last_click_*/mouse_* etc.
            MSG msg2;
            while (PeekMessage(&msg2, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg2); DispatchMessage(&msg2); }

            int w_now = state.width; int h_now = state.height;
            int dpi_now2 = state.dpi;
            if (dpi_now2 == 96) {
                HMODULE user32_q = LoadLibraryW(L"user32.dll");
                if (user32_q) {
                    auto pGetDpiForWindow_q = (UINT(WINAPI*)(HWND))GetProcAddress(user32_q, "GetDpiForWindow");
                    if (pGetDpiForWindow_q) dpi_now2 = pGetDpiForWindow_q(hwnd);
                    FreeLibrary(user32_q);
                }
            }
            double uisc = (double)dpi_now2 / 96.0;

            // draw modal background and list
            RECT bg = {0,0,w_now,h_now}; HBRUSH b = CreateSolidBrush(RGB(20,20,30)); FillRect(memDC, &bg, b); DeleteObject(b);
            SetTextColor(memDC, RGB(220,220,220)); SetBkMode(memDC, TRANSPARENT);
            DrawTextCentered(memDC, L"Manage High Scores", w_now/2, (int)round(30*uisc));

            // layout rows
            int rowStartY = (int)round(80*uisc);
            int rowH = max(20, (int)round(28 * uisc));
            for (size_t i=0;i<highList.size();++i) {
                int y = rowStartY + (int)round(i * (rowH + (int)round(4 * uisc)));
                // highlight selected
                if ((int)i == sel) {
                    int pad = (int)round(6 * uisc);
                    int wbox = (int)round(500 * uisc);
                    RECT rb = { w_now/2 - wbox/2 - pad, y - pad, w_now/2 + wbox/2 + pad, y + rowH + pad };
                    HBRUSH selb = CreateSolidBrush(RGB(60,60,90)); FillRect(memDC, &rb, selb); DeleteObject(selb);
                    SetTextColor(memDC, RGB(255,255,200));
                } else {
                    SetTextColor(memDC, RGB(200,200,200));
                }
                std::wstring line = std::to_wstring(i+1) + L"  " + highList[i].name + L"  " + std::to_wstring(highList[i].score);
                DrawTextCentered(memDC, line, w_now/2, y + rowH/2);
            }

            // draw buttons: Delete Selected, Clear All, Back
            int btnW = (int)round(140 * uisc);
            int btnH = (int)round(36 * uisc);
            int btnY = h_now - (int)round(80 * uisc);
            int gap = (int)round(20 * uisc);
            RECT btnDel = { w_now/2 - btnW - gap/2, btnY, w_now/2 - gap/2, btnY + btnH };
            RECT btnClear = { w_now/2 + gap/2, btnY, w_now/2 + btnW + gap/2, btnY + btnH };
            RECT btnBack = { w_now/2 - btnW/2, h_now - (int)round(40 * uisc) - btnH/2, w_now/2 + btnW/2, h_now - (int)round(40 * uisc) + btnH/2 };

            // Delete button (disabled when no selection)
            HBRUSH btnBg = CreateSolidBrush(RGB(100,40,40));
            HBRUSH btnBgDisabled = CreateSolidBrush(RGB(60,60,60));
            if (highList.empty()) FillRect(memDC, &btnDel, btnBgDisabled); else FillRect(memDC, &btnDel, btnBg);
            FillRect(memDC, &btnClear, btnBg);
            FillRect(memDC, &btnBack, btnBgDisabled);
            DeleteObject(btnBg); DeleteObject(btnBgDisabled);

            SetTextColor(memDC, RGB(240,240,240));
            DrawTextCentered(memDC, L"Delete Selected", (btnDel.left + btnDel.right)/2, btnDel.top + btnH/2);
            DrawTextCentered(memDC, L"Clear All", (btnClear.left + btnClear.right)/2, btnClear.top + btnH/2);
            DrawTextCentered(memDC, L"Back", (btnBack.left + btnBack.right)/2, (btnBack.top + btnBack.bottom)/2);

            // present
            BitBlt(hdc, 0, 0, w_now, h_now, memDC, 0, 0, SRCCOPY);

            // handle keyboard: ESC/Back
            if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; manage = false; break; }

            // handle click-on-release via state.last_click_x/last_click_y
            if (state.last_click_x != -1) {
                int cx = state.last_click_x; int cy = state.last_click_y;
                // reset so other loops don't see it
                state.last_click_x = -1; state.last_click_y = -1;

                // check rows hit
                bool handled = false;
                for (size_t i=0;i<highList.size();++i) {
                    int y = rowStartY + (int)round(i * (rowH + 4) * uisc);
                    int pad = (int)round(6 * uisc);
                    int wbox = (int)round(500 * uisc);
                    RECT rb = { w_now/2 - wbox/2 - pad, y - pad, w_now/2 + wbox/2 + pad, y + rowH + pad };
                    if (cx >= rb.left && cx <= rb.right && cy >= rb.top && cy <= rb.bottom) {
                        sel = (int)i; handled = true; break;
                    }
                }
                if (handled) continue; // consumed

                // check Delete Selected
                if (!highList.empty() && cx >= btnDel.left && cx <= btnDel.right && cy >= btnDel.top && cy <= btnDel.bottom) {
                    // delete selected entry
                    // delete selected entry
                    if (sel >= 0 && sel < (int)highList.size()) {
                        highList.erase(highList.begin() + sel);
                        if (sel >= (int)highList.size()) sel = (int)highList.size() - 1;
                    }
                    continue;
                }

                // check Clear All -> show confirmation overlay
                if (cx >= btnClear.left && cx <= btnClear.right && cy >= btnClear.top && cy <= btnClear.bottom) {
                    // confirmation loop
                    // confirmation loop
                    bool confirm = true; bool doClear = false;
                    while (confirm && state.running) {
                        MSG msg3;
                        while (PeekMessage(&msg3, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg3); DispatchMessage(&msg3); }
                        // redraw same base modal but with overlay
                        RECT bg2 = {0,0,w_now,h_now}; HBRUSH b2 = CreateSolidBrush(RGB(10,10,10)); FillRect(memDC, &bg2, b2); DeleteObject(b2);
                        SetTextColor(memDC, RGB(240,240,240)); SetBkMode(memDC, TRANSPARENT);
                        DrawTextCentered(memDC, L"Confirm Clear All?", w_now/2, h_now/2 - (int)round(20*uisc));

                        // draw yes/no buttons
                        int yb = h_now/2 + (int)round(10*uisc);
                        RECT rYes = { w_now/2 - btnW - gap, yb, w_now/2 - gap, yb + btnH };
                        RECT rNo = { w_now/2 + gap, yb, w_now/2 + btnW + gap, yb + btnH };
                        HBRUSH bYes = CreateSolidBrush(RGB(60,120,60)); HBRUSH bNo = CreateSolidBrush(RGB(120,60,60));
                        FillRect(memDC, &rYes, bYes); FillRect(memDC, &rNo, bNo);
                        DeleteObject(bYes); DeleteObject(bNo);
                        DrawTextCentered(memDC, L"Yes", (rYes.left + rYes.right)/2, rYes.top + btnH/2);
                        DrawTextCentered(memDC, L"No", (rNo.left + rNo.right)/2, rNo.top + btnH/2);

                        BitBlt(hdc, 0, 0, w_now, h_now, memDC, 0, 0, SRCCOPY);

                        if (state.last_click_x != -1) {
                            int ccx = state.last_click_x; int ccy = state.last_click_y;
                            state.last_click_x = -1; state.last_click_y = -1;
                            if (ccx >= rYes.left && ccx <= rYes.right && ccy >= rYes.top && ccy <= rYes.bottom) { doClear = true; confirm = false; break; }
                            if (ccx >= rNo.left && ccx <= rNo.right && ccy >= rNo.top && ccy <= rNo.bottom) { doClear = false; confirm = false; break; }
                        }
                        if (state.key_down[VK_RETURN]) { state.key_down[VK_RETURN]=false; doClear = true; confirm = false; break; }
                        if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; doClear = false; confirm = false; break; }
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                    if (doClear) {
                        highList.clear(); sel = 0;
                    }
                    continue;
                }

                // check Back
                if (cx >= btnBack.left && cx <= btnBack.right && cy >= btnBack.top && cy <= btnBack.bottom) {
                    manage = false; continue;
                }
            }

            // ...existing code...

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        state.ui_mode = 1;
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
            int baseX = winW/2 - (int)round(150*ui_scale);
            int ys[5] = { (int)(120 * ui_scale + 0.5), (int)(180 * ui_scale + 0.5), (int)(260 * ui_scale + 0.5), (int)(320 * ui_scale + 0.5), (int)(380 * ui_scale + 0.5) };
            for (int i=0;i<5;i++) {
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

        drawOption(0, (ctrl==CTRL_KEYBOARD)?L"Control: Keyboard":L"Control: Mouse (follow Y)", winW/2 - (int)round(150*ui_scale), (int)round(120*ui_scale));
        drawOption(1, (ai==AI_EASY)?L"AI: Easy":(ai==AI_NORMAL)?L"AI: Normal":L"AI: Hard", winW/2 - (int)round(150*ui_scale), (int)round(180*ui_scale));
        drawOption(2, L"Start Game", winW/2 - (int)round(150*ui_scale), (int)round(260*ui_scale));
        drawOption(3, L"Manage High Scores", winW/2 - (int)round(150*ui_scale), (int)round(320*ui_scale));
        drawOption(4, L"Quit", winW/2 - (int)round(150*ui_scale), (int)round(380*ui_scale));

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
        if (state.key_down[VK_DOWN]) { menuIndex++; clamp_menu(menuIndex, 0, 4); state.key_down[VK_DOWN]=false; }
        if (state.key_down[VK_UP]) { menuIndex--; clamp_menu(menuIndex, 0, 4); state.key_down[VK_UP]=false; }
        if (state.key_down[VK_LEFT]) {
            if (menuIndex==0) { ctrl = CTRL_KEYBOARD; settings.control_mode = 0; settings_changed = true; }
            else if (menuIndex==1) { if (ai>0) { ai=(AIDifficulty)(ai-1); settings.ai = ai; settings_changed = true; } }
            state.key_down[VK_LEFT]=false;
        }
        if (state.key_down[VK_RIGHT]) {
            if (menuIndex==0) { ctrl = CTRL_MOUSE; settings.control_mode = 1; settings_changed = true; }
            else if (menuIndex==1) { if (ai<2) { ai=(AIDifficulty)(ai+1); settings.ai = ai; settings_changed = true; } }
            state.key_down[VK_RIGHT]=false;
        }
        if (state.key_down[VK_RETURN]) {
            state.key_down[VK_RETURN]=false;
            if (menuIndex==2) { inMenu = false; state.ui_mode = 0; }
            else if (menuIndex==3) { manageHighScoresModal(); hsMgr.save(hsPath, highList); }
            else if (menuIndex==4) { state.running = false; break; }
        }
        if (state.key_down[VK_ESCAPE]) { state.key_down[VK_ESCAPE]=false; state.running=false; break; }

        // process mouse clicks (menu_click_index)
        if (state.menu_click_index != -1) {
            int clicked = state.menu_click_index;
            state.menu_click_index = -1;
            if (clicked == 0) { // control option toggles
                ctrl = (ctrl==CTRL_KEYBOARD) ? CTRL_MOUSE : CTRL_KEYBOARD;
                settings.control_mode = (ctrl==CTRL_MOUSE)?1:0; settings_changed = true;
            } else if (clicked == 1) { // cycle AI
                ai = (AIDifficulty)((ai + 1) % 3);
                settings.ai = ai; settings_changed = true;
            } else if (clicked == 2) { inMenu = false; }
            else if (clicked == 3) { manageHighScoresModal(); hsMgr.save(hsPath, highList); }
            else if (clicked == 4) { state.running = false; break; }
        }

    // leaving menu; ensure UI mode returns to gameplay
    state.ui_mode = 0;

        if (settings_changed) {
            settingsMgr.save(exeDir + L"settings.json", settings);
            settings_changed = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    while (state.running) {
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

        HBRUSH bg = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RECT r = {0,0,winW,winH};
        FillRect(memDC, &r, bg);

    // draw center dashed line with glow (two passes)
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

        // draw paddles and ball as simple rectangles/circles using normalized coordinates
        // We'll map game coordinates (width=80,height=24) to window size
    const int gw = 80, gh = 24;
    auto mapX = [&](double gx){ return (int)(gx / gw * winW); };
    auto mapY = [&](double gy){ return (int)(gy / gh * winH); };

    GameState &gs = core.state();

    // input: use window state instead of polling
        if (ctrl == CTRL_KEYBOARD) {
            if (state.key_down['W']) core.move_left_by(-120.0 * dt);
            if (state.key_down['S']) core.move_left_by(120.0 * dt);
        } else {
            double my = (double)state.mouse_y / winH * gs.gh;
            core.set_left_y(my - gs.paddle_h/2.0);
        }
        if (state.key_down[VK_UP]) core.move_right_by(-120.0 * dt);
        if (state.key_down[VK_DOWN]) core.move_right_by(120.0 * dt);

    // core.update() enforces clamping; nothing to do here

        // set AI speed into core then update
        if (ai == AI_EASY) core.set_ai_speed(0.6);
        else if (ai == AI_NORMAL) core.set_ai_speed(1.0);
        else core.set_ai_speed(1.6);
        core.update(dt);

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
