/**
 * @file console/game.h
 * @brief Console-based game interface for PongCpp (moved to src/console)
 */
#pragma once

#include "platform/platform.h"
#include "core/game_core.h"

class Game {
public:
    Game(int w, int h, Platform &platform);
    int run();
private:
    void update(GameCore &core, double dt);
    void render(GameCore &core);
    void process_input(GameCore &core);
    int width, height;
    Platform &platform;
    int paddle_h = 5;
    bool running = true;
};
