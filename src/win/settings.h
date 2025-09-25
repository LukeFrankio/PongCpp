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
    int control_mode = 0;  ///< Control mode: 0=keyboard, 1=mouse
    int ai = 1;           ///< AI difficulty: 0=easy, 1=normal, 2=hard
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
