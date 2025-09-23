#pragma once

#include <string>
#include <vector>

struct GameState {
    int gw = 80;
    int gh = 24;
    double left_y = 0.0;
    double right_y = 0.0;
    double ball_x = 0.0;
    double ball_y = 0.0;
    int paddle_h = 5;
    int score_left = 0;
    int score_right = 0;
};

class GameCore {
public:
    GameCore();
    void reset();
    void update(double dt);

    // control: move left paddle (keyboard) or set by mouse y in game coords
    void move_left_by(double dy);
    void set_left_y(double y);

    // manual control of right paddle (for testing)
    void move_right_by(double dy);

    const GameState& state() const { return s; }
    GameState& state() { return s; }

    // AI difficulty multiplier (1.0 normal)
    void set_ai_speed(double m) { ai_speed = m; }

private:
    GameState s;
    double vx, vy;
    double ai_speed = 1.0;
    // previous paddle positions (used to estimate paddle velocity during collisions)
    double prev_left_y = 0.0;
    double prev_right_y = 0.0;
    // physics tuning
    double restitution = 1.03; // slight speed change on hit
    double tangent_strength = 6.0; // how much contact offset affects tangential impulse
    double paddle_influence = 1.5; // how much paddle velocity transfers to ball
};
