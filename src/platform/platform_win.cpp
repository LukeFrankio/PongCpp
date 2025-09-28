/** platform/platform_win.cpp - moved */
#include "platform/platform.h"
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <iostream>
class WinPlatform : public Platform { public: WinPlatform(){enable_ansi();} ~WinPlatform() override { set_cursor_visible(true);} bool kbhit() override {return _kbhit();} int getch() override {return _getch();} void clear_screen() override { std::cout << "\x1b[2J\x1b[H"; } void set_cursor_visible(bool vis) override { HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); if(h==INVALID_HANDLE_VALUE) return; CONSOLE_CURSOR_INFO info; if(!GetConsoleCursorInfo(h,&info)) return; info.bVisible = vis?TRUE:FALSE; SetConsoleCursorInfo(h,&info);} void enable_ansi() override { HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); if(h==INVALID_HANDLE_VALUE) return; DWORD mode=0; if(!GetConsoleMode(h,&mode)) return; mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING; SetConsoleMode(h,mode);} };
std::unique_ptr<Platform> createPlatform(){ return std::make_unique<WinPlatform>(); }
#endif
