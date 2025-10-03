/**
 * @file black_hole.cpp
 * @brief Implementation of black hole physics
 */

#include "black_hole.h"
#include <cmath>
#include <algorithm>

void BlackHole::calculateForce(double px, double py, double& fx, double& fy) const {
    // Calculate direction vector from point to black hole
    double dx = x - px;
    double dy = y - py;
    double dist_sq = dx * dx + dy * dy;
    double dist = std::sqrt(dist_sq);
    
    // Check if within influence radius
    if (dist > influence) {
        fx = 0.0;
        fy = 0.0;
        return;
    }
    
    // Prevent division by zero and extreme forces at center
    const double min_dist = 0.5;
    if (dist < min_dist) {
        dist = min_dist;
        dist_sq = min_dist * min_dist;
    }
    
    // Calculate force magnitude using inverse square law
    // F = strength / r^2
    double force_mag = strength / dist_sq;
    
    // Normalize direction and apply force
    double nx = dx / dist;
    double ny = dy / dist;
    
    fx = force_mag * nx;
    fy = force_mag * ny;
}

void BlackHole::update(double dt, int bounds_w, int bounds_h) {
    if (!moving) return;
    
    // Update position
    x += vx * dt;
    y += vy * dt;
    
    // Bounce off walls (with some margin)
    double margin = radius + 5.0;
    if (x < margin) {
        x = margin;
        vx = std::abs(vx);
    } else if (x > bounds_w - margin) {
        x = bounds_w - margin;
        vx = -std::abs(vx);
    }
    
    if (y < margin) {
        y = margin;
        vy = std::abs(vy);
    } else if (y > bounds_h - margin) {
        y = bounds_h - margin;
        vy = -std::abs(vy);
    }
}
