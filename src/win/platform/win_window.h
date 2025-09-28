#pragma once
#include <windows.h>
struct WindowParams { int width=800; int height=600; int showCmd=SW_SHOW; };
class WinWindow {
public:
    WinWindow();
    ~WinWindow();
    bool create(const WindowParams& p, HINSTANCE hInst);
    void destroy();
    HWND hwnd() const { return hWnd; }
private:
    HWND hWnd = nullptr;
};
