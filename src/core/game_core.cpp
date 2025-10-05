/**
 * @file game_core.cpp
 * @brief Implementation of core game logic and physics
 * 
 * This file implements the GameCore class with realistic Pong physics
 * including ball-paddle collision with spin effects, AI behavior,
 * and stable numerical integration using substepping.
 */

#include "game_core.h"
#include <cmath>
#include <algorithm>
#include <random>

GameCore::GameCore() { reset(); }

void GameCore::reset() {
    // Initialize game dimensions and paddle size
    s.gw = 80; s.gh = 24; s.paddle_h = 5;
    
    // Center paddles vertically
    s.left_y = s.gh/2.0 - s.paddle_h/2.0;
    s.right_y = s.left_y;
    
    // Center ball (primary)
    s.ball_x = s.gw/2.0; s.ball_y = s.gh/2.0;
    vx = 20.0; vy = 10.0; // legacy
    s.balls.clear();
    s.balls.push_back({s.ball_x, s.ball_y, vx, vy});

    // Horizontal paddles (ThreeEnemies)
    s.top_x = s.gw/2.0; s.bottom_x = s.gw/2.0; s.paddle_w = 10;

    // Obstacles
    s.obstacles.clear();
    if (s.mode == GameMode::Obstacles || s.mode == GameMode::ObstaclesMulti) {
        // three moving vertical-ish blocks in center
        int count = 3;
        for (int i=0;i<count;++i) {
            double fx = s.gw/2.0 + (i-1)*10.0;
            double fy = s.gh/2.0 + (i-1)*2.0;
            Obstacle ob; ob.x = fx; ob.y = fy; ob.w = 4; ob.h = 3; ob.vx = (i-1)*5.0; ob.vy = (i%2==0?5.0:-5.0);
            s.obstacles.push_back(ob);
        }
    }
    if (s.mode == GameMode::MultiBall || s.mode == GameMode::ObstaclesMulti) {
        // spawn additional balls
        for (int i=0;i<2;i++) spawn_ball(0.9 + 0.2*i);
    }
    
    // Black holes are set by apply_mode_config, not reset
    // but we clear them here to be safe
    s.blackholes.clear();
    
    // Reset scores
    s.score_left = 0; s.score_right = 0;
    
    // Store initial paddle positions for velocity calculations
    prev_left_y = s.left_y;
    prev_right_y = s.right_y;
    
    // Reset speed mode tracking
    low_vx_time = 0.0;
    prev_abs_vx = std::abs(vx);
}

void GameCore::update(double dt) {
    // simple substepping to improve collision stability
    const double maxStep = 1.0/240.0; // 240 Hz substep
    double remaining = dt;
    // store paddle positions to compute paddle velocity
    double left_y_before = s.left_y;
    double right_y_before = s.right_y;
    while (remaining > 1e-6) {
        double step = remaining > maxStep ? maxStep : remaining;
        remaining -= step;
        // Update obstacles (Obstacles mode)
        if (s.mode == GameMode::Obstacles || s.mode == GameMode::ObstaclesMulti) {
            // Apply black hole gravity to obstacles if enabled
            if (config_obstacles_gravity) {
                for (auto &ob : s.obstacles) {
                    for (const auto &bh : s.blackholes) {
                        double fx, fy;
                        bh.calculateForce(ob.x, ob.y, fx, fy);
                        // Apply weak force (10% of ball force) as acceleration
                        ob.vx += fx * step * 0.1;
                        ob.vy += fy * step * 0.1;
                    }
                }
            }
            
            // Update obstacle positions
            for (auto &ob : s.obstacles) {
                ob.x += ob.vx * step;
                ob.y += ob.vy * step;
                if (ob.x - ob.w/2 < 5 || ob.x + ob.w/2 > s.gw-5) ob.vx = -ob.vx;
                if (ob.y - ob.h/2 < 1 || ob.y + ob.h/2 > s.gh-1) ob.vy = -ob.vy;
            }
            
            // Obstacle-obstacle collision detection and response
            for (size_t i = 0; i < s.obstacles.size(); ++i) {
                for (size_t j = i + 1; j < s.obstacles.size(); ++j) {
                    Obstacle &ob1 = s.obstacles[i];
                    Obstacle &ob2 = s.obstacles[j];
                    
                    // AABB overlap test
                    double left1 = ob1.x - ob1.w/2.0;
                    double right1 = ob1.x + ob1.w/2.0;
                    double top1 = ob1.y - ob1.h/2.0;
                    double bottom1 = ob1.y + ob1.h/2.0;
                    
                    double left2 = ob2.x - ob2.w/2.0;
                    double right2 = ob2.x + ob2.w/2.0;
                    double top2 = ob2.y - ob2.h/2.0;
                    double bottom2 = ob2.y + ob2.h/2.0;
                    
                    bool overlap_x = (left1 < right2) && (right1 > left2);
                    bool overlap_y = (top1 < bottom2) && (bottom1 > top2);
                    
                    if (overlap_x && overlap_y) {
                        // Calculate penetration depths on each axis
                        double pen_left = right1 - left2;
                        double pen_right = right2 - left1;
                        double pen_top = bottom1 - top2;
                        double pen_bottom = bottom2 - top1;
                        
                        double pen_x = std::min(pen_left, pen_right);
                        double pen_y = std::min(pen_top, pen_bottom);
                        
                        // Resolve along axis of minimum penetration
                        if (pen_x < pen_y) {
                            // Separate horizontally
                            double sep = pen_x / 2.0 + 0.01;
                            if (pen_left < pen_right) {
                                ob1.x -= sep;
                                ob2.x += sep;
                            } else {
                                ob1.x += sep;
                                ob2.x -= sep;
                            }
                            // Elastic collision: exchange velocities
                            double temp_vx = ob1.vx;
                            ob1.vx = ob2.vx;
                            ob2.vx = temp_vx;
                        } else {
                            // Separate vertically
                            double sep = pen_y / 2.0 + 0.01;
                            if (pen_top < pen_bottom) {
                                ob1.y -= sep;
                                ob2.y += sep;
                            } else {
                                ob1.y += sep;
                                ob2.y -= sep;
                            }
                            // Elastic collision: exchange velocities
                            double temp_vy = ob1.vy;
                            ob1.vy = ob2.vy;
                            ob2.vy = temp_vy;
                        }
                    }
                }
            }
        }
        
        // Update black holes
        for (auto &bh : s.blackholes) {
            bh.update(step, s.gw, s.gh);
        }

        // Multi-ball / single ball iteration
        for (size_t bi=0; bi<s.balls.size(); ++bi) {
            BallState &b = s.balls[bi];
            
            // Apply black hole gravitational forces
            for (const auto &bh : s.blackholes) {
                double fx, fy;
                bh.calculateForce(b.x, b.y, fx, fy);
                // Apply force as acceleration (F = ma, assuming unit mass)
                b.vx += fx * step;
                b.vy += fy * step;
            }
            
            b.x += b.vx * step;
            b.y += b.vy * step;
            
            // Check for black hole contact/destruction if enabled
            if (config_blackholes_destroy_balls && !s.blackholes.empty()) {
                for (const auto &bh : s.blackholes) {
                    double dx = b.x - bh.x;
                    double dy = b.y - bh.y;
                    double dist = std::sqrt(dx*dx + dy*dy);
                    
                    // Check if ball touches event horizon (radius of black hole)
                    if (dist < bh.radius) {
                        // Calculate distance from last reset to check if immediately sucked in again
                        double reset_dx = bh.x - b.last_reset_x;
                        double reset_dy = bh.y - b.last_reset_y;
                        double reset_dist = std::sqrt(reset_dx*reset_dx + reset_dy*reset_dy);
                        
                        // If last reset was at/near center and we're being sucked in again
                        // reset to side of center instead
                        if (reset_dist < 3.0) {
                            // Reset to side (offset from center)
                            b.x = s.gw/2.0 + 10.0;
                            b.y = s.gh/2.0;
                            b.last_reset_x = b.x;
                            b.last_reset_y = b.y;
                        } else {
                            // First reset or far enough from last - reset to center
                            b.x = s.gw/2.0;
                            b.y = s.gh/2.0;
                            b.last_reset_x = b.x;
                            b.last_reset_y = b.y;
                        }
                        
                        // Reset velocity to reasonable initial state
                        double speed = 25.0;
                        double angle = (bi * 0.7 + 0.3) * 3.14159; // Different angle per ball
                        b.vx = speed * std::cos(angle);
                        b.vy = speed * std::sin(angle);
                        
                        // No points awarded for black hole destruction
                        break; // Only process first black hole hit
                    }
                }
            }
            
            if (s.mode != GameMode::ThreeEnemies) {
                if (b.y < 0) { b.y = 0; b.vy = -b.vy; }
                if (b.y > s.gh-1) { b.y = s.gh-1; b.vy = -b.vy; }
            }
        }

        // paddle geometry: paddles are approx width 2 (x positions 1..3) with semicircle caps
    auto dist2 = [&](double ax, double ay, double bx, double by){ double dx=ax-bx, dy=ay-by; return dx*dx+dy*dy; };
    const double ball_r = 0.6; // ball radius in game coords
        // estimate paddle velocities (per second) from last frame positions stored in prev_*
        double left_paddle_v = 0.0, right_paddle_v = 0.0;
        if (dt > 1e-8) {
            left_paddle_v = (s.left_y - prev_left_y) / dt;
            right_paddle_v = (s.right_y - prev_right_y) / dt;
        }
    auto handle_paddle_local = [&](double &bx, double &by, double &bvx, double &bvy,
                                   double px_left, double px_right, double py_top, double py_bottom, bool isLeft)->bool {
        if (bx >= px_left && bx <= px_right && by >= py_top && by <= py_bottom) {
            // collision with flat face: normal points horizontally
            double nx = isLeft ? 1.0 : -1.0;
            double ny = 0.0;
            // push ball outside
            if (isLeft) bx = px_right + ball_r + 1e-3; else bx = px_left - ball_r - 1e-3;
            // reflect velocity across normal
            double vdotn = bvx*nx + bvy*ny;
            bvx = bvx - 2.0 * vdotn * nx;
            bvy = bvy - 2.0 * vdotn * ny;
            // tangential impulse based on vertical contact offset and paddle velocity
            double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
            double midY_rect = (py_top + py_bottom) / 2.0;
            double ry_rect = (py_bottom - py_top) / 2.0;
            double contact_offset = 0.0;
            if (ry_rect > 1e-6) contact_offset = (by - midY_rect) / ry_rect;
            if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
            double tx = -ny, ty = nx;
            double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
            if (physical_mode) {
                double preSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                bvx += tx * tangential; bvy += ty * tangential;
                double newSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                if (newSpeed > 1e-6) {
                    double target = preSpeed * restitution;
                    double scale = target / newSpeed;
                    bvx *= scale; bvy *= scale;
                }
                // Speed mode: no cap, else normal cap
                if (!speed_mode) {
                    double maxsp = 90.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                }
            } else {
                // Arcade: simple additive influence and mild speed-up
                bvx += tx * (tangent_strength * contact_offset * 0.5);
                bvy += ty * (tangent_strength * contact_offset * 0.5);
                bvx *= 1.02; bvy *= 1.02;
                if (!speed_mode) {
                    double maxsp = 80.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                }
            }
            return true;
        }
        // caps are drawn as ellipses on the left and right sides of the rectangle
        double midY = (py_top + py_bottom) / 2.0;
        double rx = (px_right - px_left) / 2.0; // horizontal radius of cap ellipse
        double ry = (py_bottom - py_top) / 2.0; // vertical radius of cap ellipse
        // left cap center at (px_left, midY), right cap center at (px_right, midY)
        // expand ellipse by ball radius to treat ball as a point against an expanded shape
        double rx_e = rx + ball_r;
        double ry_e = ry + ball_r;
        // left ellipse collision
        {
            double cx = px_left;
            double dx = bx - cx;
            double dy = by - midY;
            double val = (dx*dx) / (rx_e*rx_e) + (dy*dy) / (ry_e*ry_e);
            if (val <= 1.0) {
                // push out to ellipse boundary: compute len in ellipse-space
                double lenp = sqrt((dx*dx)/(rx_e*rx_e) + (dy*dy)/(ry_e*ry_e));
                if (lenp < 1e-6) lenp = 1e-6;
                double new_dx = dx / lenp;
                double new_dy = dy / lenp;
                bx = cx + new_dx; by = midY + new_dy;
                // compute normal for ellipse boundary more accurately
                double ex = (bx - cx) / (rx_e*rx_e);
                double ey = (by - midY) / (ry_e*ry_e);
                double nlen = sqrt(ex*ex + ey*ey);
                if (nlen < 1e-6) nlen = 1e-6;
                double nx = ex / nlen;
                double ny = ey / nlen;
                double vdotn = bvx*nx + bvy*ny;
                bvx = bvx - 2.0 * vdotn * nx;
                bvy = bvy - 2.0 * vdotn * ny;
                // tangential impulse and restitution
                double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
                double contact_offset = (by - midY) / ( (py_bottom - py_top)/2.0 );
                if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
                double tx = -ny, ty = nx;
                double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
                if (physical_mode) {
                    double preSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                    bvx += tx * tangential; bvy += ty * tangential;
                    double newSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                    if (newSpeed > 1e-6) {
                        double target = preSpeed * restitution;
                        double scale = target / newSpeed; bvx*=scale; bvy*=scale;
                    }
                    // Speed mode: no cap, else normal cap
                    if (!speed_mode) {
                        double maxsp = 90.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                    }
                } else {
                    bvx += tx * (tangent_strength * contact_offset * 0.5);
                    bvy += ty * (tangent_strength * contact_offset * 0.5);
                    bvx *= 1.02; bvy *= 1.02;
                    double maxsp = 80.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                }
                return true;
            }
        }
        // right ellipse collision
        {
            double cx = px_right;
            double dx = bx - cx;
            double dy = by - midY;
            double val = (dx*dx) / (rx_e*rx_e) + (dy*dy) / (ry_e*ry_e);
            if (val <= 1.0) {
                double lenp = sqrt((dx*dx)/(rx_e*rx_e) + (dy*dy)/(ry_e*ry_e));
                if (lenp < 1e-6) lenp = 1e-6;
                double new_dx = dx / lenp;
                double new_dy = dy / lenp;
                bx = cx + new_dx; by = midY + new_dy;
                double ex = (bx - cx) / (rx_e*rx_e);
                double ey = (by - midY) / (ry_e*ry_e);
                double nlen = sqrt(ex*ex + ey*ey);
                if (nlen < 1e-6) nlen = 1e-6;
                double nx = ex / nlen;
                double ny = ey / nlen;
                double vdotn = bvx*nx + bvy*ny;
                bvx = bvx - 2.0 * vdotn * nx;
                bvy = bvy - 2.0 * vdotn * ny;
                double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
                double contact_offset = (by - midY) / ( (py_bottom - py_top)/2.0 );
                if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
                double tx = -ny, ty = nx;
                double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
                if (physical_mode){
                    double preSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                    bvx += tx * tangential; bvy += ty * tangential;
                    double newSpeed = std::sqrt(bvx*bvx + bvy*bvy);
                    if (newSpeed > 1e-6) {
                        double target = preSpeed * restitution;
                        double scale = target / newSpeed; bvx*=scale; bvy*=scale;
                    }
                    double maxsp = 90.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                } else {
                    bvx += tx * (tangent_strength * contact_offset * 0.5);
                    bvy += ty * (tangent_strength * contact_offset * 0.5);
                    bvx *= 1.02; bvy *= 1.02;
                    double maxsp = 80.0; double spc = std::sqrt(bvx*bvx + bvy*bvy); if (spc > maxsp) { double sc = maxsp / spc; bvx*=sc; bvy*=sc; }
                }
                return true;
            }
        }
        return false;
    };

    auto process_ball = [&](BallState &b)->void {
        // left paddle collision
        double l_px_left = 1.0, l_px_right = 3.0;
        if (b.x < l_px_right + 1.5) {
            if (handle_paddle_local(b.x, b.y, b.vx, b.vy, l_px_left, l_px_right, s.left_y, s.left_y + s.paddle_h, true)) {
                if (b.vx < 0) b.vx = fabs(b.vx);
                if (!speed_mode) {
                    double sp = sqrt(b.vx*b.vx + b.vy*b.vy);
                    double maxsp = 80.0;
                    if (sp > maxsp) { b.vx *= maxsp/sp; b.vy *= maxsp/sp; }
                }
            } else if (b.x < -1.0) {
                s.score_right++;
                b.x = s.gw/2.0; b.y = s.gh/2.0; b.vx = 20.0; b.vy = 10.0;
            }
        }
        // right paddle collision
        double r_px_left = s.gw - 3.0, r_px_right = s.gw - 1.0;
        if (b.x > r_px_left - 1.5) {
            if (handle_paddle_local(b.x, b.y, b.vx, b.vy, r_px_left, r_px_right, s.right_y, s.right_y + s.paddle_h, false)) {
                if (b.vx > 0) b.vx = -fabs(b.vx);
                if (!speed_mode) {
                    double sp = sqrt(b.vx*b.vx + b.vy*b.vy);
                    double maxsp = 80.0;
                    if (sp > maxsp) { b.vx *= maxsp/sp; b.vy *= maxsp/sp; }
                }
            } else if (b.x > s.gw + 1.0) {
                s.score_left++;
                b.x = s.gw/2.0; b.y = s.gh/2.0; b.vx = -20.0; b.vy = -10.0;
            }
        }

        // Obstacles collisions (AABB vs ball)
        if (s.mode == GameMode::Obstacles || s.mode == GameMode::ObstaclesMulti) {
            for (auto &ob : s.obstacles) {
                double left = ob.x - ob.w/2.0;
                double right = ob.x + ob.w/2.0;
                double top = ob.y - ob.h/2.0;
                double bottom = ob.y + ob.h/2.0;
                if (b.x >= left-0.6 && b.x <= right+0.6 && b.y >= top-0.6 && b.y <= bottom+0.6) {
                    // compute penetration depths
                    double penLeft = (right+0.6) - b.x;
                    double penRight = b.x - (left-0.6);
                    double penTop = (bottom+0.6) - b.y;
                    double penBottom = b.y - (top-0.6);
                    // choose minimal axis
                    double minPen = std::min({penLeft, penRight, penTop, penBottom});
                    if (minPen == penLeft) { b.x = right+0.61; b.vx = fabs(b.vx); }
                    else if (minPen == penRight) { b.x = left-0.61; b.vx = -fabs(b.vx); }
                    else if (minPen == penTop) { b.y = bottom+0.61; b.vy = fabs(b.vy); }
                    else { b.y = top-0.61; b.vy = -fabs(b.vy); }
                }
            }
        }

        // ThreeEnemies: scoring if ball exits vertically outside paddle coverage; reflect only if within paddle span
        if (s.mode == GameMode::ThreeEnemies) {
            double halfW = s.paddle_w/2.0;
            double top_line = 0.0; // top boundary
            double bottom_line = s.gh - 1.0; // bottom boundary
            // If ball crosses top
            if (b.y < top_line) {
                if (fabs(b.x - s.top_x) <= halfW) {
                    // treat as paddle hit -> reflect down
                    b.y = top_line; b.vy = fabs(b.vy);
                } else {
                    // Score for bottom/AI side (treat like passing player paddle)
                    s.score_right++;
                    b.x = s.gw/2.0; b.y = s.gh/2.0; b.vx = 20.0; b.vy = 10.0; // re-center
                }
            }
            // If ball crosses bottom
            if (b.y > bottom_line) {
                if (fabs(b.x - s.bottom_x) <= halfW) {
                    b.y = bottom_line; b.vy = -fabs(b.vy);
                } else {
                    // Score for left/player side
                    s.score_left++;
                    b.x = s.gw/2.0; b.y = s.gh/2.0; b.vx = -20.0; b.vy = -10.0;
                }
            }
        }
    };

        for (auto &b : s.balls) process_ball(b);

        // Ball-to-ball collision detection (for multi-ball modes)
        if (s.balls.size() > 1) {
            for (size_t i = 0; i < s.balls.size(); ++i) {
                for (size_t j = i + 1; j < s.balls.size(); ++j) {
                    BallState &b1 = s.balls[i];
                    BallState &b2 = s.balls[j];
                    
                    // Calculate distance between ball centers
                    double dx = b2.x - b1.x;
                    double dy = b2.y - b1.y;
                    double dist_sq = dx*dx + dy*dy;
                    double collision_dist = 2.0 * ball_r; // sum of radii
                    double collision_dist_sq = collision_dist * collision_dist;
                    
                    if (dist_sq < collision_dist_sq && dist_sq > 1e-6) {
                        // Balls are colliding
                        double dist = std::sqrt(dist_sq);
                        
                        // Normalized collision normal (from b1 to b2)
                        double nx = dx / dist;
                        double ny = dy / dist;
                        
                        // Separate balls to prevent overlap
                        double overlap = collision_dist - dist;
                        double separation = overlap / 2.0 + 0.01; // small extra push
                        b1.x -= nx * separation;
                        b1.y -= ny * separation;
                        b2.x += nx * separation;
                        b2.y += ny * separation;
                        
                        // Calculate relative velocity
                        double dvx = b2.vx - b1.vx;
                        double dvy = b2.vy - b1.vy;
                        
                        // Relative velocity in collision normal direction
                        double dvn = dvx * nx + dvy * ny;
                        
                        // Only resolve if balls are approaching (not separating)
                        if (dvn < 0) {
                            // Elastic collision with restitution
                            double impulse = -(1.0 + restitution) * dvn / 2.0;
                            
                            // Apply impulse to both balls (equal mass assumption)
                            b1.vx -= impulse * nx;
                            b1.vy -= impulse * ny;
                            b2.vx += impulse * nx;
                            b2.vy += impulse * ny;
                            
                            // Apply speed cap if not in speed mode
                            if (!speed_mode) {
                                double maxsp = 90.0;
                                double sp1 = std::sqrt(b1.vx*b1.vx + b1.vy*b1.vy);
                                if (sp1 > maxsp) {
                                    b1.vx *= maxsp / sp1;
                                    b1.vy *= maxsp / sp1;
                                }
                                double sp2 = std::sqrt(b2.vx*b2.vx + b2.vy*b2.vy);
                                if (sp2 > maxsp) {
                                    b2.vx *= maxsp / sp2;
                                    b2.vy *= maxsp / sp2;
                                }
                            }
                        }
                    }
                }
            }
        }

        // AI for right paddle (performed per frame, fine-grained control above)
        // (AI update outside of substep loop - handled after loop)
    }

    // compute paddle velocities (units per second)
    double left_paddle_v = (s.left_y - left_y_before) / dt;
    double right_paddle_v = (s.right_y - right_y_before) / dt;

    // AI for paddles if enabled
    if (right_ai_enabled) {
        double target_ball_y = s.ball_y;
        double min_dx = 1e9;
        for (auto &b : s.balls) {
            double dx = (s.gw - b.x);
            if (b.vx > 0 && dx < min_dx) { min_dx = dx; target_ball_y = b.y; }
        }
        double target = target_ball_y - s.paddle_h/2.0;
        double dy = target - s.right_y;
        double max_speed = 25.0 * ai_speed * dt;
        if (fabs(dy) > max_speed) dy = (dy > 0 ? 1 : -1) * max_speed;
        s.right_y += dy;
    }
    if (left_ai_enabled) {
        // Track closest ball moving toward left paddle (vx < 0)
        double target_ball_y = s.ball_y;
        double min_dx = 1e9;
        for (auto &b : s.balls) {
            double dx = b.x; // distance from left edge
            if (b.vx < 0 && dx < min_dx) { min_dx = dx; target_ball_y = b.y; }
        }
        double target = target_ball_y - s.paddle_h/2.0;
        double dy = target - s.left_y;
        double max_speed = 25.0 * ai_speed * dt;
        if (fabs(dy) > max_speed) dy = (dy > 0 ? 1 : -1) * max_speed;
        s.left_y += dy;
    }

    // ThreeEnemies horizontal paddle AI: track nearest ball
    if (s.mode == GameMode::ThreeEnemies) {
        double nearest_top = s.balls.front().x;
        double nearest_bottom = s.balls.front().x;
        double min_top_dist = 1e9, min_bottom_dist = 1e9;
        for (auto &b : s.balls) {
            if (b.vy < 0) { double d = fabs(b.y - 1.0); if (d < min_top_dist) { min_top_dist = d; nearest_top = b.x; } }
            if (b.vy > 0) { double d = fabs(b.y - (s.gh - 2.0)); if (d < min_bottom_dist) { min_bottom_dist = d; nearest_bottom = b.x; } }
        }
        double speed = 30.0 * dt;
        s.top_x += std::clamp(nearest_top - s.top_x, -speed, speed);
        s.bottom_x += std::clamp(nearest_bottom - s.bottom_x, -speed, speed);
        // clamp within bounds
        if (s.top_x < s.paddle_w/2.0) s.top_x = s.paddle_w/2.0;
        if (s.top_x > s.gw - s.paddle_w/2.0) s.top_x = s.gw - s.paddle_w/2.0;
        if (s.bottom_x < s.paddle_w/2.0) s.bottom_x = s.paddle_w/2.0;
        if (s.bottom_x > s.gw - s.paddle_w/2.0) s.bottom_x = s.gw - s.paddle_w/2.0;
    }

    if (s.left_y < 0) s.left_y = 0;
    if (s.left_y + s.paddle_h > s.gh) s.left_y = s.gh - s.paddle_h;
    if (s.right_y < 0) s.right_y = 0;
    if (s.right_y + s.paddle_h > s.gh) s.right_y = s.gh - s.paddle_h;

    // Speed mode: accelerate if horizontal velocity is low for too long
    if (speed_mode && !s.balls.empty()) {
        double current_abs_vx = std::abs(s.balls[0].vx);
        const double vx_threshold = 15.0; // threshold for "low" horizontal velocity
        const double accel_time_threshold = 0.5; // seconds of low vx before acceleration kicks in
        const double accel_boost = 1.15; // 15% speed boost per trigger
        
        if (current_abs_vx < vx_threshold) {
            low_vx_time += dt;
            if (low_vx_time >= accel_time_threshold) {
                // Boost horizontal velocity while preserving direction
                double dir = (s.balls[0].vx >= 0) ? 1.0 : -1.0;
                s.balls[0].vx *= accel_boost;
                low_vx_time = 0.0; // reset timer after boost
            }
        } else {
            low_vx_time = 0.0; // reset if velocity is healthy
        }
        prev_abs_vx = current_abs_vx;
    }
    
    // Mirror primary ball for legacy fields
    if (!s.balls.empty()) { s.ball_x = s.balls[0].x; s.ball_y = s.balls[0].y; vx = s.balls[0].vx; vy = s.balls[0].vy; }

    // store for next frame's velocity estimation
    prev_left_y = s.left_y;
    prev_right_y = s.right_y;
}

void GameCore::move_left_by(double dy) { 
    s.left_y += dy; 
}

void GameCore::set_left_y(double y) { 
    s.left_y = y; 
}

void GameCore::move_right_by(double dy) { 
    s.right_y += dy; 
}

void GameCore::set_mode(GameMode m) {
    if (s.mode == m) return;
    s.mode = m;
    reset();
}

void GameCore::spawn_ball(double speed_scale) {
    double angle = 0.3 + (s.balls.size()*0.7); // simple variety
    double speed = 22.0 * speed_scale;
    double dir = (s.balls.size()%2==0)?1.0:-1.0;
    BallState b; b.x = s.gw/2.0; b.y = s.gh/2.0; b.vx = dir*speed; b.vy = speed*0.5;
    s.balls.push_back(b);
}

void GameCore::spawn_blackhole(double x, double y, bool moving) {
    BlackHole bh;
    bh.x = x;
    bh.y = y;
    bh.moving = moving;
    if (moving) {
        // Random velocity for moving black holes
        double angle = (s.blackholes.size() * 1.2) + 0.5;
        bh.vx = 10.0 * std::cos(angle);
        bh.vy = 10.0 * std::sin(angle);
    }
    bh.strength = 500.0;
    bh.radius = 2.0;
    bh.influence = 100.0;
    s.blackholes.push_back(bh);
}

void GameCore::apply_mode_config(bool multiball, bool obstacles, bool obstacles_moving,
                                bool blackholes, bool blackholes_moving, int blackhole_count,
                                int multiball_count, bool three_enemies,
                                bool obstacles_gravity, bool blackholes_destroy_balls) {
    // Store config flags for use in update loop
    config_obstacles_gravity = obstacles_gravity;
    config_blackholes_destroy_balls = blackholes_destroy_balls;
    
    // Set mode enum based on combination of flags (for legacy compatibility)
    if (obstacles && multiball) {
        s.mode = GameMode::ObstaclesMulti;
    } else if (multiball) {
        s.mode = GameMode::MultiBall;
    } else if (obstacles) {
        s.mode = GameMode::Obstacles;
    } else if (three_enemies) {
        s.mode = GameMode::ThreeEnemies;
    } else {
        s.mode = GameMode::Classic;
    }
    
    // Clear existing dynamic objects
    s.balls.clear();
    s.obstacles.clear();
    s.blackholes.clear();
    
    // Always have at least one ball
    s.balls.push_back({s.gw/2.0, s.gh/2.0, 20.0, 10.0});
    vx = 20.0; vy = 10.0; // Keep legacy velocities in sync
    
    // Add extra balls for multiball
    if (multiball) {
        for (int i = 1; i < multiball_count; ++i) {
            spawn_ball(0.9 + 0.1 * i);
        }
    }
    
    // Add obstacles
    if (obstacles) {
        int count = 3;
        for (int i=0;i<count;++i) {
            double fx = s.gw/2.0 + (i-1)*10.0;
            double fy = s.gh/2.0 + (i-1)*2.0;
            Obstacle ob; 
            ob.x = fx; ob.y = fy; ob.w = 4; ob.h = 3;
            if (obstacles_moving) {
                ob.vx = (i-1)*5.0; 
                ob.vy = (i%2==0?5.0:-5.0);
            } else {
                ob.vx = 0.0;
                ob.vy = 0.0;
            }
            s.obstacles.push_back(ob);
        }
    }
    
    // Add black holes
    if (blackholes) {
        if (blackhole_count == 1) {
            spawn_blackhole(s.gw/2.0, s.gh/2.0, blackholes_moving);
        } else {
            // Distribute multiple black holes
            for (int i = 0; i < blackhole_count; ++i) {
                double angle = (i * 2.0 * 3.14159) / blackhole_count;
                double radius = 15.0;
                double bx = s.gw/2.0 + radius * std::cos(angle);
                double by = s.gh/2.0 + radius * std::sin(angle);
                spawn_blackhole(bx, by, blackholes_moving);
            }
        }
    }
    
    // Three enemies mode affects collision logic, not objects
    // The actual horizontal paddle logic is handled in update()
}


