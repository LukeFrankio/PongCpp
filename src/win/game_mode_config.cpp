/**
 * @file game_mode_config.cpp
 * @brief Implementation of game mode configuration
 */

#include "game_mode_config.h"
#include <string>

const char* GameModeConfig::getDescription() const {
    if (isClassic()) {
        return "Classic Pong";
    }
    
    // Build description from active features
    static std::string desc;
    desc.clear();
    
    bool first = true;
    auto add = [&](const char* feature) {
        if (!first) desc += " + ";
        desc += feature;
        first = false;
    };
    
    if (three_enemies) add("Three Enemies");
    if (multiball) add("MultiBall");
    if (obstacles) {
        if (obstacles_moving) add("Moving Obstacles");
        else add("Obstacles");
    }
    if (blackholes) {
        if (blackhole_count > 1) {
            if (blackholes_moving) add("Multiple Moving Black Holes");
            else add("Multiple Black Holes");
        } else {
            if (blackholes_moving) add("Moving Black Hole");
            else add("Black Hole");
        }
    }
    
    if (desc.empty()) desc = "Custom Mode";
    return desc.c_str();
}
