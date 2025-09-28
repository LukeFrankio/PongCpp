#pragma once
#include <windows.h>
#include "input_state.h"
class InputRouter {
public:
    void new_frame(){ state.advance(); }
    void handle(UINT msg, WPARAM w, LPARAM l);
    const InputState& get() const { return state; }
    InputState& mutable_state() { return state; }
private:
    InputState state;
};
