#pragma once
#include <windows.h>
class BackBuffer {
public:
    BackBuffer() = default;
    ~BackBuffer();
    void resize(HDC screen, int w, int h);
    HDC dc() const { return memDC; }
private:
    HDC memDC=nullptr; HBITMAP bmp=nullptr; HBITMAP oldBmp=nullptr; int W=0,H=0;
};
