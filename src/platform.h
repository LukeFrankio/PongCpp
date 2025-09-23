#pragma once

#include <memory>

struct Platform {
    virtual ~Platform() = default;
    virtual bool kbhit() = 0;         // whether a key has been pressed
    virtual int getch() = 0;          // get a character (non-blocking if kbhit used)
    virtual void clear_screen() = 0;  // clear and move cursor home
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void enable_ansi() = 0;
};

std::unique_ptr<Platform> createPlatform();
