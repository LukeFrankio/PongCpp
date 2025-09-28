/**
 * @file app_controller.h
 * @brief Main application controller for Windows GUI version
 * 
 * This file defines the AppController class which serves as the central
 * coordinator for the Windows GUI application, managing game sessions,
 * rendering, input handling, and UI state.
 */

#pragma once
#include <memory>

struct UIState;
struct InputState;
struct Settings;
class GameSession;
class RenderDispatch;
class SettingsStore;
class HighScoresStore;

/**
 * @brief Central application controller for Windows GUI version
 * 
 * AppController serves as the main coordinator for the Windows GUI
 * application, managing the lifecycle of game sessions, UI states,
 * input processing, and rendering operations. It acts as the bridge
 * between the low-level Windows message loop and the high-level
 * game logic.
 * 
 * The controller maintains ownership of key application subsystems
 * including game sessions, settings persistence, high score tracking,
 * and rendering dispatch.
 */
class AppController {
public:
    /**
     * @brief Construct a new AppController
     * 
     * Initializes the application controller but does not perform
     * expensive initialization. Call initialize() after construction.
     */
    AppController();
    
    /**
     * @brief Destroy the AppController and clean up resources
     * 
     * Properly shuts down all subsystems and releases resources.
     */
    ~AppController();
    
    /**
     * @brief Initialize the application controller
     * 
     * Performs expensive initialization including loading settings,
     * initializing game sessions, and setting up rendering systems.
     * Must be called after construction and before other operations.
     */
    void initialize();
    
    /**
     * @brief Update application state for one frame
     * 
     * Processes game logic updates, UI state changes, and other
     * per-frame operations. Should be called once per frame.
     * 
     * @param dt Delta time in seconds since last update
     */
    void tick(double dt);
    
    /**
     * @brief Render the current frame
     * 
     * Dispatches rendering operations for the current frame including
     * game graphics, UI elements, and overlays. Should be called
     * once per frame after tick().
     */
    void render();
    
    /**
     * @brief Process input events
     * 
     * Handles keyboard, mouse, and other input events, routing them
     * to appropriate subsystems based on current UI state.
     * 
     * @param in Current input state including key/mouse states
     */
    void on_input(const InputState& in);
};
