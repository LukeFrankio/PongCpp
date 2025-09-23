#pragma once

#include "platform.h"
#include "core/game_core.h"

class Game {
public:
    Game(int w, int h, Platform &platform);
    int run(); // run game loop, return exit code
private:
    void update(GameCore &core, double dt);
    void render(GameCore &core);
    void process_input(GameCore &core);

    int width, height;
    Platform &platform;

    // paddles and ball (legacy fields kept for compatibility if needed)
    int paddle_h = 5;

    bool running = true;
};
