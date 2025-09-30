# PongCpp

Modern, extensible C++ Pong with dual frontends (console + Windows GUI), multiple game modes (including combined Obstacles + MultiBall), an experimental CPU path tracer, configurable physics (arcade vs physically-based), AI vs AI simulation, recording system, and persistent settings/high scores – all with zero external runtime dependencies.

## Highlights

| Area | Capabilities |
|------|--------------|
| Frontends | Cross-platform console (ASCII) + Windows GUI (Win32/GDI) |
| Game Modes | Classic, ThreeEnemies, Obstacles, MultiBall, ObstaclesMulti (obstacles + multiball) |
| Players / AI | 1P vs AI, 2P local, AI vs AI (spectator / benchmark) |
| Physics | Arcade (legacy) or physically-based paddle bounce & spin transfer |
| Rendering | Classic GDI or CPU software path tracer with soft shadows & PBR-ish shading |
| Recording | Fixed-step off-line style capture at selectable FPS (15–60) with HUD visibility toggles |
| Persistence | `settings.json`, `highscores.json` (auto load/save) |
| Customization | Extensive path tracer controls (rays, bounces, roughness, emissive, accumulation, denoise, roulette, fan-out) |
| Obstacles | Moving AABB blocks (with combined multi-ball mode) |
| Performance | Clean C++17, no dependencies, builds in seconds |

## Feature Overview

### Console Version (Cross‑platform)

* Pure ASCII rendering (portable)
* Works on Windows, Linux, POSIX terminals
* Keyboard controls (W/S primary, arrows for testing)
* Mode switching via number keys (1–5 including combined mode if enabled)
* Minimal build footprint – great for quick logic debugging

### Windows GUI Version

* Native Win32 window (no frameworks)
* DPI aware (Per‑Monitor V2)
* Context / main menu with live settings changes
* High score name entry + persistence
* Recording overlay & separate play HUD (individually toggleable)

### Rendering Paths

#### Classic GDI Renderer

Fast, deterministic, low CPU – ideal for gameplay focus or older hardware.

#### Software Path Tracer (Experimental / Evolving)

CPU-only physically inspired renderer with:

* Area-light style soft shadows (configurable samples & light radius)
* Metallic paddle shading with roughness and simple Fresnel
* Emissive balls (bounce lighting) with adjustable intensity
* Temporal accumulation + 3x3 spatial denoise
* Russian roulette termination & optional combinatorial fan‑out (safety caps)
* Orthographic or perspective projection

Tuning parameters (UI backed / persisted): rays per frame, max bounces, internal resolution %, roughness, emissive %, accumulation alpha, denoise strength, force rays-per-pixel, ortho toggle, soft shadow samples, light radius scale, PBR enable, roulette enable/start/min probability, fan-out enable/cap/abort.

### Game Modes

| Mode | Description |
|------|-------------|
| Classic | Standard two vertical paddles |
| ThreeEnemies | Adds autonomous top & bottom horizontal paddles |
| Obstacles | Moving center-field obstacle bricks (AABB) |
| MultiBall | Multiple simultaneous balls (chaos) |
| ObstaclesMulti | Obstacles + MultiBall combined (movement & collision active) |

### Player / AI Configurations (Player Mode)

* 1P vs AI (default)
* 2P local (left vs right human – disables right AI)
* AI vs AI (spectator / stress test – both paddles AI)

### Physics Modes

* Arcade: Original “lively” bounce with simplified energy handling
* Physical: More consistent near-elastic response with velocity & tangent injection producing controlled spin

### Recording System

* Deterministic fixed-step simulation at selected recording FPS (15–60)
* Separate recording status panel (frames elapsed, time, target FPS)
* Optionally hide play HUD and/or recording overlay via settings

### HUD & Overlays

* Score, mode, physics flag, renderer stats (when path tracing)
* Toggle visibility independently for normal play vs recording

### Persistence (Settings Fields)

Stored in `settings.json` next to the executable (excerpt – fields evolve):

```text
recording_mode, recording_fps,
control_mode, ai, player_mode,
renderer, game_mode, physics_mode,
pt_rays_per_frame, pt_max_bounces, pt_internal_scale,
pt_roughness, pt_emissive, pt_accum_alpha, pt_denoise_strength,
pt_force_full_pixel_rays, pt_use_ortho,
pt_rr_enable, pt_rr_start_bounce, pt_rr_min_prob_pct,
pt_fanout_enable, pt_fanout_cap, pt_fanout_abort,
pt_soft_shadow_samples, pt_light_radius_pct, pt_pbr_enable,
hud_show_play, hud_show_record
```

High scores persist in `highscores.json` (top list, sorted, trimmed).

## Technical Stack

* Language: C++17
* Build: CMake (single config generator support + Visual Studio multi-config)
* External libs: None
* Targets: `pong` (console), `pong_win` (GUI)

## Building

### Windows (Primary)

```powershell
# Clean
./build.bat clean

# Default Release (x64 auto)
./build.bat

# Debug
./build.bat Debug

# Specify generator
./build.bat Release "Visual Studio 17 2022"

# Force 32-bit (rarely needed)
./build.bat Release "Visual Studio 17 2022" Win32

```text
Outputs placed in `dist/release/` or `dist/debug/`.

### Linux / POSIX (Console Only)

```bash
rm -rf build
mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Binary appears in `dist/release/pong`.

## Controls (Summary)

Console:

* W / S – Move left paddle
* 1–5 – Change mode (Classic / ThreeEnemies / Obstacles / MultiBall / ObstaclesMulti)
* Q – Quit

GUI (Keyboard mode): W / S move, ESC exit, Right‑click open menu.
GUI (Mouse mode): Cursor vertical = paddle, ESC exit, Right‑click menu.

Player mode / AI flags applied on game start; AI difficulty affects tracking speed.

## Configuration Workflow (GUI)

1. Right‑click in window → menu / settings panel
2. Adjust renderer, physics, mode, player mode, AI difficulty, path tracer tuning
3. Start game / toggle recording
4. Settings auto-save on change or exit

## Project Layout

```text
src/
  core/        # GameCore (physics, AI, modes, obstacles, multi-ball)
  console/     # Modern console frontend (supersedes legacy root files)
  platform/    # Platform abstraction (win/posix console)
  win/         # GUI application (app, rendering, ui, persistence, renderer)
  (legacy root: main.cpp, game.cpp etc. retained for backward compatibility)
docs/          # Hand-written docs & generated doxygen (html after build)
dist/          # Build outputs & runtime JSON
```

## Physics & Gameplay Notes

* Sub-stepped integration keeps collisions stable at high velocities
* Tangent influence + paddle velocity mixing produces controllable spin
* Combined mode (ObstaclesMulti) activates both obstacle movement & multi-ball collision set

## Path Tracer Implementation Sketch

* Orthographic mapping to 2D game space (optionally perspective)
* Paddles: tinted metallic surfaces with roughness-driven reflection lobe
* Ball: emissive sphere acts as soft area light (sampled N times)
* Walls / arena: diffuse
* Temporal accumulation resets on parameter changes / resize
* Early termination via roulette; experimental fan‑out for exploration

## Example Minimal Settings JSON

```json
{
  "control_mode": 1,
  "ai": 1,
  "player_mode": 0,
  "renderer": 1,
  "game_mode": 4,
  "physics_mode": 1,
  "recording_mode": 1,
  "recording_fps": 60,
  "pt_rays_per_frame": 8000,
  "pt_max_bounces": 3,
  "pt_internal_scale": 60,
  "pt_roughness": 15,
  "pt_emissive": 120,
  "pt_accum_alpha": 12,
  "pt_denoise_strength": 70
}
```

## Common Issues

| Issue | Resolution |
|-------|------------|
| Doxygen warnings about user guide refs | Updated guide removes broken anchors; rebuild docs |
| Blank GUI window | Run console build to check logs; verify DPI scaling |
| High CPU (path tracer) | Lower rays per frame, internal scale, or disable fan-out |
| Obstacles missing in combined mode | Ensure mode set to ObstaclesMulti (value 4) |

## Contributing

Ideas: new game modes (power-ups?), improved material model, replay export, network multiplayer, energy conserving paddle scattering, GPU backend, WASM build.

Coding guidelines: const-correct, RAII, no exceptions thrown across module boundaries, keep dependencies zero.

## License

Educational / personal use. Attribute the project if you fork substantially.

## Acknowledgements

Built as a compact demonstration of clean C++17 structure + experimental CPU rendering techniques.

Enjoy experimenting – open an issue or PR for enhancements.
