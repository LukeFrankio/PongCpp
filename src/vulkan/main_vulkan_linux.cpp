#include "game_vulkan_linux.h"
#include <iostream>

int main() {
    try {
        return run_vulkan_pong_linux();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}