# Settings System Documentation

## Overview

The PongCpp settings system provides persistent configuration storage for the Windows GUI version. It uses a human-readable JSON format with a lightweight custom parser to avoid external dependencies. Settings are automatically saved and loaded between game sessions.

## Architecture

### Core Components

```text
src/win/settings.h              # Settings struct + SettingsManager class
src/win/settings.cpp             # JSON serialization/deserialization
src/win/ui/settings_panel.cpp   # Interactive UI for path tracer settings
src/win/rendering/pt_renderer_adapter.cpp  # Settings → renderer config conversion
```

### Data Flow

```text
JSON File (settings.json)
    ↓ load
Settings struct (integer percentages)
    ↓ conversion (/100.0f)
SRConfig struct (float values)
    ↓ configure()
SoftRenderer (path tracer state)
```

### Key Design Principles

1. **Integer Storage**: All settings stored as `int` for simplicity (percentages, boolean 0/1, etc.)
2. **Separation of Concerns**: UI settings (integers) separate from renderer config (floats)
3. **Backward Compatibility**: Missing fields in JSON use default values from struct initialization
4. **No External Dependencies**: Custom JSON parser using `std::ifstream` and string parsing
5. **Immediate Validation**: Settings clamped/validated both on load and before renderer application

## File Structure

### settings.json Format

Located next to the executable (`pong_win.exe`), created on first save:

```json
{
  "control_mode": 1,
  "ai": 1,
  "renderer": 1,
  "game_mode": 0,
  "pt_rays_per_frame": 10,
  "pt_max_bounces": 3,
  "pt_internal_scale": 50,
  "pt_roughness": 15,
  "pt_emissive": 150,
  "pt_paddle_emissive": 100,
  "pt_accum_alpha": 75,
  "pt_denoise_strength": 25,
  "pt_force_full_pixel_rays": 1,
  "pt_use_ortho": 0,
  "pt_rr_enable": 1,
  "pt_rr_start_bounce": 2,
  "pt_rr_min_prob_pct": 10,
  "pt_fanout_enable": 0,
  "pt_fanout_cap": 2000000,
  "pt_fanout_abort": 1,
  "pt_soft_shadow_samples": 4,
  "pt_light_radius_pct": 100,
  "pt_pbr_enable": 1,
  "recording_mode": 0,
  "player_mode": 0,
  "recording_fps": 60,
  "physics_mode": 1,
  "speed_mode": 0,
  "hud_show_play": 1,
  "hud_show_record": 1
}
```

## Adding a New Setting

### Step-by-Step Guide

Follow these steps to add a new setting (example: adding `pt_paddle_emissive`):

#### 1. Add Field to Settings Struct

**File**: `src/win/settings.h`

Add the new field with default value, documentation comment, and appropriate units:

```cpp
struct Settings {
    // ... existing fields ...
    
    int pt_emissive = 100;            ///< Emissive intensity percent for ball (1..5000)
    int pt_paddle_emissive = 0;       ///< Emissive intensity percent for paddles (0..5000, 0=no emission)  // NEW
    
    // ... more fields ...
};
```

**Naming Convention**:

- Path tracer settings: `pt_*`
- Boolean toggles: `*_enable` or plain `*_mode`
- Percentages: `*_pct` suffix (optional, used for clarity)
- Counts/integers: descriptive name like `*_samples`, `*_bounces`

**Default Values**:

- Choose sensible defaults that work for most users
- Consider backward compatibility (0 = off for optional features)
- Document valid ranges in comment

#### 2. Add Load Logic

**File**: `src/win/settings.cpp` → `Settings SettingsManager::load()`

Add extraction after existing `extractInt` calls:

```cpp
Settings SettingsManager::load(const std::wstring &path) {
    Settings s; // defaults
    // ... file loading code ...
    
    extractInt("pt_emissive", s.pt_emissive);
    extractInt("pt_paddle_emissive", s.pt_paddle_emissive);  // NEW
    
    // ... more fields ...
    
    // Optional: Add validation/clamping
    if(s.pt_paddle_emissive < 0) s.pt_paddle_emissive = 0;
    if(s.pt_paddle_emissive > 5000) s.pt_paddle_emissive = 5000;
    
    return s;
}
```

**Note**: The `extractInt` lambda handles missing keys gracefully (keeps default value).

#### 3. Add Save Logic

**File**: `src/win/settings.cpp` → `bool SettingsManager::save()`

Add serialization line in alphabetical/logical order:

```cpp
bool SettingsManager::save(const std::wstring &path, const Settings &s) {
    // ... open file ...
    
    ofs << "  \"pt_emissive\": " << s.pt_emissive << ",\n";
    ofs << "  \"pt_paddle_emissive\": " << s.pt_paddle_emissive << ",\n";  // NEW
    
    // ... more fields ...
    // IMPORTANT: Last field must NOT have trailing comma!
    
    ofs << "}\n";
    return true;
}
```

**JSON Syntax Rules**:

- All fields except the last need trailing comma
- Use consistent indentation (2 spaces)
- Boolean values: `0` or `1` (not `true`/`false`)

#### 4. Add UI Control (Optional)

**File**: `src/win/ui/settings_panel.cpp` → `SettingsPanel::frame()`

##### For Slider Controls

Add to `SliderInfo sliders[]` array around line 56:

```cpp
SliderInfo sliders[] = {
    {L"Rays / Frame", &settings_->pt_rays_per_frame, 1, 1000, 1},
    {L"Ball Emissive %", &settings_->pt_emissive, 1, 5000, 1},
    {L"Paddle Emissive %", &settings_->pt_paddle_emissive, 0, 5000, 1},  // NEW
    // ... more sliders ...
};
```

**SliderInfo Parameters**:

- `label`: Wide string label shown in UI
- `val`: Pointer to setting field (`&settings_->field_name`)
- `minv`: Minimum value (inclusive)
- `maxv`: Maximum value (inclusive)
- `step`: Increment/decrement step size

##### Add Tooltip (Optional)

Update `tooltipForIndex` function around line 120:

```cpp
case 4: return L"Ball Emissive %: Ball light intensity (1-5000).";
case 5: return L"Paddle Emissive %: Paddle light intensity (0-5000, 0=off).";  // NEW
```

##### For Toggle/Checkbox Controls

If adding a boolean toggle (like `pt_use_ortho`), requires more code:

1. Add index calculation function:

    ```cpp
    // Around line 40
    int idxMyToggle_() const { return kBaseSliderCount + 5; }
    ```

2. Draw the toggle:

    ```cpp
    int cyMyToggle = baseY + (kBaseSliderCount+5)*rowH;
    bool myToggleHot = (sel_==idxMyToggle_());
    drawCenterLine(std::wstring(L"My Feature: ") + 
                (settings_->my_feature?L"ON":L"OFF"), 
                cyMyToggle, myToggleHot);
    ```

3. Handle keyboard input:

    ```cpp
    else if (sel_==idxMyToggle_()) {
        if (input.just_pressed(VK_LEFT) || input.just_pressed(VK_RIGHT)) {
            settings_->my_feature = settings_->my_feature?0:1;
            changedSinceOpen_ = true;
        }
    }
    ```

4. Handle mouse click:

    ```cpp
    else if (cy < panelTop && hitMid(cyMyToggle)) {
        settings_->my_feature = settings_->my_feature?0:1;
        sel_=idxMyToggle_();
        changedSinceOpen_=true;
    }
    ```

#### 5. Update Renderer Config (If Renderer-Related)

**File**: `src/win/soft_renderer.h`

Add corresponding field to `SRConfig` struct (use actual types, not percentages):

```cpp
struct SRConfig {
    // ... existing fields ...
    float emissiveIntensity = 1.0f;         // Ball emission multiplier
    float paddleEmissiveIntensity = 0.0f;   // Paddle emission multiplier (NEW)
    // ... more fields ...
};
```

**Type Conversion Guidelines**:

- Percentages (0-100) → divide by 100 → float (0.0-1.0)
- Percentages (0-5000) → divide by 100 → float (0.0-50.0)
- Boolean int (0/1) → compare `!= 0` → bool
- Direct integers → cast to appropriate type

#### 6. Add Config Conversion

**File**: `src/win/rendering/pt_renderer_adapter.cpp` → `applySettings()`

Add conversion around line 16:

```cpp
static void applySettings(SoftRenderer* r, SRConfig& cur, const Settings& s){
    bool changed=false;
    auto apply=[&](auto &dst, auto v){ if(dst!=v){ dst=v; changed=true; }};
    
    apply(cur.emissiveIntensity, s.pt_emissive/100.0f);
    apply(cur.paddleEmissiveIntensity, s.pt_paddle_emissive/100.0f);  // NEW
    
    // ... more conversions ...
    
    // Optional: Add defensive clamping for corrupted values
    if(cur.paddleEmissiveIntensity < 0.0f){ 
        cur.paddleEmissiveIntensity = 0.0f; 
        changed=true; 
    }
    
    if(changed){ r->configure(cur); r->resetHistory(); }
}
```

**Why `resetHistory()`?**: When configuration changes, the temporal accumulation buffer contains samples from the old config, so it must be cleared.

#### 7. Use Setting in Renderer

**File**: `src/win/soft_renderer.cpp`

Access via `config` parameter passed to `render()`:

```cpp
void SoftRenderer::render(const GameState& gs) {
    // ... setup code ...
    
    float ballEmission = config.emissiveIntensity;
    float paddleEmission = config.paddleEmissiveIntensity;  // NEW
    
    // Use in rendering logic:
    if (config.paddleEmissiveIntensity > 0.0f) {
        // Paddle emission code...
    }
}
```

#### 8. Update Reset Defaults (Optional)

**File**: `src/win/ui/settings_panel.cpp` → `SettingsPanel::resetDefaults()`

Add reset logic around line 14:

```cpp
void SettingsPanel::resetDefaults() {
    if (!settings_) return;
    settings_->pt_emissive = 100;
    settings_->pt_paddle_emissive = 0;  // NEW
    // ... more resets ...
    changedSinceOpen_ = true;
}
```

### Complete Example: Adding Ball Emissivity Limit Increase

Here's a real example showing how the ball emissive limit was increased from 1000 to 5000:

**1. Update max in settings_panel.cpp (SliderInfo array)**:

```cpp
{L"Ball Emissive %", &settings_->pt_emissive, 1, 5000, 1},  // Changed 1000 → 5000
```

**2. Update comment in settings.h**:

```cpp
int pt_emissive = 100;  ///< Emissive intensity percent for ball (1..5000)  // Updated doc
```

That's it! No other changes needed because:

- Load/save already handled the field
- Conversion code already divided by 100
- UI automatically used new max value

### Complete Example: Adding Separate Paddle Emission

Real example showing how `pt_paddle_emissive` was added (more complex, new feature):

**1. settings.h** - Added field:

```cpp
int pt_paddle_emissive = 0;  ///< Emissive intensity percent for paddles (0..5000, 0=no emission)
```

**2. settings.cpp** - Added load:

```cpp
extractInt("pt_paddle_emissive", s.pt_paddle_emissive);
```

**3. settings.cpp** - Added save:

```cpp
ofs << "  \"pt_paddle_emissive\": " << s.pt_paddle_emissive << ",\n";
```

**4. settings_panel.cpp** - Added slider:

```cpp
{L"Paddle Emissive %", &settings_->pt_paddle_emissive, 0, 5000, 1},
```

**5. settings_panel.cpp** - Added tooltip:

```cpp
case 5: return L"Paddle Emissive %: Paddle light intensity (0-5000, 0=off).";
```

**6. soft_renderer.h** - Added config field:

```cpp
float paddleEmissiveIntensity = 0.0f;
```

**7. pt_renderer_adapter.cpp** - Added conversion:

```cpp
apply(cur.paddleEmissiveIntensity, s.pt_paddle_emissive/100.0f);
```

**8. soft_renderer.cpp** - Used in rendering (multiple locations):

```cpp
// Area light system (~line 808):
if (config.paddleEmissiveIntensity > 0.0f) {
    // Populate paddle lights...
}

// Direct emission (~line 1089, 1416):
if (config.paddleEmissiveIntensity > 0.0f) {
    Vec3 paddleEmit{2.2f,1.4f,0.8f};
    paddleEmit = paddleEmit * config.paddleEmissiveIntensity;
    // Emit light and terminate path...
}

// Occlusion testing (~line 863):
if (config.paddleEmissiveIntensity <= 0.0f) {
    // Only treat paddles as occluders when NOT emissive...
}
```

## Common Patterns

### Boolean Toggles

**Storage**: `int` (0 = off, 1 = on)

```cpp
// settings.h
int my_feature_enable = 1;  ///< 1 = feature enabled

// settings.cpp (load)
extractInt("my_feature_enable", s.my_feature_enable);
if(s.my_feature_enable != 0) s.my_feature_enable = 1;  // Normalize

// settings.cpp (save)
ofs << "  \"my_feature_enable\": " << s.my_feature_enable << ",\n";

// pt_renderer_adapter.cpp
apply(cur.myFeatureEnable, s.my_feature_enable != 0);  // Convert to bool
```

### Percentage Values (0-100)

**Storage**: `int` (0-100)  
**Usage**: `float` (0.0-1.0)

```cpp
// settings.h
int my_strength = 50;  ///< Strength percent (0..100)

// pt_renderer_adapter.cpp
apply(cur.myStrength, s.my_strength / 100.0f);
```

### Extended Percentages (0-5000)

**Storage**: `int` (0-5000)  
**Usage**: `float` (0.0-50.0)

```cpp
// settings.h  
int my_intensity = 100;  ///< Intensity percent (0..5000)

// pt_renderer_adapter.cpp
apply(cur.myIntensity, s.my_intensity / 100.0f);
```

### Integer Ranges

**Storage**: `int` (direct value)  
**Usage**: `int` (direct, possibly with clamping)

```cpp
// settings.h
int my_samples = 4;  ///< Sample count (1..64)

// pt_renderer_adapter.cpp
apply(cur.mySamples, s.my_samples);

// Optional validation
if(cur.mySamples < 1) cur.mySamples = 1;
if(cur.mySamples > 64) cur.mySamples = 64;
```

### Enumeration Values

**Storage**: `int` (0, 1, 2, ...)  
**Usage**: `enum` or `int` with semantic meaning

```cpp
// settings.h
int renderer = 0;  ///< 0=classic GDI, 1=path tracer

// Usage in code
if (settings.renderer == 0) {
    // Use classic renderer
} else if (settings.renderer == 1) {
    // Use path tracer
}
```

## Backward Compatibility

### Handling Missing Fields

The custom JSON parser gracefully handles missing fields:

```cpp
extractInt("new_field", s.new_field);
// If "new_field" not in JSON → s.new_field keeps default from struct initialization
```

**Best Practice**: Always provide sensible defaults in struct initialization:

```cpp
struct Settings {
    int new_feature = 0;  // Default to OFF for new features
    int new_quality = 50; // Default to middle value for new sliders
};
```

### Removing Deprecated Fields

When removing old settings:

1. **Keep load code** (ignores unknown fields, no harm):

```cpp
extractInt("old_deprecated_field", s.old_field);  // Can keep indefinitely
```

1. **Remove from save code** (prevents writing to new files)
2. **Remove from Settings struct**
3. **Remove from UI**

This allows old `settings.json` files to load without errors.

### Version Migration

Currently no version field exists. If breaking changes needed:

```cpp
// settings.h
int version = 1;  ///< Settings format version

// settings.cpp (load)
extractInt("version", s.version);
if (s.version < 2) {
    // Migrate old format...
}
```

## Validation Strategy

### Three-Layer Validation

1. **Struct Initialization** (defaults):

    ```cpp
    int pt_samples = 4;  // Valid default
    ```

2. **Load-Time Validation** (`settings.cpp`):

    ```cpp
    if(s.pt_samples < 1) s.pt_samples = 1;
    if(s.pt_samples > 64) s.pt_samples = 64;
    ```

3. **Pre-Render Validation** (`pt_renderer_adapter.cpp`):

    ```cpp
    if(cur.samples < 1){ cur.samples = 1; changed=true; }
    if(cur.samples > 64){ cur.samples = 64; changed=true; }
    ```

**Why Three Layers?**

- Struct defaults: Handle first launch (no file)
- Load validation: Handle corrupted/manual edits
- Render validation: Safety net for code bugs

### Common Validation Patterns

```cpp
// Clamp to range
if(val < min) val = min;
if(val > max) val = max;

// Normalize boolean
if(flag != 0) flag = 1;

// Ensure positive
if(val <= 0) val = 1;

// Percentage to float with safety
float ratio = s.percent / 100.0f;
if(ratio < 0.0f) ratio = 0.0f;
if(ratio > 1.0f) ratio = 1.0f;
```

## Performance Considerations

### When Settings Change

When `configure()` detects changes in `pt_renderer_adapter.cpp`:

1. Calls `r->configure(cur)` - Updates internal config
2. Calls `r->resetHistory()` - **Clears temporal accumulation buffer**

**Impact**: Frame accumulation restarts, image temporarily noisy

**Mitigation**: Only call when actually changed (handled by `apply` lambda):

```cpp
auto apply=[&](auto &dst, auto v){ if(dst!=v){ dst=v; changed=true; }};
```

### Save Frequency

- Settings saved only on explicit "Save" button click
- **Not** saved automatically on every change
- Allows experimentation without persistence

### Load Frequency  

- Loaded once at application startup (`game_win.cpp`)
- Can be manually reloaded by closing/reopening settings panel

## Debugging

### Common Issues

**1. Setting not persisting**:

- Check save code has correct field name (must match load)
- Verify JSON syntax (commas, quotes)
- Check if save button actually clicked (vs just closing panel)

**2. Setting ignored by renderer**:

- Verify conversion in `pt_renderer_adapter.cpp`
- Check if `SRConfig` field added
- Ensure `resetHistory()` called when changed

**3. UI slider not responding**:

- Verify pointer in `SliderInfo` array: `&settings_->field`
- Check min/max values reasonable
- Ensure `changedSinceOpen_ = true` set

**4. Corrupted JSON file**:

- Delete `settings.json` → app creates new with defaults
- Check for missing commas, extra commas on last field
- Verify all values are integers (not floats like `1.5`)

### Debug Logging

Add temporary logging to verify setting usage:

```cpp
// In soft_renderer.cpp
#ifdef _DEBUG
char buf[256];
sprintf_s(buf, "Paddle emission: %.2f\n", config.paddleEmissiveIntensity);
OutputDebugStringA(buf);
#endif
```

View with Visual Studio Output window or DebugView tool.

## Testing Checklist

When adding a new setting:

- [ ] Add to `Settings` struct with default and doc comment
- [ ] Add to `load()` with `extractInt` call
- [ ] Add to `save()` with proper JSON formatting
- [ ] Add to UI (slider/toggle) in `settings_panel.cpp`
- [ ] Add tooltip text explaining the setting
- [ ] Add to `SRConfig` if renderer-related
- [ ] Add conversion in `applySettings()`
- [ ] Use in renderer code (`soft_renderer.cpp`)
- [ ] Update `resetDefaults()` if non-zero default
- [ ] Test: Create new settings file (delete old)
- [ ] Test: Load settings from existing file
- [ ] Test: Modify setting in UI, verify visual change
- [ ] Test: Save settings, restart app, verify persistence
- [ ] Test: Manually edit JSON with invalid value, verify clamping
- [ ] Test: Remove field from JSON, verify default used

## Settings Reference

### Core Settings

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `control_mode` | int | 0-1 | 1 | 0=keyboard, 1=mouse |
| `ai` | int | 0-2 | 1 | AI difficulty (0=easy, 1=normal, 2=hard) |
| `renderer` | int | 0-1 | 0 | 0=classic GDI, 1=path tracer |
| `game_mode` | int | 0-3 | 0 | 0=Classic, 1=ThreeEnemies, 2=Obstacles, 3=MultiBall |
| `player_mode` | int | 0-2 | 0 | 0=1P vs AI, 1=2 Players, 2=AI vs AI |
| `physics_mode` | int | 0-1 | 1 | 0=Arcade, 1=Physically-based paddle bounce |
| `speed_mode` | int | 0-1 | 0 | 1="I am Speed" mode (no max speed) |

### Path Tracer Settings

| Field | Type | Range | Default | Description | Units |
|-------|------|-------|---------|-------------|-------|
| `pt_rays_per_frame` | int | 1-1000 | 10 | Primary rays per frame | count |
| `pt_max_bounces` | int | 1-8 | 1 | Maximum path bounces | count |
| `pt_internal_scale` | int | 1-100 | 10 | Internal resolution | percent |
| `pt_roughness` | int | 0-100 | 15 | Metallic roughness | percent |
| `pt_emissive` | int | 1-5000 | 100 | Ball emission intensity | percent (1.0-50.0x) |
| `pt_paddle_emissive` | int | 0-5000 | 0 | Paddle emission intensity (0=off) | percent |
| `pt_accum_alpha` | int | 0-100 | 75 | Temporal accumulation blend | percent |
| `pt_denoise_strength` | int | 0-100 | 25 | Spatial denoise (3x3 blur) | percent |
| `pt_soft_shadow_samples` | int | 1-64 | 4 | Samples per light | count |
| `pt_light_radius_pct` | int | 10-500 | 100 | Light radius scale | percent |
| `pt_force_full_pixel_rays` | int | 0-1 | 1 | Minimum 1 ray per pixel | boolean |
| `pt_use_ortho` | int | 0-1 | 0 | Orthographic camera | boolean |
| `pt_pbr_enable` | int | 0-1 | 1 | Physically-based rendering | boolean |

### Russian Roulette Settings

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `pt_rr_enable` | int | 0-1 | 1 | Enable path termination |
| `pt_rr_start_bounce` | int | 1-16 | 2 | Bounce to start termination |
| `pt_rr_min_prob_pct` | int | 1-90 | 10 | Minimum survival probability |

### Experimental Fan-Out (Dangerous)

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `pt_fanout_enable` | int | 0-1 | 0 | Combinatorial ray explosion mode |
| `pt_fanout_cap` | int | 1000-10M | 2M | Maximum total rays safety limit |
| `pt_fanout_abort` | int | 0-1 | 1 | Abort when cap exceeded |

### HUD & Recording

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `recording_mode` | int | 0-1 | 0 | Enable recording mode |
| `recording_fps` | int | 15-60 | 60 | Recording frame rate |
| `hud_show_play` | int | 0-1 | 1 | Show HUD during gameplay |
| `hud_show_record` | int | 0-1 | 1 | Show HUD while recording |

### Legacy/Deprecated

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `quality` | int | 0-2 | 1 | Deprecated quality preset (ignored) |

## Advanced Topics

### Custom JSON Parser

The settings system uses a minimal custom JSON parser (`extractInt` lambda):

**Advantages**:

- No external dependencies (stdlib only)
- Simple and maintainable (~10 lines of code)
- Sufficient for integer-only settings

**Limitations**:

- Integers only (no floats, strings, arrays, nested objects)
- Whitespace-tolerant but not fully JSON-compliant
- No error reporting for malformed JSON

**When to Upgrade**:

If you need complex settings (strings, nested objects, arrays), consider:

- [nlohmann/json](https://github.com/nlohmann/json) - Header-only C++ JSON library
- [rapidjson](https://rapidjson.org/) - Fast JSON parser

### Thread Safety

**Current State**: Settings are **not thread-safe**.

**Usage Pattern** (currently safe):

- UI thread modifies settings via `settings_panel.cpp`
- Render thread reads settings via `pt_renderer_adapter.cpp`
- Settings only updated between frames (not during rendering)

**Future Considerations**:
If adding concurrent access, wrap Settings with mutex:

```cpp
std::mutex settingsMutex;
std::lock_guard<std::mutex> lock(settingsMutex);
```

### Settings Templates/Presets

Not currently implemented, but could add:

```cpp
// settings.h
struct SettingsPreset {
    const wchar_t* name;
    Settings values;
};

const SettingsPreset PRESETS[] = {
    {L"Performance", Settings{/*.pt_rays_per_frame=1, ...*/}},
    {L"Balanced",    Settings{/*.pt_rays_per_frame=10, ...*/}},
    {L"Quality",     Settings{/*.pt_rays_per_frame=100, ...*/}},
};
```

## Further Reading

- **Source Code**: Start with `settings.h` for complete field list
- **UI Implementation**: See `settings_panel.cpp` for interaction patterns  
- **Rendering Usage**: Check `soft_renderer.cpp` for practical examples
- **Architecture**: Read `docs/developer/architecture.md` for system overview

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2025-10-02 | 1.0 | Initial documentation |
