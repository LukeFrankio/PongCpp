/**
 * @file platform/platform.h
 * @brief Platform abstraction layer for console I/O operations (moved to src/platform)
 */
#pragma once
#include <memory>

struct Platform {
    virtual ~Platform() = default;
    virtual bool kbhit() = 0;
    virtual int getch() = 0;
    virtual void clear_screen() = 0;
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void enable_ansi() = 0;
};

std::unique_ptr<Platform> createPlatform();
