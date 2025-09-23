# Copilot Instructions for PongCpp

## Repository Summary

PongCpp is a classic Pong game implementation written in C++ with dual frontend support:
- **Console version**: Cross-platform text-based version using POSIX/Win32 platform abstraction
- **Windows GUI version**: Native Win32/GDI windowed version with DPI awareness, mouse/keyboard controls, settings persistence, and high scores

**Repository size**: Small (~15 source files, ~3K lines of code)  
**Languages**: C++ (C++17 standard)  
**Build system**: CMake 3.8+ (prefers 3.20+)  
**Target platforms**: Windows (primary), Linux/POSIX (console only)  
**External dependencies**: None (uses only standard library + platform APIs)

## Build Instructions

### Prerequisites
- **Windows**: Visual Studio (MSVC) or CMake + C++ toolchain, CMake 3.20+
- **Linux/POSIX**: g++/clang++ with C++17 support, CMake 3.8+

### Build Commands

**IMPORTANT**: Always build from the repository root directory.

#### Windows (Primary Method)
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

**Build outputs**: 
- GUI: `build\Release\pong_win.exe` 
- Console: `build\Release\pong.exe`

#### Linux/Cross-platform (Console only)
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

**Build output**: `build/pong` (console version only)

### Build Times
- **Clean configure**: ~1-5 seconds
- **Clean build**: ~10-30 seconds  
- **Incremental build**: ~2-10 seconds

### Common Build Issues

1. **CMake version warning**: "Compatibility with CMake < 3.10 will be removed"
   - **Solution**: This is a deprecation warning, safe to ignore
   - **Workaround**: Update CMakeLists.txt line 1 to `cmake_minimum_required(VERSION 3.10)`

2. **Windows blank window or crash**:  
   - **Diagnosis**: Run console version `build\Release\pong.exe` to see logs
   - **Common cause**: DPI scaling issues on high-DPI displays

3. **Build.bat permission denied on Linux**:
   - **Cause**: Batch files don't execute on Linux  
   - **Solution**: Use cmake commands directly as shown above

4. **Missing Win32 libraries**:
   - **Target**: Windows GUI version requires `user32.dll` and `gdi32.dll`
   - **Solution**: These are automatically linked on Windows builds

## Project Layout

### Core Architecture
```
src/
├── core/           # Platform-agnostic game logic
│   ├── game_core.cpp  # Main game simulation, physics, AI
│   └── game_core.h    # GameCore class, GameState struct
├── main.cpp        # Console version entry point  
├── game.cpp/.h     # Console game loop, rendering, input
├── platform.h      # Platform abstraction interface
├── platform_win.cpp   # Windows console implementation
├── platform_posix.cpp # POSIX console implementation  
└── win/            # Windows GUI-specific code
    ├── main_win.cpp    # GUI entry point, DPI awareness
    ├── game_win.cpp/.h # Win32/GDI rendering, input, menus
    ├── settings.cpp/.h # JSON settings persistence  
    └── highscores.cpp/.h # JSON high score persistence
```

### Key Configuration Files
- `CMakeLists.txt`: Build configuration (creates `pong` and `pong_win` targets)
- `build.bat`: Windows build script with generator detection
- `.gitignore`: Excludes build artifacts (`/build/`, `*.vcxproj*`, `*.sln`)

### Runtime Files (Created by GUI version)
- `settings.json`: Control mode, AI difficulty (saved next to executable)
- `highscores.json`: Player scores (saved next to executable)

### Validation and Testing

**No automated tests exist**. Manual validation steps:

1. **Console version**: Should display ASCII Pong game, W/S and arrow keys work
2. **Windows GUI**: Should show DPI-aware window with configuration menu
3. **Settings persistence**: Change settings, restart, verify settings preserved  
4. **High DPI**: Test on high-DPI display, verify proper scaling

### Development Workflow

**For code changes**:
1. Always run build commands from repository root
2. Test console version first (cross-platform, easier debugging)
3. Test Windows GUI version for UI/DPI changes
4. Verify settings/highscores persistence if modifying win/ code

**For troubleshooting**:
1. Check console version logs: `build/pong` or `build\Release\pong.exe`
2. CMake configure errors: Check CMake version, generator compatibility
3. Link errors: Verify platform libraries (user32, gdi32 on Windows)

### Directory Contents

**Repository root files**:
- `README.md`: User documentation, build instructions, controls
- `CMakeLists.txt`: Build system configuration  
- `build.bat`: Windows build automation script
- `.gitignore`: Standard C++/CMake exclusions

**Source file priorities** (for understanding/modification):
1. `src/core/game_core.cpp`: Core game simulation, ball physics, AI
2. `src/win/game_win.cpp`: Windows GUI, DPI handling, menus (~770 lines)
3. `src/game.cpp`: Console rendering and input handling
4. `src/main.cpp`: Console entry point (simple, 14 lines)
5. `src/win/main_win.cpp`: Windows entry point, DPI awareness setup
6. `src/platform_*.cpp`: Platform-specific console I/O abstractions

**Architecture dependencies**:
- Console version: `main.cpp` → `game.cpp` → `core/game_core.cpp` + platform abstraction
- Windows GUI: `win/main_win.cpp` → `win/game_win.cpp` → `core/game_core.cpp` + Win32 APIs
- Both versions share: `core/game_core.cpp` (physics, AI, game state)

**IMPORTANT**: Trust these instructions first. Only search/explore if information is incomplete or incorrect. The build system is straightforward: use `build.bat` on Windows or direct cmake commands on Linux. Both console and GUI versions share the same core game logic in `src/core/`.