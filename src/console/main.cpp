/**
 * @file console/main.cpp
 * @brief Entry point for the console version of PongCpp (moved to src/console)
 */

#include "platform/platform.h"
#include "console/game.h"
#include <memory>
#include <iostream>

int main() {
    auto plat = createPlatform();
    if (!plat) {
        std::cerr << "Failed to create platform abstraction\n";
        return 1;
    }
    Game g(80, 24, *plat);
    return g.run();
}
