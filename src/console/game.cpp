/**
 * @file console/game.cpp
 * @brief Implementation of console-based Pong game interface (moved to src/console)
 */

#include "console/game.h"
#include "core/game_core.h"
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>

Game::Game(int w, int h, Platform &platform)
: width(w), height(h), platform(platform) {}

void Game::process_input(GameCore &core) {
    while (platform.kbhit()) {
        int c = platform.getch();
        if (c <= 0) break;
        if (c == 'q' || c == 'Q') { running = false; }
        if (c == 'w' || c == 'W') { core.move_left_by(-1.5); }
        if (c == 's' || c == 'S') { core.move_left_by(1.5); }
    if (c == '1') { core.set_mode(GameMode::Classic); }
    if (c == '2') { core.set_mode(GameMode::ThreeEnemies); }
    if (c == '3') { core.set_mode(GameMode::Obstacles); }
    if (c == '4') { core.set_mode(GameMode::MultiBall); }
        if (c == 0x1B) { // ESC seq
            if (!platform.kbhit()) continue;
            int b1 = platform.getch();
            if (b1 == '[') {
                if (!platform.kbhit()) continue;
                int b2 = platform.getch();
                if (b2 == 'A') { core.move_right_by(-1.5); }
                if (b2 == 'B') { core.move_right_by(1.5); }
            }
        }
        if (c == 0 || c == 0xE0) { // Windows arrow prefix
            if (!platform.kbhit()) continue;
            int code = platform.getch();
            if (code == 72) { core.move_right_by(-1.5); }
            if (code == 80) { core.move_right_by(1.5); }
        }
    }
}

void Game::update(GameCore &core, double dt) { core.update(dt); }

void Game::render(GameCore &core) {
    platform.set_cursor_visible(false);
    std::string out;
    out.reserve((width + 1) * (height + 1) + 128);
    out += "\x1b[H"; // cursor home
    const GameState &gs = core.state();
    int gw = gs.gw, gh = gs.gh;
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < width; ++x) {
            char ch = ' ';
            if (x == width / 2 && (y % 2 == 0)) ch = '|';
            // draw balls (multi-ball aware)
            int cx = (int)std::round(gs.ball_x);
            int cy = (int)std::round(gs.ball_y);
            int ly0 = (int)std::round(gs.left_y);
            if (x == 1 && y >= ly0 && y < ly0 + gs.paddle_h) ch = '|';
            int ry0 = (int)std::round(gs.right_y);
            if (x == gw - 2 && y >= ry0 && y < ry0 + gs.paddle_h) ch = '|';
            // obstacles
            if (gs.mode == GameMode::Obstacles) {
                for (auto &ob : gs.obstacles) {
                    int left = (int)std::round(ob.x - ob.w/2.0);
                    int right = (int)std::round(ob.x + ob.w/2.0);
                    int top = (int)std::round(ob.y - ob.h/2.0);
                    int bottom = (int)std::round(ob.y + ob.h/2.0);
                    if (x >= left && x <= right && y >= top && y <= bottom) ch = '#';
                }
            }
            if (gs.mode == GameMode::ThreeEnemies) {
                int halfW = gs.paddle_w/2;
                int top_y = 1; int bottom_y = gh - 2;
                int top_l = (int)std::round(gs.top_x) - halfW;
                int top_r = (int)std::round(gs.top_x) + halfW;
                int bottom_l = (int)std::round(gs.bottom_x) - halfW;
                int bottom_r = (int)std::round(gs.bottom_x) + halfW;
                if (y == top_y && x >= top_l && x <= top_r) ch = '=';
                if (y == bottom_y && x >= bottom_l && x <= bottom_r) ch = '=';
            }
            // multi-balls
            if (!gs.balls.empty()) {
                for (size_t bi=0; bi<gs.balls.size(); ++bi) {
                    int bx = (int)std::round(gs.balls[bi].x);
                    int by = (int)std::round(gs.balls[bi].y);
                    if (x == bx && y == by) ch = (bi==0?'O':'o');
                }
            } else {
                if (x == cx && y == cy) ch = 'O';
            }
            out.push_back(ch);
        }
        out.push_back('\n');
    }
    const GameState &gs2 = core.state();
    std::string score = std::to_string(gs2.score_left) + " - " + std::to_string(gs2.score_right);
    out += score + "\n";
    std::string modeName;
    switch (gs.mode) {
        case GameMode::Classic: modeName = "Classic"; break;
        case GameMode::ThreeEnemies: modeName = "3 Enemies"; break;
        case GameMode::Obstacles: modeName = "Obstacles"; break;
        case GameMode::MultiBall: modeName = "MultiBall"; break;
    }
    out += "Mode: " + modeName + " | 1=Classic 2=3Enemies 3=Obstacles 4=MultiBall\n";
    out += "Controls: W/S, Arrow keys (right paddle), Q quit\n";
    std::cout << out << std::flush;
}

int Game::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    const double target_dt = 1.0/60.0;
    GameCore core;
    core.state().gw = width; core.state().gh = height; core.state().paddle_h = paddle_h;
    while (running) {
        auto now = clock::now();
        std::chrono::duration<double> elapsed = now - last;
        double dt = elapsed.count();
        if (dt < target_dt) { std::this_thread::sleep_for(std::chrono::duration<double>(target_dt - dt)); continue; }
        last = now;
        process_input(core);
        update(core, dt);
        render(core);
    }
    platform.set_cursor_visible(true);
    return 0;
}
