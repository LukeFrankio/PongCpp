/**
 * @file highscores.h
 * @brief High score tracking and persistence for Windows GUI version
 * 
 * This file defines structures and classes for managing player high scores
 * with persistent storage in JSON format.
 */

#pragma once
#include <string>
#include <vector>

/**
 * @brief Single high score entry
 * 
 * Represents one entry in the high score table with player name and score.
 */
struct HighScoreEntry { 
    std::wstring name;  ///< Player name (Unicode support)
    int score;          ///< Player's score
};

/**
 * @brief High score management class
 * 
 * Handles loading, saving, and updating high score lists with automatic
 * sorting and persistence to JSON files. Maintains a configurable number
 * of top scores and provides methods for adding new entries.
 */
class HighScores {
public:
    /**
     * @brief Default constructor
     */
    HighScores() = default;
    
    /**
     * @brief Load high scores from JSON file
     * 
     * Reads and parses high scores from the specified file. Returns
     * an empty list if the file doesn't exist or cannot be parsed.
     * Results are automatically sorted by score in descending order.
     * 
     * @param path Wide string path to the high scores JSON file
     * @param maxEntries Maximum number of entries to load (default: 10)
     * @return Vector of HighScoreEntry objects sorted by score
     */
    std::vector<HighScoreEntry> load(const std::wstring &path, size_t maxEntries = 10);
    
    /**
     * @brief Save high scores to JSON file
     * 
     * Serializes and writes the high score list to a JSON file.
     * The list should already be sorted in the desired order.
     * 
     * @param path Wide string path where high scores should be saved
     * @param list Vector of HighScoreEntry objects to save
     * @return true if save was successful, false on error
     */
    bool save(const std::wstring &path, const std::vector<HighScoreEntry> &list);
    
    /**
     * @brief Add new score and return updated list
     * 
     * Convenience method that loads existing scores, adds a new entry,
     * sorts the list, trims to maximum entries, saves to disk, and
     * returns the updated list. This is the primary method for
     * recording new high scores.
     * 
     * @param path Wide string path to the high scores JSON file
     * @param name Player name for the new entry
     * @param score Player's score
     * @param maxEntries Maximum entries to keep (default: 10)
     * @return Updated and sorted high score list
     */
    std::vector<HighScoreEntry> add_and_get(const std::wstring &path, const std::wstring &name, int score, size_t maxEntries = 10);
};
