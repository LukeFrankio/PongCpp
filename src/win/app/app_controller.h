#pragma once
#include <memory>
struct UIState;
struct InputState;
struct Settings;
class GameSession;
class RenderDispatch;
class SettingsStore;
class HighScoresStore;

class AppController {
public:
    AppController();
    ~AppController();
    void initialize();
    void tick(double dt);
    void render();
    void on_input(const InputState& in);
};
