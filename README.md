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

### Advanced Rendering Options (Windows GUI)

The Windows GUI version includes multiple rendering modes:

#### Classic GDI Renderer
- Fast, traditional 2D graphics using Windows GDI
- Crisp, pixel-perfect rendering suitable for all systems
- Minimal CPU usage and excellent performance

#### Software Path Tracer (Experimental)
- Pure CPU-based ray tracing renderer for realistic lighting effects
- Configurable parameters for quality vs performance trade-offs
- Creates soft, glowing aesthetic with realistic reflections and lighting

**Path Tracer Configuration:**
- **Rays per Frame**: Controls rendering quality (higher = less noise, more CPU time)
- **Max Bounces**: Light bounce depth (more bounces = more realistic lighting)
- **Internal Resolution**: Rendering scale (lower = faster, higher = sharper)
- **Material Properties**: Metallic roughness, emissive intensity
- **Post-Processing**: Temporal accumulation, spatial denoising

**Performance Notes:**
- Path tracer is CPU-intensive and intended for modern multi-core processors
- Start with lower settings and adjust based on your system's performance
- Classic renderer is recommended for older systems or when maximum frame rate is desired

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

The batch script now automatically selects 64‑bit (x64) for Visual Studio generators. Pass `Win32` (or `x86`) explicitly as the third argument to force a 32‑bit build.

```powershell
# Clean build (removes build directory)
.\build.bat clean

# Release build (default - builds both console and GUI versions, 64-bit)
.\build.bat

# Debug build
.\build.bat Debug

# Force specific Visual Studio generator (auto x64)
.\build.bat Release "Visual Studio 17 2022"

# Force 32-bit (override)
.\build.bat Release "Visual Studio 17 2022" Win32
```

**Build outputs:**

- **Release builds:**
  - GUI version: `dist\release\pong_win.exe`
  - Console version: `dist\release\pong.exe`
- **Debug builds:**
  - GUI version: `dist\debug\pong_win.exe`
  - Console version: `dist\debug\pong.exe`

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

**Build output:** `dist/release/pong` or `dist/debug/pong` (console version)

### Architecture (64-bit Enforcement)

By default Windows builds enforce 64-bit (pointer size must be 8). To build 32-bit intentionally:

```powershell
cmake -S . -B build32 -G "Visual Studio 17 2022" -A Win32 -D ENFORCE_64BIT=OFF -D CMAKE_BUILD_TYPE=Release
cmake --build build32 --config Release
```

Or via the batch script (still requires ENFORCE_64BIT override if you reconfigure manually afterwards):

```powershell
./build.bat Release "Visual Studio 17 2022" x86
```

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

The Windows GUI version automatically saves and loads configuration from `settings.json`. All settings can be modified through the in-game menus:

- **Right-click** during gameplay to access the configuration menu
- **Renderer Settings** to choose between Classic GDI and Path Tracer
- **Path Tracer Settings** for detailed ray tracing configuration
- **Control Settings** to switch between keyboard and mouse control
- **AI Difficulty** to adjust computer opponent strength

Example `settings.json` structure:
```json
{
  "control_mode": 1,              // 0=keyboard, 1=mouse
  "ai": 1,                        // 0=easy, 1=normal, 2=hard  
  "renderer": 0,                  // 0=classic, 1=path tracer
  "pt_rays_per_frame": 8000,      // Path tracer: rays per frame
  "pt_max_bounces": 3,            // Path tracer: maximum light bounces
  "pt_internal_scale": 60,        // Path tracer: render resolution %
  "pt_roughness": 15,             // Path tracer: surface roughness %
  "pt_emissive": 100,             // Path tracer: light intensity %
  "pt_accum_alpha": 12,           // Path tracer: temporal blending %
  "pt_denoise_strength": 70       // Path tracer: noise reduction %
}
```

**Note:** It's recommended to use the in-game settings menus rather than editing the JSON file directly, as the UI provides real-time preview and validation.

## High Scores

The Windows GUI version tracks high scores in `highscores.json`. Players can enter their name when achieving a high score. The system maintains the top 10 scores.

## Project Structure

```text
src/
├── core/                   # Platform-independent game logic
│   ├── game_core.cpp       # Main game simulation, physics, AI
│   └── game_core.h         # GameCore class, GameState struct
├── console/                # Console version implementation
│   ├── game.cpp/.h         # Console game loop, rendering, input
│   └── main.cpp            # Console entry point
├── platform/               # Platform abstraction layer
│   ├── platform.h          # Platform interface definition
│   ├── platform_posix.cpp  # POSIX/Linux implementation
│   └── platform_win.cpp    # Windows console implementation
├── main.cpp                # Legacy console entry point
├── game.cpp/.h             # Legacy console interface
├── platform.h              # Legacy platform abstraction
├── platform_*.cpp          # Legacy platform implementations
└── win/                    # Windows GUI implementation
    ├── app/                # Application management
    ├── events/             # Event system
    ├── input/              # Input handling
    ├── integration/        # Windows system integration
    ├── persistence/        # Settings and data persistence
    ├── platform/           # Windows-specific platform code
    ├── rendering/          # Rendering engines (GDI, path tracer)
    ├── ui/                 # User interface components
    ├── main_win.cpp        # GUI entry point, DPI awareness
    ├── game_win.cpp/.h     # Main game window and logic
    ├── settings.cpp/.h     # Settings management
    └── highscores.cpp/.h   # High score tracking
```

**Documentation:**
```text  
docs/
├── README.md               # Documentation overview
├── user/                   # User guides and tutorials
├── developer/              # Technical documentation
└── doxygen/                # Generated API documentation (after build)
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
