# API Reference (Updated)

Concise guide to the primary public-facing types, functions, and data structures in PongCpp. For architectural context see `architecture.md`.

---

## 1. Core Simulation

### 1.1 GameMode

```cpp
enum class GameMode {
    Classic = 0,
    ThreeEnemies,
    Obstacles,
    MultiBall,
    ObstaclesMulti // obstacles + multi-ball combined
};
```

### 1.2 Structures

```cpp
struct Obstacle { double x,y,w,h,vx,vy; };
struct BallState { double x,y,vx,vy; };

struct GameState {
    int gw, gh;
    double left_y, right_y;
    double ball_x, ball_y;  // mirror of balls[0]
    int paddle_h;
    int score_left, score_right;
    double top_x, bottom_x; // horizontal paddles (ThreeEnemies)
    int paddle_w;
    std::vector<Obstacle> obstacles; // active obstacles
    std::vector<BallState> balls;    // multi-ball
    GameMode mode;
};
```

### 1.3 GameCore API (Selected)

```cpp
class GameCore {
public:
    GameCore();
    void reset();
    void update(double dt);

    // Player movement
    void move_left_by(double dy);
    void set_left_y(double y);
    void move_right_by(double dy); // manual / test

    // Mode & configuration
    void set_mode(GameMode m);
    GameMode mode() const;
    void set_ai_speed(double m);
    void enable_left_ai(bool e);
    void enable_right_ai(bool e);
    void set_physical_mode(bool on);
    bool is_physical() const;

    // Multi-ball / obstacles
    void spawn_ball(double speed_scale = 1.0);
    const std::vector<BallState>& balls() const;
    const std::vector<Obstacle>& get_obstacles() const;

    // State access
    const GameState& state() const;
    GameState& state();
};
```

#### Notes

* `update(dt)` internally sub-steps for stability; callers should pass frame delta or fixed recording step.
* `spawn_ball()` only meaningful in multi-ball modes (ignored or harmless otherwise).
* AI enable toggles allow dynamic transition to AI vs AI or local multiplayer mid-session (reset recommended for clarity).

### 1.4 Physics Parameters

Located privately within `GameCore` (values subject to tuning):

```cpp
double restitution;       // Paddle bounce energy factor
double tangent_strength;  // Contact offset to spin scaling
double paddle_influence;  // Paddle velocity injection weight
```

Physics mode switch adjusts how these apply vs legacy “arcade” reflection path.

---

## 2. Rendering Interfaces (Windows)

### 2.1 Classic Renderer

Immediate GDI drawing (rectangles, filled shapes) – not exposed as a distinct public class; integrated through a rendering adapter inside the GUI code. Uses `GameState` read-only.

### 2.2 Software Path Tracer

Header: `win/soft_renderer.h`

```cpp
struct SRConfig { /* raysPerFrame, maxBounces, internalScalePct, metallicRoughness, emissiveIntensity, accumAlpha,
                     denoiseStrength, forceFullPixelRays, useOrtho, roulette*, fanout*, softShadowSamples,
                     lightRadiusScale, pbrEnable */ };
struct SRStats  { /* msTrace, msTemporal, msDenoise, msUpscale, msTotal, spp, totalRays, avgBounceDepth, etc */ };

class SoftRenderer {
public:
    void configure(const SRConfig&);
    void resize(int w, int h);
    void resetHistory();
    void render(const GameState &gs);
    const SRStats& stats() const;
    const BITMAPINFO& getBitmapInfo() const;
    const uint32_t*  pixels() const; // BGRA linear array
};
```

#### Rendering Flow

1. Frontend updates SRConfig from settings
2. `render()` generates or reuses accumulation
3. Packed BGRA buffer blitted with `StretchDIBits`
4. HUD overlays (scores, stats) drawn after image

---

## 3. Settings & Persistence

Header: `win/settings.h`

```cpp
struct Settings {
  int recording_mode, control_mode, ai, renderer, quality, game_mode;
  int pt_rays_per_frame, pt_max_bounces, pt_internal_scale;
  int pt_roughness, pt_emissive, pt_accum_alpha, pt_denoise_strength;
  int pt_force_full_pixel_rays, pt_use_ortho;
  int pt_rr_enable, pt_rr_start_bounce, pt_rr_min_prob_pct;
  int pt_fanout_enable, pt_fanout_cap, pt_fanout_abort;
  int pt_soft_shadow_samples, pt_light_radius_pct, pt_pbr_enable;
  int player_mode, recording_fps, physics_mode, hud_show_play, hud_show_record;
};

class SettingsManager {
public:
  Settings load(const std::wstring& path);
  bool save(const std::wstring& path, const Settings&);
};
```

Ranges are validated & clamped on load to preserve stability (e.g., preventing zero bounces or runaway fan-out settings).

### 3.1 High Scores

`highscores.json` contains an array of objects `{ "name": "Player", "score": N }` sorted descending and truncated (commonly top 10). New entries inserted via helper in GUI logic.

---

## 4. Console Platform Abstraction

```cpp
struct Platform {
    virtual bool kbhit() = 0;
    virtual int  getch() = 0;
    virtual void clear_screen() = 0;
    virtual void set_cursor_visible(bool) = 0;
    virtual void enable_ansi() = 0;
    virtual ~Platform() = default;
};
std::unique_ptr<Platform> createPlatform();
```

Used exclusively by the console frontend for cross-platform terminal behavior.

---

## 5. Build Targets & Commands

### Windows (Batch Script)

```powershell
./build.bat          # Release (x64 auto)
./build.bat Debug    # Debug
./build.bat Release "Visual Studio 17 2022"  # Specify generator
./build.bat Release "Visual Studio 17 2022" Win32  # Force 32-bit
```

### Cross-Platform Console

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs placed in `dist/release` or `dist/debug`.

---

## 6. Typical Usage (GUI)

```cpp
GameCore core;
core.set_mode(GameMode::ObstaclesMulti);
core.enable_left_ai(false);
core.enable_right_ai(true);
core.set_physical_mode(true);
while (running) {
    core.update(dt);
    renderer.render(core.state());
}
```

---

## 7. Error Handling & Stability

* Single-threaded – no locking required
* File I/O failures fall back to defaults (settings / scores)
* Renderer safety caps (fan-out) prevent runaway ray counts
* All new settings additions are backward compatible (missing keys → defaults)

---

## 8. Performance Reference (Indicative)

* Core update: microseconds-level per frame (low complexity)
* Path tracer cost dominated by raysPerFrame * bounces
* Accumulation & denoise O(pixels)
* Memory footprint small (< a few MB for internal buffers)

---

## 9. Extension Checklist

| Extension | Steps |
|-----------|-------|
| New Setting | Add field → clamp in load → expose in UI → use in subsystem |
| New Mode | Enum + switch branches (spawn/reset + update) + UI cycle |
| New Renderer | Implement class, integrate selection/persistence, adapt HUD |
| Export Frames | Hook after `render()` before HUD, write pixels buffer |
| Replay System | Record inputs or GameState per frame; feed back deterministically |

---

## 10. Debug Tips

* Use console build to validate physics independent of rendering complexity
* Temporarily force physics_mode to compare bounce behavior
* Track ball/obstacle counts when adding new spawn logic
* Inspect `SRStats` to tune ray budgets

---

This reference captures the current public and semi-public surfaces of PongCpp focusing on extendability and clarity.
