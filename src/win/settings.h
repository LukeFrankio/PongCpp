/**
 * @file settings.h
 * @brief Settings persistence for Windows GUI version
 * 
 * This file defines the Settings structure and SettingsManager class
 * for saving and loading game configuration in JSON format.
 */

#pragma once
#include <string>

/**
 * @brief Game settings structure
 * 
 * Contains all configurable game options that can be persisted
 * to disk and restored between game sessions.
 */
struct Settings {
    int recording_mode = 0; ///< Recording mode: 0=off, 1=on (record at fixed 60fps)
    int control_mode = 1;  ///< Control mode: 0=keyboard, 1=mouse
    int ai = 1;           ///< AI difficulty: 0=easy, 1=normal, 2=hard
    int renderer = 0;     ///< 0=classic GDI, 1=path tracer
    int quality = 1;      ///< Deprecated quality preset (legacy)
    int game_mode = 0;    ///< Game mode: 0=Classic,1=ThreeEnemies,2=Obstacles,3=MultiBall
    // New path tracer parameter sliders (persisted as ints for simplicity)
    int pt_rays_per_frame = 10;     ///< Total primary rays per frame (distributed over render target)
    int pt_max_bounces = 1;           ///< Maximum bounces (1-8 reasonable)
    int pt_internal_scale = 10;       ///< Internal resolution percentage (25..100)
    int pt_roughness = 15;            ///< Metallic roughness percent (0..100)
    int pt_emissive = 100;            ///< Emissive intensity percent for ball (1..5000)
    int pt_paddle_emissive = 0;       ///< Emissive intensity percent for paddles (0..5000, 0=no emission)
    int pt_accum_alpha = 75;          ///< Temporal accumulation alpha percent (1..50 => 0.01..0.50)
    int pt_denoise_strength = 25;     ///< Spatial denoise strength percent (0..100)
    int pt_force_full_pixel_rays = 1; ///< 1 = force at least 1 primary ray per pixel at internal resolution
    int pt_use_ortho = 0;             ///< 1 = use orthographic camera, 0 = perspective
    // Russian roulette settings (percent/min probability stored as scaled ints for simplicity)
    int pt_rr_enable = 1;             ///< 1 = enable Russian roulette termination
    int pt_rr_start_bounce = 2;       ///< Bounce index at or after which roulette starts (1..16)
    int pt_rr_min_prob_pct = 10;      ///< Minimum survival probability percent (e.g. 10 => 0.10)
    // Experimental combinatorial fan-out (dangerous)
    int pt_fanout_enable = 0;         ///< 1 = enable exponential fan-out mode
    int pt_fanout_cap = 2000000;      ///< Safety cap for total rays
    int pt_fanout_abort = 1;          ///< 1 = abort when cap exceeded, 0 = continue (may freeze)
    // Segment tracer (2D GI) alternative renderer
    // Soft shadows / PBR additions
    int pt_soft_shadow_samples = 4;    ///< Soft shadow samples per light (1..64) mapped directly
    int pt_light_radius_pct = 100;     ///< Light radius scale percent (10..500 => 0.1x .. 5.0x)
    int pt_pbr_enable = 1;             ///< Enable PBR energy terms (1=on,0=off)
    // Gameplay / meta
    int player_mode = 0;               ///< 0=1P vs AI, 1=2 Players, 2=AI vs AI
    // Recording
    int recording_fps = 60;            ///< Target recording FPS (15..60)
    // Physics / HUD
    int physics_mode = 1;              ///< 0=Arcade physics, 1=Physically-based paddle bounce
    int speed_mode = 0;                ///< 1="I am Speed" mode: no max speed, auto-acceleration
    int hud_show_play = 1;             ///< 1=Show HUD during normal gameplay
    int hud_show_record = 1;           ///< 1=Show HUD overlays while recording
};

/**
 * @brief Manager class for settings persistence
 * 
 * Handles loading and saving game settings to/from JSON files.
 * Uses a simple custom JSON parser/serializer to avoid external
 * dependencies while providing human-readable configuration files.
 */
class SettingsManager {
public:
    /**
     * @brief Default constructor
     */
    SettingsManager() = default;
    
    /**
     * @brief Load settings from JSON file
     * 
     * Reads settings from the specified file path. If the file
     * doesn't exist or cannot be parsed, returns default settings.
     * 
     * @param path Wide string path to the settings JSON file
     * @return Settings structure with loaded or default values
     */
    Settings load(const std::wstring &path);
    
    /**
     * @brief Save settings to JSON file
     * 
     * Writes the current settings to a JSON file at the specified
     * path. Creates the file if it doesn't exist.
     * 
     * @param path Wide string path where settings should be saved
     * @param s Settings structure to serialize and save
     * @return true if save was successful, false on error
     */
    bool save(const std::wstring &path, const Settings &s);
};
