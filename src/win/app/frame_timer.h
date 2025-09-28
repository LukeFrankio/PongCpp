#pragma once
#include <chrono>
class FrameTimer {
public:
    FrameTimer();
    double tick(); // returns delta seconds
private:
    using clock = std::chrono::steady_clock;
    clock::time_point last;
};
