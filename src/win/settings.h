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
    int control_mode = 1;  ///< Control mode: 0=keyboard, 1=mouse
    int ai = 1;           ///< AI difficulty: 0=easy, 1=normal, 2=hard
    int renderer = 0;     ///< 0=classic GDI, 1=path tracer
    int quality = 1;      ///< Deprecated quality preset (legacy)
    // New path tracer parameter sliders (persisted as ints for simplicity)
    int pt_rays_per_frame = 10;     ///< Total primary rays per frame (distributed over render target)
    int pt_max_bounces = 4;           ///< Maximum bounces (1-8 reasonable)
    int pt_internal_scale = 10;       ///< Internal resolution percentage (25..100)
    int pt_roughness = 15;            ///< Metallic roughness percent (0..100)
    int pt_emissive = 100;            ///< Emissive intensity percent for ball (50..300 mapped)
    int pt_accum_alpha = 75;          ///< Temporal accumulation alpha percent (1..50 => 0.01..0.50)
    int pt_denoise_strength = 25;     ///< Spatial denoise strength percent (0..100)
    int pt_force_full_pixel_rays = 1; ///< 1 = force at least 1 primary ray per pixel at internal resolution
    int pt_use_ortho = 1;             ///< 1 = use orthographic camera, 0 = perspective
    // Russian roulette settings (percent/min probability stored as scaled ints for simplicity)
    int pt_rr_enable = 1;             ///< 1 = enable Russian roulette termination
    int pt_rr_start_bounce = 2;       ///< Bounce index at or after which roulette starts (1..16)
    int pt_rr_min_prob_pct = 10;      ///< Minimum survival probability percent (e.g. 10 => 0.10)
    // Experimental combinatorial fan-out (dangerous)
    int pt_fanout_enable = 0;         ///< 1 = enable exponential fan-out mode
    int pt_fanout_cap = 2000000;      ///< Safety cap for total rays
    int pt_fanout_abort = 1;          ///< 1 = abort when cap exceeded, 0 = continue (may freeze)
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
