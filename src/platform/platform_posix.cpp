/** platform/platform_posix.cpp - moved */
#include "platform/platform.h"
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
class PosixPlatform : public Platform {
public:
    PosixPlatform(){ enable_ansi(); orig={}; tcgetattr(STDIN_FILENO,&orig); term=orig; term.c_lflag &= ~(ICANON|ECHO); tcsetattr(STDIN_FILENO,TCSANOW,&term);}~PosixPlatform()override{tcsetattr(STDIN_FILENO,TCSANOW,&orig);set_cursor_visible(true);}bool kbhit() override {int bytes=0; ioctl(STDIN_FILENO,FIONREAD,&bytes); return bytes>0;}int getch() override {char c=0; if(read(STDIN_FILENO,&c,1)<=0) return -1; return (int)c;}void clear_screen() override {std::cout<<"\x1b[2J\x1b[H";}void set_cursor_visible(bool v) override { if(v) std::cout<<"\x1b[?25h"; else std::cout<<"\x1b[?25l";}void enable_ansi() override {}
private: struct termios orig; struct termios term;};
std::unique_ptr<Platform> createPlatform(){ return std::make_unique<PosixPlatform>(); }
#endif
