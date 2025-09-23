Pong (Win32 / Console)
======================

A small Pong clone written in C++ using the Win32 API and standard library (no external dependencies).

Features

- Win32/GDI frontend with double-buffering
- Simple console frontend (fallback)
- Rounded paddle visuals that match collision shape
- Mouse and keyboard controls
- AI difficulty levels (Easy / Normal / Hard)
- Persistent settings and highscores saved as JSON files
- DPI-aware UI (per-frame ui_scale, WM_DPICHANGED handling)

Quick start (Windows)

<<<<<<< HEAD
## Prerequisites
- **General**: CMake 3.8+ and a C++ toolchain
- **Windows GDI version**: Visual Studio (MSVC) or compatible compiler
- **Vulkan version**: Vulkan SDK, X11 development libraries (Linux)
=======
1. Prerequisites
   - Visual Studio (MSVC) or CMake + a C++ toolchain
   - CMake 3.20+ recommended
>>>>>>> parent of c7097b5 (Implement Vulkan renderer with cross-platform support)

2. Build
   Open PowerShell in the repository root and run:

<<<<<<< HEAD
### Windows
Open PowerShell in the repository root and run:
=======
   ```powershell
   .\build.bat
   ```
>>>>>>> parent of c7097b5 (Implement Vulkan renderer with cross-platform support)

   This configures and builds the project into the `build` folder (Release by default). To build Debug:

   ```powershell
   .\build.bat Debug
   ```

   To clean the build directory:

<<<<<<< HEAD
### Linux/Cross-platform (CMake)
```bash
# Install Vulkan development dependencies
sudo apt install libvulkan-dev vulkan-tools libx11-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

To clean the build directory:
```powershell
.\build.bat clean
```

## Run
- **Vulkan build**: `build/pong_vulkan` (Linux) or `build\Release\pong_vulkan.exe` (Windows)
- **GUI build**: `build\Release\pong_win.exe` (Windows only)
- **Console build**: `build\Release\pong.exe` or `build/pong`
=======
   ```powershell
   .\build.bat clean
   ```

3. Run
   - GUI build: `build\Release\pong_win.exe`
   - Console build: `build\Release\pong.exe`
>>>>>>> parent of c7097b5 (Implement Vulkan renderer with cross-platform support)

Controls

- Menu navigation: Up/Down arrows, Enter to select
- Toggle control mode: Left/Right
- Keyboard gameplay: W/S for left paddle, Up/Down for right paddle
- Mouse gameplay: move mouse vertically over window (left paddle follows Y)
- Quit: Q or select Quit from menu

High Scores and Settings

- Persistent settings are stored in `settings.json` next to the executable.
- High scores are stored in `highscores.json` next to the executable.

DPI and scaling notes

- The UI computes a `ui_scale` value from the window's DPI (96 DPI == scale 1.0).

- The app listens for `WM_DPICHANGED` and recomputes layout. Modals recompute scale per-frame, so resizing while a modal is open should update layout.

Troubleshooting

- If build fails during CMake configure, ensure your generator matches your Visual Studio version. You can force a generator like:

   ```powershell
   .\build.bat Release "Visual Studio 17 2022"
   ```

- If the Win32 window is blank or crashes, run the console build `build\\Release\\pong.exe` to see logs.

- If you see misaligned text or tiny UI on high-DPI screens, make sure your Windows DPI scaling is set correctly and restart the app after system-level DPI changes. The app also tries to scale dynamically on `WM_DPICHANGED`.

---

Development notes

<<<<<<< HEAD
- **Win32 GDI version**: `src/win/game_win.cpp`
- **Vulkan version**: `src/vulkan/` directory
- **Core simulation**: `src/core/game_core.cpp` and headers
- **High score/settings persistence**: `src/win/highscores.cpp`, `src/win/settings.cpp`

## Vulkan Version

The Vulkan renderer provides modern, hardware-accelerated graphics with cross-platform support. See `docs/vulkan_migration.md` for detailed information about the Vulkan implementation.

**Key advantages:**
- Hardware-accelerated rendering
- Cross-platform compatibility (Windows, Linux)
- Modern graphics API features
- Better performance scaling

**Requirements:**
- Vulkan 1.0+ compatible graphics hardware
- Vulkan SDK installed
- Platform-specific window system (Win32 on Windows, X11 on Linux)
=======
- Primary UI code: `src/win/game_win.cpp`
- Core simulation: `src/core/game_core.cpp` and headers
- High score/settings persistence: `src/win/highscores.cpp`, `src/win/settings.cpp`
>>>>>>> parent of c7097b5 (Implement Vulkan renderer with cross-platform support)

---

Contributing

- Fork and send PRs. Keep changes small and test builds on MSVC.

License

- (Add your license here)
