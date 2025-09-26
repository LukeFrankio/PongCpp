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

### Experimental Path Tracing Renderer (Win32 Only)

An optional pure CPU software path tracer (still only Win32 + GDI `StretchDIBits`) provides a soft glowy aesthetic. It is fully parameter‑driven (no fixed quality presets anymore).

Configuration menu entries:

- Renderer: Classic | Path Tracer
- Path Tracer Settings… (opens modal with live sliders)

Current path tracer parameters (all persisted in `settings.json`):

| Setting (UI Label)        | JSON Field                | Range / Units                  | Effect |
|---------------------------|---------------------------|--------------------------------|--------|
| Rays / Frame              | `pt_rays_per_frame`       | 100 – 200000 (step 100)        | Total ray budget per frame (or per pixel if force toggle ON). Higher = less noise, more CPU time. |
| Max Bounces               | `pt_max_bounces`          | 1 – 8                          | Path depth. More bounces capture more indirect light but cost more rays. |
| Internal Scale %          | `pt_internal_scale`       | 25 – 100 %                     | Internal rendering resolution relative to window. Lower = faster, blurrier. |
| Metal Roughness %         | `pt_roughness`            | 0 – 100 %                      | 0 = mirror paddles, 100 = very rough (diffuse-ish) reflections. |
| Emissive %                | `pt_emissive`             | 50 – 300 %                     | Scales ball light intensity (100% = base). |
| Accum Alpha %             | `pt_accum_alpha`          | 1 – 50 %                       | Temporal blend factor (EMA). Higher = faster convergence but more ghosting; 10–20% typical. |
| Denoise %                 | `pt_denoise_strength`     | 0 – 100 %                      | Strength of 3x3 spatial blur (0 = off). |
| Force 1 ray / pixel (ON)  | `pt_force_full_pixel_rays`| 0 / 1                          | Interpret Rays/Frame as rays PER pixel instead of a global pool. Good for consistent sampling at small resolutions. |

Notes:

- Changing any parameter automatically resets the accumulation history to avoid stale noise patterns.
- When Force 1 ray/pixel is OFF the ray budget is evenly distributed (integer division) and may yield <1 spp for very large windows (still at least 1 ray/pixel via min). Turn it ON if you prefer stable sampling per pixel (at the cost of potentially large total ray counts).
- Metallic paddles give clearer silhouettes than the earlier glass prototype while still producing highlight variety via roughness.

Implementation details:

- Pure C++17 + Win32 (no GPU APIs)
- Internal low‑resolution float buffer, upscaled each frame
- Dynamic samples per pixel derived from ray budget and internal resolution
- Temporal accumulation (exponential moving average) + optional 3×3 spatial denoise
- Simple materials: diffuse walls, emissive sphere, metallic paddles with roughness perturbation
- Classic outline overlay (paddles + ball highlight) for crisp gameplay silhouettes

Performance tips:

- Start with Internal Scale 50–70% and ~5000–15000 Rays/Frame.
- Increase Accum Alpha slowly if you want faster convergence; too high causes visible ghost trails.
- Use Denoise 50–80% for a good quality/performance compromise.
- Large windows + Force 1 ray/pixel + high Rays/Frame can become very CPU heavy—dial one back if FPS drops.

Legacy fields `quality` and presets remain in the JSON for backward compatibility but are ignored by the new renderer logic.

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

The Windows GUI version stores persistent configuration in `settings.json`. Example (fields trimmed):

```jsonc
{
  "control_mode": 0,          // 0=keyboard,1=mouse
  "ai": 1,                    // 0=easy,1=normal,2=hard
  "renderer": 1,              // 0=classic,1=path tracer
  "quality": 1,               // legacy (ignored by path tracer)
  "pt_rays_per_frame": 8000,
  "pt_max_bounces": 3,
  "pt_internal_scale": 60,
  "pt_roughness": 15,
  "pt_emissive": 100,
  "pt_accum_alpha": 12,
  "pt_denoise_strength": 70,
  "pt_force_full_pixel_rays": 0
}
```

Adjust values via the in‑game Path Tracer Settings modal (recommended) rather than manual edits.

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
