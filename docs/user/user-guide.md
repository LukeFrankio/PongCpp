# PongCpp User Guide

Welcome! This guide explains how to install, configure, and enjoy PongCpp across its console and Windows GUI frontends, including advanced modes, rendering, recording, and physics options.

---

## 1. Editions

| Edition | Binary | Description |
|---------|--------|-------------|
| Console | `pong` / `pong.exe` | Cross‑platform ASCII version (lightweight, debug friendly) |
| Windows GUI | `pong_win.exe` | Win32 windowed version with menus, path tracer, persistence, recording |

Both share the same core simulation (`GameCore`).

---

## 2. System Requirements

| Component | Console | Windows GUI |
|-----------|---------|-------------|
| OS | Windows / Linux / POSIX | Windows 7+ (DPI aware) |
| CPU | Any C++17-capable | Multi-core recommended for path tracing |
| RAM | < 10 MB | < 50 MB (with path tracer) |
| Disk | < 5 MB | < 5 MB + JSON settings/scores |

Path tracer performance scales strongly with core count; start with lower rays if unsure.

---

## 3. Quick Start

### Console (Quick Start)

```bash
./pong            # Linux / POSIX
pong.exe          # Windows
```

### Windows GUI

```text
double‑click pong_win.exe
```

Right‑click in the window to open the main menu / settings panel.

---

## 4. Game Modes

| Key | Mode | Description |
|-----|------|-------------|
| 1 | Classic | Standard vertical paddles |
| 2 | ThreeEnemies | Adds top + bottom horizontal AI paddles |
| 3 | Obstacles | Moving central obstacle bricks (AABB) |
| 4 | MultiBall | Multiple simultaneous balls |
| 5 | ObstaclesMulti | Obstacles + MultiBall combined |

Mode switching is immediate; multi-ball spawns additional balls (existing score preserved).

---

## 5. Player / AI Modes

Configured via settings / menu:

| Player Mode | Left Paddle | Right Paddle |
|-------------|-------------|--------------|
| 1P vs AI | Human | AI |
| 2P Local | Human | Human |
| AI vs AI | AI | AI |

AI difficulty (Easy / Normal / Hard) adjusts reaction speed & tracking aggressiveness.

---

## 6. Physics Modes

| Mode | Behavior |
|------|----------|
| Arcade | Livelier legacy style, slight energy gain keeps tempo high |
| Physical | Near-elastic response; tangent spin + paddle velocity transfer tuned for control |

Switch affects paddle-ball collision math only (not wall/obstacle reflections).

---

## 7. Controls

### Console

| Key | Action |
|-----|--------|
| W / S | Move left paddle |
| 1–5 | Change game mode |
| Q | Quit |

Right paddle arrow keys still work (debug / second player test).

### Windows GUI – Keyboard Mode

| Key | Action |
|-----|--------|
| W / S | Move paddle |
| ESC | Exit application |
| Right‑click | Open menu / settings |

### Windows GUI – Mouse Mode

| Action | Result |
|--------|--------|
| Move mouse vertically | Paddle follows cursor (locked horizontally) |
| ESC | Exit |
| Right‑click | Menu / settings |

---

## 8. Rendering Options (GUI)

| Renderer | Use Case |
|----------|----------|
| Classic (GDI) | Maximum FPS, low noise, baseline play |
| Path Tracer | Visual experiment: soft shadows, emissive bounce, metallic paddles |

Key path tracer parameters (all persisted): Rays/frame, Max bounces, Internal resolution %, Roughness, Emissive %, Accum (temporal), Denoise strength, Force rays-per-pixel, Ortho toggle, Soft-shadow samples, Light radius %, PBR enable, Roulette controls, Fan-out controls.

If image becomes very noisy: lower roughness, increase rays, or disable fan-out.

---

## 9. Recording

Enable recording mode to run simulation at a fixed target FPS (15–60). This produces consistent temporal sampling for frame capture tools. While recording:

* Overlay panel shows: frame count, simulated time, target FPS
* Optionally hide normal HUD or recording overlay (two toggles)
* Game continues deterministic stepping independent of display refresh

Recording currently writes frames only if you integrate an external capturer (no built-in file export yet) – the mode ensures temporal stability.

---

## 10. HUD Elements

| Element | Meaning |
|---------|---------|
| Score | Player vs opponent tally |
| Mode | Current game mode shorthand |
| Phys | Physics mode (Arc/Phys) |
| PT Stats | When path tracer active: rays, ms timings, internal resolution |
| Rec Panel | (If recording) frame/time/FPS box |

Visibility: `hud_show_play`, `hud_show_record` (settings) govern display contexts.

---

## 11. Persistence Files

Created beside the executable:

| File | Purpose |
|------|---------|
| settings.json | All adjustable options & last selected mode/renderer |
| highscores.json | Sorted high score table (trimmed) |

You may edit manually; game clamps & validates ranges on load.

---

## 12. High Scores

Triggered when a new score exceeds the lowest stored entry. Enter a name (GUI). Table retains top N (commonly 10). Removal or editing can be done by modifying `highscores.json` while the game is closed.

---

## 13. Performance Tips

| Goal | Recommendation |
|------|----------------|
| Higher FPS (PT) | Lower rays, reduce internal scale, fewer bounces |
| Faster convergence | Increase rays first, then reduce roughness |
| Stable image | Raise accum alpha (lower temporal blending %) |
| Sharper w/ less blur | Lower denoise strength |
| Prevent runaway fan-out | Keep cap moderate (default 2M) |

Console version is ideal for verifying logic changes quickly.

---

## 14. Troubleshooting

| Symptom | Fix |
|---------|-----|
| Obstacles not moving in combined mode | Ensure mode = ObstaclesMulti (5) |
| Path tracer extremely slow | Disable fan-out; lower rays & bounces |
| Blank GUI window | Run console build to inspect runtime messages |
| Settings ignored | Delete `settings.json` (will regenerate defaults) |
| High scores not saving | Check directory write permissions |
| Excessive noise | Increase rays or enable denoise; lower roughness |

### Console Specific

Misaligned ASCII: Use a monospace font & 80x24 (or larger) terminal. If input lag occurs in POSIX terminals, ensure raw mode is supported.

### Windows Specific

Blurry scaling: Adjust Windows display scaling (app is DPI aware). If the window freezes, verify no external overlay tools are interfering.

---

## 15. Advanced Notes

* Physics sub-stepping prevents tunneling at higher speeds.
* Paddle influence blends center offset + paddle velocity.
* Multi-ball shares scoring; removing balls (future) can be added safely – state vector supports dynamic length.
* Obstacles employ AABB vs circle collision with velocity reflection & slight positional pushback.

---

## 16. Manual Editing of Settings

The game auto-saves after menu changes. Editing JSON while running is not recommended (changes cached in memory). Ranges are clamped on load to prevent instability.

---

## 17. Getting Help

Open an issue (if hosted) or inspect core source (`src/core/game_core.*`) for authoritative logic. The console frontend remains the fastest path to debug behavior.

Enjoy the game – experiment with AI vs AI and path tracer tuning!
