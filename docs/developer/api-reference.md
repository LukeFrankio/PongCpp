# API Reference

This document provides a quick reference for the main classes and functions in PongCpp.

## Core Classes

### GameCore

The main game simulation engine.

```cpp
#include "core/game_core.h"

class GameCore {
public:
    GameCore();
    void reset();
    void update(double dt);
    
    // Player control
    void move_left_by(double dy);
    void set_left_y(double y);
    void move_right_by(double dy);  // For testing
    
    // State access
    const GameState& state() const;
    GameState& state();
    
    // Configuration
    void set_ai_speed(double multiplier);
};
```

**Key Methods:**

- `update(dt)`: Advance simulation by dt seconds
- `reset()`: Reset to initial game state  
- `move_left_by(dy)`: Move player paddle relatively
- `set_left_y(y)`: Set player paddle absolute position

### GameState

Contains all dynamic game data.

```cpp
struct GameState {
    int gw, gh;           // Game dimensions
    double left_y, right_y;  // Paddle positions (center)
    double ball_x, ball_y;   // Ball position
    int paddle_h;         // Paddle height
    int score_left, score_right;  // Current scores
};
```

### Platform Interface

Abstraction for console I/O operations.

```cpp
#include "platform.h"

struct Platform {
    virtual bool kbhit() = 0;
    virtual int getch() = 0;
    virtual void clear_screen() = 0;
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void enable_ansi() = 0;
};

// Factory function
std::unique_ptr<Platform> createPlatform();
```

## Console Interface

### Game Class

Main class for console version.

```cpp
#include "game.h"

class Game {
public:
    Game(int width, int height, Platform &platform);
    int run();  // Returns exit code
};
```

**Usage:**

```cpp
auto platform = createPlatform();
Game game(80, 24, *platform);
int result = game.run();
```

## Windows GUI Interface

### Main Entry Point

```cpp
#include "win/game_win.h"

int run_win_pong(HINSTANCE hInstance, int nCmdShow);
```

### Settings Management  

```cpp
#include "win/settings.h"

struct Settings {
    int control_mode;  // 0=keyboard, 1=mouse
    int ai;           // 0=easy, 1=normal, 2=hard
};

class SettingsManager {
public:
    Settings load(const std::wstring &path);
    bool save(const std::wstring &path, const Settings &settings);
};
```

### High Score Management

```cpp
#include "win/highscores.h"

struct HighScoreEntry {
    std::wstring name;
    int score;
};

class HighScores {
public:
    std::vector<HighScoreEntry> load(const std::wstring &path, size_t maxEntries = 10);
    bool save(const std::wstring &path, const std::vector<HighScoreEntry> &list);
    std::vector<HighScoreEntry> add_and_get(const std::wstring &path, 
                                            const std::wstring &name, 
                                            int score, 
                                            size_t maxEntries = 10);
};
```

## Build Targets

### CMake Targets

```bash
# Console version (cross-platform)
cmake --build . --target pong

# Windows GUI version  
cmake --build . --target pong_win

# Generate documentation
cmake -DBUILD_DOCUMENTATION=ON ..
cmake --build . --target docs

# Clean documentation
cmake --build . --target clean-docs
```

### Build Options

```cmake
# Enable documentation generation
cmake -DBUILD_DOCUMENTATION=ON ..

# Set build type
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

## Constants and Configuration

### Physics Parameters

Located in `GameCore` private members:

```cpp
double restitution = 1.03;      // Energy change on paddle hit
double tangent_strength = 6.0;  // Contact offset effect on spin
double paddle_influence = 1.5;  // Paddle velocity transfer
```

### Game Dimensions

Default console size: 80x24 characters
Default paddle height: 5 units
Ball radius: 0.6 units

### Control Keys (Console)

- W/S: Move paddle up/down
- Q: Quit game
- Arrow keys: Test right paddle

### Control Keys (Windows GUI)

**Keyboard Mode:**

- W/S: Move paddle up/down
- ESC: Exit
- Right-click: Settings menu

**Mouse Mode:**

- Mouse movement: Control paddle
- ESC: Exit  
- Right-click: Settings menu

## File Formats

### Settings JSON

```json
{
  "control_mode": 0,
  "ai": 1
}
```

### High Scores JSON

```json
[
  {"name": "Player1", "score": 15},
  {"name": "Player2", "score": 12}
]
```

## Error Handling

### Return Codes

- 0: Success
- 1: Initialization error (console version)
- Non-zero: Windows error code (GUI version)

### Common Errors

- Platform creation failure
- File I/O errors for settings/scores
- Window creation failure (GUI)

## Performance Notes

- Target frame rate: 60 FPS
- Physics substep rate: 240 Hz
- Memory usage: <10MB typical
- CPU usage: <5% on modern systems

## Thread Safety

**Single-threaded design**: All components run on the main thread.
No synchronization required.

## Platform-Specific Notes

### Windows

- Requires user32.dll and gdi32.dll
- DPI awareness enabled automatically
- Settings stored next to executable

### POSIX/Linux

- Requires termios support
- ANSI escape sequence support
- Console version only

### File Locations

Settings and scores are stored in the same directory as the executable:

- `settings.json`
- `highscores.json`

## Debugging Tips

1. Use console version for debugging core logic
2. Add debug output to see game state
3. Check return values from file operations
4. Use debugger breakpoints in update loop

## Extension Points

### Adding New Features

1. **New Game Modes**: Extend GameCore with mode parameter
2. **New Platforms**: Implement Platform interface
3. **New Controls**: Add input handling to Game classes
4. **Visual Effects**: Extend rendering in respective frontends

### Modifying Physics

All physics logic is in `GameCore::update()`. Key areas:

- Ball movement and collision
- Paddle-ball interaction
- AI behavior
- Scoring logic

This API reference covers the essential interfaces for understanding and extending PongCpp.
