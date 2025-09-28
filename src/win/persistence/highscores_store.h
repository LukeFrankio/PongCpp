/**
 * @file highscores_store.h
 * @brief High score persistence and management
 * 
 * This file defines the HighScoresStore class which handles loading,
 * saving, and managing player high scores.
 */

#pragma once
#include <vector>
#include <string>

struct HighScoreEntry;

/**
 * @brief High score storage and management
 * 
 * HighScoresStore manages the high score system including loading
 * scores from disk, adding new scores, sorting, and persistence.
 * It maintains the top scores and handles file I/O operations.
 */
class HighScoresStore { 
public: 
    /**
     * @brief Get the current high score list
     * 
     * Returns the list of high score entries sorted by score.
     * 
     * @return Const reference to high score entries vector
     */
    const std::vector<HighScoreEntry>& list() const; 
    
    /**
     * @brief Add a new high score entry
     * 
     * Adds a new score entry with the given name and score.
     * The list is automatically sorted and trimmed to maintain
     * only the top scores.
     * 
     * @param name Player name for the score entry
     * @param score Score value to add
     */
    void add(const std::wstring& name, int score); 
    
    /**
     * @brief Load high scores from file
     * 
     * Loads high scores from the specified file path. Creates
     * an empty list if the file doesn't exist.
     * 
     * @param path Path to high scores file
     * @return true if loading succeeded, false otherwise
     */
    bool load(const std::wstring& path); 
    
private: 
    std::vector<HighScoreEntry> entries;  ///< List of high score entries
};
