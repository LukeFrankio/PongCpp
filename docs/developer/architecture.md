# Developer Documentation

## Overview

PongCpp is designed with a clean, modular architecture that separates platform-specific code from core game logic. This document provides technical details for developers who want to understand, modify, or extend the codebase.

## Architecture

### Core Components

1. **GameCore** (`src/core/`): Platform-independent game simulation
2. **Platform Layer** (`src/platform*`): Console I/O abstraction  
3. **Console Frontend** (`src/game.*`, `src/main.cpp`): Text-based interface
4. **Windows GUI Frontend** (`src/win/`): Win32/GDI graphical interface

### Design Principles

- **Platform Independence**: Core game logic works on any platform
- **No External Dependencies**: Uses only standard library and OS APIs
- **Clean Interfaces**: Well-defined boundaries between components
- **Modern C++**: Uses C++17 features appropriately

## Core Game Logic

### GameCore Class

The `GameCore` class implements all game simulation logic:

```cpp
class GameCore {
public:
    void update(double dt);  // Main physics simulation
    void reset();            // Reset game state
    // Player control methods
    void move_left_by(double dy);
    void set_left_y(double y);
    // State access
    const GameState& state() const;
};
```

### Physics Implementation

The physics system uses several advanced techniques:

#### Substepping

- Breaks large timesteps into smaller substeps
- Prevents tunneling at high velocities
- Maintains collision accuracy

#### Ball-Paddle Collision

- Uses geometric collision detection
- Applies realistic physics with velocity transfer
- Includes spin effects based on contact point

#### AI Behavior

- Simple but effective tracking algorithm
- Configurable difficulty via speed multiplier
- Realistic limitations to maintain fun gameplay

## Platform Abstraction

### Platform Interface

```cpp
struct Platform {
    virtual bool kbhit() = 0;
    virtual int getch() = 0;
    virtual void clear_screen() = 0;
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void enable_ansi() = 0;
};
```

### Implementations

- **Windows** (`platform_win.cpp`): Uses `_kbhit()`, `_getch()`, Win32 console API
- **POSIX** (`platform_posix.cpp`): Uses `termios`, `ioctl()`, ANSI escape sequences

## Build System

### CMake Configuration

The project uses CMake with separate targets:

```cmake
# Console version (cross-platform)
add_executable(pong src/main.cpp src/game.cpp ...)

# Windows GUI version (Windows only)
if (WIN32)
    add_executable(pong_win src/win/main_win.cpp ...)
    target_link_libraries(pong_win PRIVATE user32 gdi32)
endif()
```

### Build Process

1. **Configure**: `cmake -S . -B build`
2. **Build**: `cmake --build build --config Release`
3. **Output**: `build/pong` and `build/pong_win.exe` (Windows)

## Windows GUI Implementation

### Win32 Window Management

The Windows GUI uses native Win32 APIs:

- Window class registration and message handling
- DPI awareness for high-resolution displays
- GDI rendering for smooth graphics
- Context menus for configuration

### Key Features

#### DPI Awareness

```cpp
// Enables per-monitor DPI awareness
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

#### Settings Persistence

- JSON format for human readability
- Custom parser to avoid external dependencies
- Automatic loading/saving

#### High Score System

- Persistent storage in JSON format
- Automatic sorting and trimming
- Name entry dialog for new high scores

## Adding New Features

### Adding a New Control Mode

1. Extend `Settings` structure:

```cpp
struct Settings {
    int control_mode = 0;  // Add new mode value
    // ...
};
```

1. Update settings UI and persistence
1. Implement control logic in input processing

### Adding New Platforms

1. Create new platform implementation:

```cpp
// src/platform_newos.cpp
class NewOSPlatform : public Platform {
    // Implement all virtual methods
};
```

1. Update factory function in `platform.h`
1. Add platform detection in build system

### Extending Physics

The physics system is contained in `GameCore::update()`:

1. Modify collision detection algorithms
2. Add new physics parameters to `GameCore` private members
3. Update reset logic for new parameters

## Testing and Debugging

### Console Version Testing

- Easier to debug with text output
- Cross-platform testing
- Log messages to console

### Windows GUI Testing

- Use console version to verify core logic
- Test DPI scaling on different displays
- Verify settings persistence

### Performance Considerations

- Game targets 60 FPS
- Physics substepping maintains accuracy
- GDI rendering is efficient for this simple game

## Code Style and Standards

### C++ Guidelines

- Use C++17 features appropriately
- RAII for resource management
- const-correctness throughout
- Clear, descriptive naming

### Documentation

- Doxygen comments for all public APIs
- Clear function and class descriptions
- Parameter and return value documentation

### Error Handling

- Check return values from OS APIs
- Graceful degradation when possible
- Clear error messages for users

## File Organization

```text
src/
├── core/           # Game logic (platform-independent)
├── platform*       # Platform abstraction implementations  
├── main.cpp        # Console entry point
├── game.*          # Console interface
└── win/            # Windows GUI implementation
    ├── main_win.cpp    # Windows entry point
    ├── game_win.*      # Main GUI implementation
    ├── settings.*      # Settings persistence
    └── highscores.*    # High score management
```

## Memory Management

- Stack allocation for game objects
- RAII for platform resources (console settings, etc.)
- No manual memory management required
- Standard containers for dynamic data

## Threading Model

- Single-threaded design
- All operations on main thread
- Timer-based game loop
- No synchronization required

## Future Enhancement Ideas

### Gameplay Features

- Multiple game modes (different ball speeds, paddle sizes)
- Power-ups and special effects
- Network multiplayer support
- Tournament mode with brackets

### Technical Improvements

- Graphics backend abstraction (OpenGL, DirectX)
- Audio system with sound effects
- Plugin architecture for game modes
- Configuration file format validation

### Platform Support

- macOS support via Cocoa APIs
- Mobile platforms (iOS, Android)
- Web assembly port
- Game controller support

## Building Documentation

### Doxygen Setup

```bash
# Generate documentation
doxygen Doxyfile

# Output in docs/doxygen/html/
```

### Documentation Files

- API documentation generated from source comments
- User guides in `docs/user/`
- Developer documentation in `docs/developer/`

This architecture provides a solid foundation for a cross-platform game while maintaining simplicity and avoiding external dependencies.
