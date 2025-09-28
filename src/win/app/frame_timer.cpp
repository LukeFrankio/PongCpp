#include "frame_timer.h"
FrameTimer::FrameTimer() : last(clock::now()) {}
double FrameTimer::tick(){ auto now = clock::now(); std::chrono::duration<double> d = now-last; last=now; return d.count(); }
