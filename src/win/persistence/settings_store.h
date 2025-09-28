/**
 * @file settings_store.h
 * @brief Settings persistence and management
 * 
 * This file defines the SettingsStore class which handles loading,
 * saving, and managing application settings persistence.
 */

#pragma once
#include <string>

struct Settings;
struct SettingsDelta;

/**
 * @brief Persistent settings storage and management
 * 
 * SettingsStore manages the application settings lifecycle including
 * loading from disk, applying changes, and saving back to persistent
 * storage. It provides a centralized interface for settings management.
 */
class SettingsStore { 
public: 
    /**
     * @brief Get current settings
     * 
     * Returns the current settings configuration.
     * 
     * @return Const reference to current settings
     */
    const Settings& get() const; 
    
    /**
     * @brief Apply settings changes
     * 
     * Applies a delta of settings changes to the current configuration.
     * 
     * @param delta Settings changes to apply
     */
    void apply(const SettingsDelta& delta); 
    
    /**
     * @brief Load settings from file
     * 
     * Loads settings from the specified file path. Creates default
     * settings if the file doesn't exist.
     * 
     * @param path Path to settings file
     * @return true if loading succeeded, false otherwise
     */
    bool load(const std::wstring& path); 
    
    /**
     * @brief Save settings to file
     * 
     * Saves current settings to the specified file path.
     * 
     * @param path Path to save settings file
     * @return true if saving succeeded, false otherwise
     */
    bool save(const std::wstring& path); 
    
private: 
    Settings* ptr = nullptr;  ///< Current settings instance
};
