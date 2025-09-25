/**
 * @file game.cpp
 * @brief Implementation of console-based Pong game interface
 * 
 * This file implements the Game class methods for running a text-based
 * version of Pong in a console/terminal window with ASCII graphics.
 */

#include "game.h"
#include "core/game_core.h"
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>

Game::Game(int w, int h, Platform &platform)
: width(w), height(h), platform(platform) {
    // Store dimensions and platform reference
    // GameCore instance will be created in run() for deterministic initialization
}

void Game::process_input(GameCore &core) {
    while (platform.kbhit()) {
        int c = platform.getch();
        if (c <= 0) break;
        if (c == 'q' || c == 'Q') { running = false; }
        if (c == 'w' || c == 'W') { core.move_left_by(-1.5); }
        if (c == 's' || c == 'S') { core.move_left_by(1.5); }
        // simple arrow support for right paddle
        if (c == 0x1B) {
            if (!platform.kbhit()) continue;
            int b1 = platform.getch();
            if (b1 == '[') {
                if (!platform.kbhit()) continue;
                int b2 = platform.getch();
                if (b2 == 'A') { core.move_right_by(-1.5); }
                if (b2 == 'B') { core.move_right_by(1.5); }
            }
        }
        if (c == 0 || c == 0xE0) {
            if (!platform.kbhit()) continue;
            int code = platform.getch();
            if (code == 72) { core.move_right_by(-1.5); }
            if (code == 80) { core.move_right_by(1.5); }
        }
    }
}

void Game::update(GameCore &core, double dt) {
    // map keyboard/mouse updates to core handled in process_input
    core.update(dt);
}

void Game::render(GameCore &core) {
    // Build one large string containing the entire frame and write it in a single
    // operation to reduce terminal flicker.
    platform.set_cursor_visible(false);
    std::string out;
    out.reserve((width + 1) * (height + 1) + 128);

    // move cursor to home and don't clear; we'll overwrite the full region
    out += "\x1b[H";

    // draw each row into out directly
    const GameState &gs = core.state();
    int gw = gs.gw, gh = gs.gh;
    // draw each row into out directly
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < width; ++x) {
            char ch = ' ';
            // middle line
            if (x == width / 2 && (y % 2 == 0)) ch = '|';

            // map core coordinates to terminal
            int cx = (int)std::round(gs.ball_x);
            int cy = (int)std::round(gs.ball_y);
            int ly0 = (int)std::round(gs.left_y);
            if (x == 1 && y >= ly0 && y < ly0 + gs.paddle_h) ch = '|';
            int ry0 = (int)std::round(gs.right_y);
            if (x == gw - 2 && y >= ry0 && y < ry0 + gs.paddle_h) ch = '|';
            if (x == cx && y == cy) ch = 'O';

            out.push_back(ch);
        }
        out.push_back('\n');
    }

    // score line and controls appended after the buffer region
    const GameState &gs2 = core.state();
    std::string score = std::to_string(gs2.score_left) + " - " + std::to_string(gs2.score_right);
    out += score + "\n";
    out += "Controls: W/S, Arrow keys, Q to quit\n";

    // finally write once
    std::cout << out << std::flush;
}

int Game::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    const double target_dt = 1.0/60.0;
    GameCore core;
    // set core grid to terminal size
    core.state().gw = width; core.state().gh = height; core.state().paddle_h = paddle_h;
    while (running) {
        auto now = clock::now();
        std::chrono::duration<double> elapsed = now - last;
        double dt = elapsed.count();
        if (dt < target_dt) {
            std::this_thread::sleep_for(std::chrono::duration<double>(target_dt - dt));
            continue;
        }
        last = now;
        process_input(core);
        update(core, dt);
        render(core);
    }
    platform.set_cursor_visible(true);
    return 0;
}
