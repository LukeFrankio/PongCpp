#include "win_window.h"

LRESULT CALLBACK WinStubProc(HWND h, UINT m, WPARAM w, LPARAM l){ return DefWindowProcW(h,m,w,l); }
WinWindow::WinWindow() = default;
WinWindow::~WinWindow(){ destroy(); }
bool WinWindow::create(const WindowParams& p, HINSTANCE hInst){
    WNDCLASSW wc{}; wc.lpfnWndProc=WinStubProc; wc.hInstance=hInst; wc.lpszClassName=L"PongRefWin"; wc.hCursor=LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_ARROW));
    RegisterClassW(&wc);
    hWnd = CreateWindowExW(0, wc.lpszClassName, L"Pong (Refactor)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, p.width, p.height, NULL, NULL, hInst, NULL);
    if(!hWnd) return false; ShowWindow(hWnd, p.showCmd); return true;
}
void WinWindow::destroy(){ if(hWnd){ DestroyWindow(hWnd); hWnd=nullptr; }}
