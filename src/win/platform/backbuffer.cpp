#include "backbuffer.h"
BackBuffer::~BackBuffer(){ if(memDC){ SelectObject(memDC, oldBmp); DeleteObject(bmp); DeleteDC(memDC);} }
void BackBuffer::resize(HDC screen, int w, int h){ if(memDC){ SelectObject(memDC, oldBmp); DeleteObject(bmp); DeleteDC(memDC);} W=w;H=h; memDC=CreateCompatibleDC(screen); bmp=CreateCompatibleBitmap(screen,W,H); oldBmp=(HBITMAP)SelectObject(memDC,bmp); }
