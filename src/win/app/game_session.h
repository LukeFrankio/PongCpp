#pragma once
class GameCore;
class GameSession {
public:
    GameSession();
    ~GameSession();
    void update(double dt);
    GameCore& core();
private:
    GameCore* corePtr = nullptr; // allocated later
};
