/**
 * @file game_core.h
 * @brief Core game logic and physics for PongCpp
 * 
 * This file contains the platform-independent game simulation logic,
 * physics calculations, and AI behavior for the Pong game.
 */

#pragma once

#include <string>
#include <vector>

/**
 * @brief Available game modes
 * 
 * Classic: Original two paddle pong
 * ThreeEnemies: Player vs right paddle while additional autonomous paddles guard top & bottom
 * Obstacles: Classic paddles plus moving obstacle blocks in center area
 * MultiBall: Multiple balls active simultaneously (chaos mode)
 */
enum class GameMode {
    Classic = 0,
    ThreeEnemies,
    Obstacles,
    MultiBall,
    ObstaclesMulti  ///< Obstacles + MultiBall combined mode
};

/**
 * @brief Obstacle block used in obstacle game mode
 */
struct Obstacle {
    double x = 0.0;    ///< Center X
    double y = 0.0;    ///< Center Y
    double w = 4.0;    ///< Width
    double h = 2.0;    ///< Height
    double vx = 0.0;   ///< Horizontal velocity (for moving obstacles mode)
    double vy = 0.0;   ///< Vertical velocity
};

/**
 * @brief Ball state (supports multi-ball mode)
 */
struct BallState {
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
};

/**
 * @brief Game state structure containing all dynamic game data
 * 
 * This structure holds the complete state of a Pong game including
 * paddle positions, ball position, scores, and game dimensions.
 */
struct GameState {
    int gw = 80;           ///< Game width in game coordinate units
    int gh = 24;           ///< Game height in game coordinate units
    double left_y = 0.0;   ///< Left paddle Y position (center)
    double right_y = 0.0;  ///< Right paddle Y position (center)
    double ball_x = 0.0;   ///< Ball X position
    double ball_y = 0.0;   ///< Ball Y position
    int paddle_h = 5;      ///< Paddle height in game units
    int score_left = 0;    ///< Left player score
    int score_right = 0;   ///< Right player score
    
    // Extended paddles for advanced modes
    double top_x = 0.0;    ///< Top horizontal paddle X (center) (ThreeEnemies mode)
    double bottom_x = 0.0; ///< Bottom horizontal paddle X (center) (ThreeEnemies mode)
    int paddle_w = 8;      ///< Horizontal paddle width

    // Obstacles (Obstacles mode)
    std::vector<Obstacle> obstacles; ///< Active obstacles

    // Multi-ball
    std::vector<BallState> balls;    ///< Active balls (ball_x/ball_y mirror balls[0])

    GameMode mode = GameMode::Classic; ///< Current game mode
};

/**
 * @brief Core game simulation class
 * 
 * GameCore handles all platform-independent game logic including:
 * - Ball physics simulation with realistic collision detection
 * - Paddle-ball interaction with velocity transfer
 * - AI opponent behavior with configurable difficulty
 * - Score tracking and game state management
 * 
 * The class uses a coordinate system where (0,0) is top-left and
 * Y increases downward. Physics calculations use continuous coordinates
 * for smooth movement and accurate collision detection.
 */
class GameCore {
public:
    /**
     * @brief Construct a new GameCore object
     * 
     * Initializes the game state and resets all values to defaults.
     */
    GameCore();
    
    /**
     * @brief Reset the game to initial state
     * 
     * Resets paddle positions, ball position, scores, and physics state.
     * Called at game start and after each point is scored.
     */
    void reset();
    
    /**
     * @brief Update game simulation for one frame
     * 
     * Performs physics simulation including:
     * - Ball movement and boundary collision
     * - Paddle-ball collision detection and response
     * - AI paddle movement
     * - Score detection when ball exits play area
     * 
     * Uses substepping for stability with fast-moving objects.
     * 
     * @param dt Time delta in seconds since last update
     */
    void update(double dt);

    /**
     * @brief Move left paddle relatively
     * 
     * Moves the left (player) paddle by a relative amount.
     * Used for keyboard control where movement is incremental.
     * 
     * @param dy Change in Y position (negative = up, positive = down)
     */
    void move_left_by(double dy);
    
    /**
     * @brief Set left paddle absolute position
     * 
     * Sets the left (player) paddle to an absolute Y coordinate.
     * Used for mouse control where position is set directly.
     * Position is automatically clamped to valid game bounds.
     * 
     * @param y Absolute Y coordinate for paddle center
     */
    void set_left_y(double y);

    /**
     * @brief Move right paddle relatively (for testing)
     * 
     * Moves the right (AI) paddle by a relative amount.
     * Normally the AI controls this paddle, but this method
     * allows manual control for testing purposes.
     * 
     * @param dy Change in Y position (negative = up, positive = down)
     */
    void move_right_by(double dy);

    /**
     * @brief Get read-only reference to current game state
     * 
     * @return const GameState& Immutable reference to game state
     */
    const GameState& state() const { return s; }
    
    /**
     * @brief Get mutable reference to current game state
     * 
     * @return GameState& Mutable reference to game state
     */
    GameState& state() { return s; }

    /**
     * @brief Set AI difficulty multiplier
     * 
     * Controls the speed and responsiveness of the AI opponent.
     * Values less than 1.0 make the AI slower/easier, values
     * greater than 1.0 make it faster/harder.
     * 
     * @param m Speed multiplier (1.0 = normal, 0.5 = easy, 2.0 = hard)
     */
    void set_ai_speed(double m) { ai_speed = m; }

    /**
     * @brief Change current game mode and reset relevant state
     */
    void set_mode(GameMode m);
    GameMode mode() const { return s.mode; }

    /**
     * @brief Spawn an extra ball (used in MultiBall mode)
     */
    void spawn_ball(double speed_scale = 1.0);

    /**
     * @brief Access balls vector (read-only)
     */
    const std::vector<BallState>& balls() const { return s.balls; }

    /**
     * @brief Access obstacles vector (read-only)
     */
    const std::vector<Obstacle>& get_obstacles() const { return s.obstacles; }

private:
    GameState s;                    ///< Current game state
    double vx, vy;                  ///< Legacy primary ball velocity (mirrors balls[0])
    double ai_speed = 1.0;          ///< AI difficulty multiplier
    bool left_ai_enabled = false;   ///< When true, left paddle is AI-controlled
    bool right_ai_enabled = true;   ///< When true, right paddle is AI-controlled
    bool physical_mode = true;      ///< Use physically-based bounce (true) or legacy arcade (false)
    bool speed_mode = false;        ///< "I am Speed" mode: no max speed, auto-acceleration if stalling
    
    /// @name Speed Mode State Tracking
    /// @{
    double low_vx_time = 0.0;       ///< Accumulated time ball has low horizontal velocity
    double prev_abs_vx = 0.0;       ///< Previous absolute horizontal velocity for tracking
    /// @}
    
    /// @name Paddle Physics State
    /// @{
    double prev_left_y = 0.0;       ///< Previous left paddle Y (for velocity calculation)
    double prev_right_y = 0.0;      ///< Previous right paddle Y (for velocity calculation)
    /// @}
    
    /// @name Physics Tuning Parameters
    /// @{
    double restitution = 1.01;      ///< Near-elastic restitution (slight speed gain to keep action lively)
    double tangent_strength = 6.0;  ///< How much contact offset affects ball spin
    double paddle_influence = 1.5;  ///< How much paddle velocity transfers to ball
    /// @}

public:
    // AI enable/disable controls (used by UI/player mode)
    void enable_left_ai(bool e){ left_ai_enabled = e; }
    void enable_right_ai(bool e){ right_ai_enabled = e; }
    bool left_ai() const { return left_ai_enabled; }
    bool right_ai() const { return right_ai_enabled; }
    void set_physical_mode(bool on){ physical_mode = on; }
    bool is_physical() const { return physical_mode; }
    void set_speed_mode(bool on){ speed_mode = on; if(!on) low_vx_time = 0.0; }
    bool is_speed_mode() const { return speed_mode; }
};
