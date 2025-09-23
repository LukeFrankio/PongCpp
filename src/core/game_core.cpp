#include "game_core.h"
#include <cmath>

GameCore::GameCore() { reset(); }

void GameCore::reset() {
    s.gw = 80; s.gh = 24; s.paddle_h = 5;
    s.left_y = s.gh/2.0 - s.paddle_h/2.0;
    s.right_y = s.left_y;
    s.ball_x = s.gw/2.0; s.ball_y = s.gh/2.0;
    vx = 20.0; vy = 10.0;
    s.score_left = 0; s.score_right = 0;
    prev_left_y = s.left_y;
    prev_right_y = s.right_y;
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

        s.ball_x += vx * step;
        s.ball_y += vy * step;

        if (s.ball_y < 0) { s.ball_y = 0; vy = -vy; }
        if (s.ball_y > s.gh-1) { s.ball_y = s.gh-1; vy = -vy; }

        // paddle geometry: paddles are approx width 2 (x positions 1..3) with semicircle caps
    auto dist2 = [&](double ax, double ay, double bx, double by){ double dx=ax-bx, dy=ay-by; return dx*dx+dy*dy; };
    const double ball_r = 0.6; // ball radius in game coords
        // estimate paddle velocities (per second) from last frame positions stored in prev_*
        double left_paddle_v = 0.0, right_paddle_v = 0.0;
        if (dt > 1e-8) {
            left_paddle_v = (s.left_y - prev_left_y) / dt;
            right_paddle_v = (s.right_y - prev_right_y) / dt;
        }
    auto handle_paddle = [&](double px_left, double px_right, double py_top, double py_bottom, bool isLeft)->bool {
        // rectangle collision region
        if (s.ball_x >= px_left && s.ball_x <= px_right && s.ball_y >= py_top && s.ball_y <= py_bottom) {
            // collision with flat face: normal points horizontally
            double nx = isLeft ? 1.0 : -1.0;
            double ny = 0.0;
            // push ball outside
            if (isLeft) s.ball_x = px_right + ball_r + 1e-3;
            else s.ball_x = px_left - ball_r - 1e-3;
            // reflect velocity across normal
            double vdotn = vx*nx + vy*ny;
            vx = vx - 2.0 * vdotn * nx;
            vy = vy - 2.0 * vdotn * ny;
            // tangential impulse based on vertical contact offset and paddle velocity
            double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
            double midY_rect = (py_top + py_bottom) / 2.0;
            double ry_rect = (py_bottom - py_top) / 2.0;
            double contact_offset = 0.0;
            if (ry_rect > 1e-6) contact_offset = (s.ball_y - midY_rect) / ry_rect;
            if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
            double tx = -ny, ty = nx;
            double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
            vx += tx * tangential; vy += ty * tangential;
            vx *= restitution; vy *= restitution;
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
            double dx = s.ball_x - cx;
            double dy = s.ball_y - midY;
            double val = (dx*dx) / (rx_e*rx_e) + (dy*dy) / (ry_e*ry_e);
            if (val <= 1.0) {
                // push out to ellipse boundary: compute len in ellipse-space
                double lenp = sqrt((dx*dx)/(rx_e*rx_e) + (dy*dy)/(ry_e*ry_e));
                if (lenp < 1e-6) lenp = 1e-6;
                double new_dx = dx / lenp;
                double new_dy = dy / lenp;
                s.ball_x = cx + new_dx;
                s.ball_y = midY + new_dy;
                // compute normal for ellipse boundary more accurately
                double ex = (s.ball_x - cx) / (rx_e*rx_e);
                double ey = (s.ball_y - midY) / (ry_e*ry_e);
                double nlen = sqrt(ex*ex + ey*ey);
                if (nlen < 1e-6) nlen = 1e-6;
                double nx = ex / nlen;
                double ny = ey / nlen;
                double vdotn = vx*nx + vy*ny;
                vx = vx - 2.0 * vdotn * nx;
                vy = vy - 2.0 * vdotn * ny;
                // tangential impulse and restitution
                double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
                double contact_offset = (s.ball_y - midY) / ( (py_bottom - py_top)/2.0 );
                if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
                double tx = -ny, ty = nx;
                double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
                vx += tx * tangential; vy += ty * tangential;
                vx *= restitution; vy *= restitution;
                return true;
            }
        }
        // right ellipse collision
        {
            double cx = px_right;
            double dx = s.ball_x - cx;
            double dy = s.ball_y - midY;
            double val = (dx*dx) / (rx_e*rx_e) + (dy*dy) / (ry_e*ry_e);
            if (val <= 1.0) {
                double lenp = sqrt((dx*dx)/(rx_e*rx_e) + (dy*dy)/(ry_e*ry_e));
                if (lenp < 1e-6) lenp = 1e-6;
                double new_dx = dx / lenp;
                double new_dy = dy / lenp;
                s.ball_x = cx + new_dx;
                s.ball_y = midY + new_dy;
                double ex = (s.ball_x - cx) / (rx_e*rx_e);
                double ey = (s.ball_y - midY) / (ry_e*ry_e);
                double nlen = sqrt(ex*ex + ey*ey);
                if (nlen < 1e-6) nlen = 1e-6;
                double nx = ex / nlen;
                double ny = ey / nlen;
                double vdotn = vx*nx + vy*ny;
                vx = vx - 2.0 * vdotn * nx;
                vy = vy - 2.0 * vdotn * ny;
                double paddle_v = isLeft ? left_paddle_v : right_paddle_v;
                double contact_offset = (s.ball_y - midY) / ( (py_bottom - py_top)/2.0 );
                if (contact_offset > 1.0) contact_offset = 1.0; if (contact_offset < -1.0) contact_offset = -1.0;
                double tx = -ny, ty = nx;
                double tangential = tangent_strength * contact_offset + paddle_influence * paddle_v;
                vx += tx * tangential; vy += ty * tangential;
                vx *= restitution; vy *= restitution;
                return true;
            }
        }
        return false;
    };

    // left paddle collision
    double l_px_left = 1.0, l_px_right = 3.0;
    if (s.ball_x < l_px_right + 1.5) {
        if (handle_paddle(l_px_left, l_px_right, s.left_y, s.left_y + s.paddle_h, true)) {
            // ensure ball moves rightwards
            if (vx < 0) vx = fabs(vx);
            // clamp speed to avoid runaway
            double sp = sqrt(vx*vx + vy*vy);
            double maxsp = 80.0;
            if (sp > maxsp) { vx *= maxsp/sp; vy *= maxsp/sp; }
        } else if (s.ball_x < -1.0) {
            s.score_right++;
            s.ball_x = s.gw/2.0; s.ball_y = s.gh/2.0; vx = 20.0; vy = 10.0;
        }
    }

    // right paddle collision
    double r_px_left = s.gw - 3.0, r_px_right = s.gw - 1.0;
    if (s.ball_x > r_px_left - 1.5) {
        if (handle_paddle(r_px_left, r_px_right, s.right_y, s.right_y + s.paddle_h, false)) {
            if (vx > 0) vx = -fabs(vx);
            double sp = sqrt(vx*vx + vy*vy);
            double maxsp = 80.0;
            if (sp > maxsp) { vx *= maxsp/sp; vy *= maxsp/sp; }
        } else if (s.ball_x > s.gw + 1.0) {
            s.score_left++;
            s.ball_x = s.gw/2.0; s.ball_y = s.gh/2.0; vx = -20.0; vy = -10.0;
        }
    }

        // AI for right paddle (performed per frame, fine-grained control above)
        // (AI update outside of substep loop - handled after loop)
    }

    // compute paddle velocities (units per second)
    double left_paddle_v = (s.left_y - left_y_before) / dt;
    double right_paddle_v = (s.right_y - right_y_before) / dt;

    // AI for right paddle (after substeps)
    double target = s.ball_y - s.paddle_h/2.0;
    double dy = target - s.right_y;
    double max_speed = 25.0 * ai_speed * dt;
    if (fabs(dy) > max_speed) dy = (dy > 0 ? 1 : -1) * max_speed;
    s.right_y += dy;

    if (s.left_y < 0) s.left_y = 0;
    if (s.left_y + s.paddle_h > s.gh) s.left_y = s.gh - s.paddle_h;
    if (s.right_y < 0) s.right_y = 0;
    if (s.right_y + s.paddle_h > s.gh) s.right_y = s.gh - s.paddle_h;

    // store for next frame's velocity estimation
    prev_left_y = s.left_y;
    prev_right_y = s.right_y;
}

void GameCore::move_left_by(double dy) { s.left_y += dy; }
void GameCore::set_left_y(double y) { s.left_y = y; }
void GameCore::move_right_by(double dy) { s.right_y += dy; }

