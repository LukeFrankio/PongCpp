#include "platform.h"
#include "game.h"
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
