/**
 * @file game_mode_settings_view.h
 * @brief Game mode customization UI
 * 
 * Provides an interactive settings screen where users can toggle
 * individual game mode features instead of selecting predefined modes.
 */

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../game_mode_config.h"

struct InputState;

/**
 * @brief Game mode settings view for customizing game features
 * 
 * This view replaces the simple game mode dropdown with a comprehensive
 * settings screen allowing users to enable/disable individual features:
 * - MultiBall (with ball count)
 * - Obstacles (static or moving)
 * - Black Holes (static or moving, with count)
 * - Three Enemies mode
 */
class GameModeSettingsView {
public:
    enum class Action { None, Commit, Cancel };
    
    /**
     * @brief Initialize the view with current configuration
     * 
     * @param config Pointer to the game mode configuration to edit
     */
    void begin(GameModeConfig* config);
    
    /**
     * @brief Render one frame of the settings view
     * 
     * @param memDC Device context to draw into
     * @param winW Window width in pixels
     * @param winH Window height in pixels
     * @param dpi Current DPI setting
     * @param input Current input state
     * @param mouse_x Mouse X coordinate
     * @param mouse_y Mouse Y coordinate
     * @param mouse_pressed Whether mouse button is pressed
     * @param mouse_wheel_delta Mouse wheel delta (consumed by this function)
     * @param last_click_x Last click X coordinate
     * @param last_click_y Last click Y coordinate
     * @return Action indicating whether to commit or cancel
     */
    Action frame(HDC memDC,
                 int winW, int winH, int dpi,
                 const InputState& input,
                 int mouse_x, int mouse_y, bool mouse_pressed,
                 int& mouse_wheel_delta,
                 int& last_click_x, int& last_click_y);
    
    /**
     * @brief Check if any changes were made
     */
    bool anyChangesSinceOpen() const { return changedSinceOpen_; }

private:
    GameModeConfig* config_ = nullptr;
    GameModeConfig original_;
    bool changedSinceOpen_ = false;
    int sel_ = 0;
    int scrollOffset_ = 0;
    int maxScroll_ = 0;
    
    // UI item indices
    int idxMultiball_() const { return 0; }
    int idxMultiballCount_() const { return 1; }
    int idxObstacles_() const { return 2; }
    int idxObstaclesMoving_() const { return 3; }
    int idxBlackholes_() const { return 4; }
    int idxBlackholesMoving_() const { return 5; }
    int idxBlackholeCount_() const { return 6; }
    int idxThreeEnemies_() const { return 7; }
    int totalItems_() const { return 8; }
    
    void clampSel();
};
