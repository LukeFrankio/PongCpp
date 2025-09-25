/**
 * @file main.cpp
 * @brief Entry point for the console version of PongCpp
 * 
 * This file contains the main function that initializes the platform
 * abstraction layer and starts the console-based Pong game.
 * 
 * The console version features:
 * - ASCII art rendering
 * - Cross-platform keyboard input
 * - Text-based display suitable for any terminal
 */

#include "platform.h"
#include "game.h"
#include <memory>
#include <iostream>

/**
 * @brief Main entry point for console Pong game
 * 
 * Creates a platform-specific console interface and starts the game
 * with standard dimensions (80x24 characters). The game runs until
 * the user quits or an error occurs.
 * 
 * @return 0 on successful completion, 1 on initialization error
 */
int main() {
    // Create platform-specific console abstraction
    auto plat = createPlatform();
    if (!plat) {
        std::cerr << "Failed to create platform abstraction\n";
        return 1;
    }
    
    // Initialize and run the game with 80x24 character display
    Game g(80, 24, *plat);
    return g.run();
}
