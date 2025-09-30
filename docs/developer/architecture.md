# Architecture Guide

This document details the internal structure of PongCpp: its core simulation, rendering paths, settings & persistence, AI, and extensibility points. It reflects the current feature set including obstacles + multi-ball combined mode, recording system, path tracer, physics mode toggle, and expanded settings.

## 1. High-Level Overview

```text
      +------------------+
      |   Frontends      |
      |------------------|
      | Console (ASCII)  |
      | Windows GUI      |
      +---------+--------+
            |
        (calls API)
            v
      +------------------+
      |    GameCore      |
      |------------------|
      | Physics / Modes  |
      | AI (Left/Right)  |
      | Obstacles        |
      | Multi-ball       |
      +---------+--------+
            |
            v
      +------------------+
      |  Shared State    |
      |  (GameState)     |
      +------------------+
            |
    +-----------+------------+
    v                        v
  +-----------+           +--------------+
  | Rendering |           | Persistence  |
  | Classic   |           | settings.json|
  | PathTrace |           | highscores   |
  +-----------+           +--------------+
```

Frontends own a `GameCore` instance, drive `update(dt)`, and render a view of `GameState`. The Windows GUI enriches presentation via HUD layers and either the classic GDI renderer or the experimental software path tracer.

## 2. Core Components

| Component | Responsibility |
|-----------|----------------|
| `GameCore` | Physics simulation, scoring, AI steering, mode orchestration |
| `GameState` | Data container for paddles, balls, scores, obstacles, dimensions, mode |
| Platform Layer | Abstracted console I/O (blocking-free keyboard, ANSI control) |
| Console Frontend | Terminal rendering & input mapping to `GameCore` |
| Windows GUI Frontend | Window lifecycle, menus, input routing, renderer integration, persistence |
| Path Tracer (`SoftRenderer`) | CPU ray/path sampling, accumulation, shading, upscaling |
| Settings Manager | Load/save user-configurable options & rewrite defaults when missing fields |
| High Scores Store | Ordered insertion + trimming of persistent scoreboard |

Legacy console root files remain for backward compatibility; newer console code lives under `src/console/` with clearer separation.

## 3. Design Principles

* **Zero external runtime deps** – only standard library + OS APIs
* **Deterministic core** – simulation independent of frame pacing (frontends may record at fixed step)
* **Progressive enhancement** – advanced features (path tracer, recording) layer on top, not embedded in physics logic
* **Strict separation** – `GameCore` unaware of rendering or OS concerns
* **Data-oriented state** – `GameState` is a plain struct; accessible for serialization or replay extension
* **Safety via clamping** – All persisted settings clamped on load to avoid invalid renderer or physics configurations

## 4. GameCore Details

Primary responsibilities:

* Integrate ball(s) & obstacle movement
* Resolve collisions (paddle, walls, obstacles)
* Apply paddle spin & velocity influence
* Dispatch scoring & reset events
* Update AI-controlled paddles (left/right based on player mode)

### Multi-Ball

`std::vector<BallState>` holds dynamic balls; index 0 is mirrored to legacy `ball_x/ball_y` values for compatibility with renderers expecting a primary ball. New balls spawn with randomized angle & speed scaling.

### Obstacles & Combined Mode

`std::vector<Obstacle>` updated when mode is Obstacles or ObstaclesMulti. Collision logic reflects velocity across obstacle AABB normals with slight penetration correction.

### Physics Modes

* Arcade: simplified restitution & tangent impulse (slightly energizing)
* Physical: closer to elastic bounce; energy drift minimized; paddle influence separated into normal vs tangential components.

### AI System

Two enable flags (`left_ai_enabled`, `right_ai_enabled`) set by player mode selection. Each AI paddle tracks a target Y (or X for horizontal paddles) using a speed multiplier derived from difficulty.

### Sub-Stepping

Large `dt` split into fixed micro-steps (e.g., 240 Hz equivalent) to prevent tunneling and preserve consistent collision ordering across variable frame rates or recording modes.

### Collision Outline

1. Integrate position
2. Check wall bounds (invert velocity)
3. Test paddle volumes (vertical & optional horizontal paddles)
4. Apply paddle transfer: normal reflection, tangent offset spin, velocity injection
5. Obstacles (branch if mode supports)
6. Score detection (ball passes side) → increment score & reset ball/paddles

### Scoring & Reset

State resets minimally: scores persist, ball(s) maybe respawned. Multi-ball modes can respawn all or maintain distribution depending on current internal logic.

### Extending Modes

Add enum value to `GameMode`; gate new behavior in update loops; ensure renderers respect visibility (e.g., surfaces or overlays) and persistence adjusts store if needed.

## 5. Platform Abstraction (Console)

`Platform` interface standardizes minimal console needs: char input detection, raw getch, cursor visibility, ANSI enable, screen clear. Backends:

* Windows: `_kbhit`, `_getch`, Win32 console functions
* POSIX: termios configuration + non-blocking reads & ANSI sequences

Console frontend loops: poll input → map to paddle commands → call `update(dt)` → re-render ASCII frame.

## 6. Windows GUI Architecture

### Layers

| Layer | Role |
|-------|------|
| Window / Message Loop | Translate Win32 messages to app events |
| Input Router | Consolidate mouse + keyboard into abstract paddle commands |
| UI (Menus / Panels) | Settings, high scores, modal name entry |
| Rendering Adapter | Chooses Classic vs Path Tracer; composites HUD |
| Persistence | Loads settings/high scores at startup; saves on change or shutdown |
| Recording | Adjusts simulation step cadence & overlays recording stats |

### HUD & Panels

HUD draws scores & stats; recording panel separated to allow independent visibility toggles (play vs record contexts).

### DPI Awareness

Process-level Per Monitor V2 ensures crisp scaling; sizes computed respecting DPI for layout fidelity.

## 7. Path Tracer Overview (`SoftRenderer`)

| Subsystem | Function |
|-----------|----------|
| Ray Generation | Orthographic (default) or perspective rays per pixel (budget-based or fixed spp) |
| Shading | Metallic paddles w/ roughness, diffuse walls, emissive ball area light sampling |
| Bounces | Up to N bounces; optional Russian roulette from configurable depth |
| Accumulation | Exponential moving average (temporal) + optional 3x3 spatial blend |
| Soft Shadows | Multiple samples over scaled light radius approximating area emission |
| Fan-Out Mode | Experimental exponential branching (guarded by cap + abort) |

Statistics (ms timings, spp, total rays, average bounce depth) exposed for HUD.

State invalidations (resize / parameter change) reset accumulation history.

## 8. Persistence Layer

`settings.json` & `highscores.json` created next to executables. Loading process:

1. Read file (if missing → defaults)
2. Parse minimal JSON (flat key/value)
3. For each known field: if parse succeeds, clamp range; else keep default
4. Unknown fields ignored (forward compatibility)

Saving always writes a complete canonical set of known fields (stable keys, stable order not guaranteed but typically consistent).

High scores: load vector, append candidate, sort descending, truncate (top N), save.

## 9. Recording System

When enabled: simulation decouples from wall-clock; each frame advances by fixed `1/recording_fps`. Renderer continues to display frames as fast as produced; timing overlay shows simulated time progression.

Potential extensions: frame dump callbacks, video writer integration, deterministic random seed capture for re-simulation.

## 10. Extensibility Patterns

| Goal | Pattern |
|------|---------|
| New Game Mode | Extend `GameMode` enum + branch in update loops & UI cycle logic |
| New Renderer | Implement adapter with `render(GameState&)` + integrate menu toggle |
| New Setting | Add field → clamp/load/save in persistence → expose in UI → use in subsystem |
| Replay System | Serialize `GameState` deltas or input events each frame |
| Online Multiplayer | Replace direct paddle control with network inputs; preserve deterministic step |

## 11. Testing Strategy

No automated tests currently; practical workflow:

1. Validate logic via console build (fast iteration)
2. Stress multi-ball + obstacles combined mode
3. Check AI vs AI for stability over long runs
4. Toggle physics modes and ensure expected spin/energy characteristics
5. Path tracer smoke test: change roughness/emissive & verify accumulation resets

## 12. Performance Considerations

* Small code footprint keeps instruction cache favorable
* Avoids heap churn in hot loops (vectors pre-sized or reserve where needed)
* Path tracer budgets rays to maintain interactivity; fan-out guarded by hard cap
* Sub-stepping avoids expensive corrective collision rewinds

## 13. Code Style

* C++17, RAII, explicit intent
* `const` where possible, pass by reference for heavy structs
* Minimal macros, prefer inline helpers or lambdas
* Doxygen comments for public headers (core, renderer, persistence)

## 14. Future Directions (Ideas)

| Area | Enhancement |
|------|-------------|
| Rendering | SSE/AVX acceleration, GPU backend, spectral emissive, MIS |
| Gameplay | Power-ups, paddle deformation, tournament ladder |
| Tooling | Replay capture & deterministic playback, scripting API |
| Networking | Lockstep or rollback netcode prototype |
| Export | Automatic frame dump for recording mode |

## 15. Summary

PongCpp balances clarity and experimentation: a clean, deterministic simulation core with optional advanced rendering and extended modes. The modular approach allows adding features without entangling core physics or bloating dependencies.

Contributions that preserve clarity and separation are encouraged.
