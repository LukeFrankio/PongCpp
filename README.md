# PongCpp

A classic Pong game implementation in C++ with dual frontend support: console and Windows GUI versions.

## Features

### Console Version (Cross-platform)

- Text-based ASCII rendering
- Cross-platform support (Windows/Linux/POSIX)
- Keyboard controls (W/S keys and arrow keys)
- Simple AI opponent

### Windows GUI Version

- Native Win32/GDI windowed interface
- DPI awareness for high-resolution displays
- Multiple control modes:
  - **Keyboard**: W/S keys for paddle control
  - **Mouse**: Click and drag or move mouse to control paddle
- Configurable AI difficulty (Easy/Normal/Hard)
- High score persistence with player names
- Settings persistence (control mode, AI difficulty)
- Real-time physics with ball spin and paddle velocity transfer

## Technical Details

- **Language**: C++17
- **Build System**: CMake 3.10+ (prefers 3.20+)
- **Dependencies**: None (uses only standard library and platform APIs)
- **Platforms**: Windows (full support), Linux/POSIX (console only)
- **Architecture**: Shared game core with platform-specific frontends

## Build Instructions

### Prerequisites

**Windows:**

- Visual Studio with C++ support OR CMake + C++ toolchain
- CMake 3.20+ (recommended)

**Linux/POSIX:**

- g++ or clang++ with C++17 support
- CMake 3.10+

### Building on Windows

The easiest way to build on Windows is using the provided batch script:

```powershell
# Clean build (removes build directory)
.\build.bat clean

# Release build (default - builds both console and GUI versions)
.\build.bat

# Debug build
.\build.bat Debug

# Force specific Visual Studio generator
.\build.bat Release "Visual Studio 17 2022"
```

**Build outputs:**

- GUI version: `build\Release\pong_win.exe`
- Console version: `build\Release\pong.exe`

### Building on Linux/Cross-platform

Console version only (GUI version requires Win32 APIs):

```bash
# Clean
rm -rf build

# Configure and build Release
mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# Configure and build Debug
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

**Build output:** `build/pong` (console version)

## Controls

### Console Version

- **W/S**: Move left paddle up/down
- **Arrow Keys**: Alternative paddle controls
- **Q**: Quit game

### GUI Version

- **Keyboard Mode**:
  - **W/S**: Move paddle up/down
  - **ESC**: Exit game
  - **Right-click**: Open configuration menu

- **Mouse Mode**:
  - **Mouse Movement**: Control paddle position
  - **ESC**: Exit game
  - **Right-click**: Open configuration menu

## Configuration

The Windows GUI version supports persistent configuration stored in `settings.json`:

```json
{
  "control_mode": 0,
  "ai": 1
}
```

- `control_mode`: 0 = Keyboard, 1 = Mouse
- `ai`: 0 = Easy, 1 = Normal, 2 = Hard

## High Scores

The Windows GUI version tracks high scores in `highscores.json`. Players can enter their name when achieving a high score. The system maintains the top 10 scores.

## Project Structure

```text
src/
├── core/              # Platform-agnostic game logic
│   ├── game_core.cpp  # Main game simulation, physics, AI
│   └── game_core.h    # GameCore class, GameState struct
├── main.cpp           # Console version entry point
├── game.cpp/.h        # Console game loop, rendering, input
├── platform.h         # Platform abstraction interface
├── platform_win.cpp   # Windows console implementation
├── platform_posix.cpp # POSIX console implementation
└── win/               # Windows GUI-specific code
    ├── main_win.cpp   # GUI entry point, DPI awareness
    ├── game_win.cpp/.h # Win32/GDI rendering, input, menus
    ├── settings.cpp/.h # JSON settings persistence
    └── highscores.cpp/.h # JSON high score persistence
```

## Physics & Gameplay

The game features realistic Pong physics:

- **Ball Behavior**: Consistent velocity with slight acceleration on paddle hits
- **Paddle Interaction**: Ball spin affected by paddle velocity and contact point
- **AI Intelligence**: Three difficulty levels with different reaction speeds
- **Collision Detection**: Precise ball-to-paddle collision with proper normal calculation

## Development

### Architecture

The project uses a modular architecture with shared game logic:

1. **GameCore** (`src/core/`): Platform-independent game simulation
2. **Platform Layer** (`src/platform*`): Console I/O abstraction
3. **Frontends**: Console (`src/game.*`) and Windows GUI (`src/win/`)

### Build Times

- **Clean configure**: ~1-5 seconds
- **Clean build**: ~10-30 seconds
- **Incremental build**: ~2-10 seconds

### Common Issues

1. **CMake version warning**: Update `CMakeLists.txt` line 1 to `cmake_minimum_required(VERSION 3.10)` if needed
2. **Windows blank window**: Run console version to check for error messages
3. **High DPI issues**: The GUI version includes DPI awareness code for modern displays

## License

This project is available for educational and personal use.

## Contributing

Contributions are welcome! The codebase is designed for easy extension:

- Add new platforms by implementing the `Platform` interface
- Extend game features in the shared `GameCore` class
- Add new frontends following the existing patterns

## System Requirements

- **Windows**: Windows 7+ (GUI version), any Windows with console support
- **Linux**: Any POSIX-compliant system with terminal support
- **Memory**: < 10MB RAM
- **Storage**: < 5MB disk space
