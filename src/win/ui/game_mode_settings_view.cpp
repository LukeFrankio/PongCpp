/**
 * @file game_mode_settings_view.cpp
 * @brief Implementation of game mode settings UI
 */

#include "game_mode_settings_view.h"
#include "../input/input_state.h"
#include <algorithm>
#include <cwchar>
#include <vector>

void GameModeSettingsView::begin(GameModeConfig* config) {
    config_ = config;
    original_ = *config;
    changedSinceOpen_ = false;
    sel_ = 0;
    scrollOffset_ = 0;
    maxScroll_ = 0;
}

void GameModeSettingsView::clampSel() {
    if (sel_ < 0) sel_ = 0;
    if (sel_ > totalItems_() - 1) sel_ = totalItems_() - 1;
}

GameModeSettingsView::Action GameModeSettingsView::frame(HDC memDC,
                                                          int winW, int winH, int dpi,
                                                          const InputState& input,
                                                          int mouse_x, int mouse_y, bool mouse_pressed,
                                                          int& mouse_wheel_delta,
                                                          int& last_click_x, int& last_click_y) {
    if (!config_) return Action::None;
    
    double ui_scale = (double)dpi / 96.0;
    
    // Background
    RECT bg {0,0,winW,winH};
    HBRUSH b = CreateSolidBrush(RGB(15,15,25));
    FillRect(memDC,&bg,b);
    DeleteObject(b);
    
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(235,235,245));
    
    // Title
    RECT trTitle {0,(int)(10*ui_scale),winW,(int)(90*ui_scale)};
    DrawTextW(memDC, L"Game Mode Settings", -1, &trTitle, DT_CENTER|DT_TOP|DT_SINGLELINE);
    
    // Description
    RECT trDesc {0,(int)(50*ui_scale),winW,(int)(80*ui_scale)};
    DrawTextW(memDC, L"Customize your game mode by toggling features", -1, &trDesc, DT_CENTER|DT_TOP|DT_SINGLELINE);
    
    int centerX = winW/2;
    int baseY = (int)(110*ui_scale + 0.5) - scrollOffset_;
    int rowH = (int)(50*ui_scale + 0.5);
    int bottomPanelH = (int)(100*ui_scale + 0.5);
    
    // Handle scrolling
    if (mouse_wheel_delta != 0) {
        scrollOffset_ -= (mouse_wheel_delta / 120) * 40;
        if (scrollOffset_ < 0) scrollOffset_ = 0;
        if (scrollOffset_ > maxScroll_) scrollOffset_ = maxScroll_;
        mouse_wheel_delta = 0;
    }
    
    // Keyboard navigation
    if (input.just_pressed(VK_UP)) { sel_--; clampSel(); }
    if (input.just_pressed(VK_DOWN)) { sel_++; clampSel(); }
    if (input.just_pressed(VK_RETURN)) return Action::Commit;
    if (input.just_pressed(VK_ESCAPE)) {
        *config_ = original_;
        return Action::Cancel;
    }
    
    auto drawToggle = [&](const wchar_t* label, bool value, int cy, bool hot) {
        RECT r {centerX - 250, cy, centerX + 250, cy + 40};
        if (hot) {
            HBRUSH hb = CreateSolidBrush(RGB(40,40,60));
            FillRect(memDC, &r, hb);
            DeleteObject(hb);
        }
        
        wchar_t buf[256];
        swprintf(buf, 256, L"%s: %s", label, value ? L"ON" : L"OFF");
        DrawTextW(memDC, buf, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    };
    
    auto drawSlider = [&](const wchar_t* label, int value, int minVal, int maxVal, int cy, bool hot) {
        // Background highlight if selected
        RECT bgRect {centerX - 250, cy, centerX + 250, cy + 40};
        if (hot) {
            HBRUSH hb = CreateSolidBrush(RGB(40,40,60));
            FillRect(memDC, &bgRect, hb);
            DeleteObject(hb);
        }
        
        // Label and value text
        wchar_t buf[256];
        swprintf(buf, 256, L"%s: %d", label, value);
        RECT labelRect {centerX - 240, cy, centerX + 240, cy + 18};
        SetTextColor(memDC, hot ? RGB(255,240,160) : RGB(200,200,210));
        DrawTextW(memDC, buf, -1, &labelRect, DT_CENTER|DT_TOP|DT_SINGLELINE);
        
        // Slider bar
        int barW = 400;
        int barH = (int)(6*ui_scale + 0.5);
        int bx = centerX - barW/2;
        int by = cy + 22;
        
        RECT barBg {bx, by, bx + barW, by + barH};
        HBRUSH bgBrush = CreateSolidBrush(RGB(50,60,80));
        FillRect(memDC, &barBg, bgBrush);
        DeleteObject(bgBrush);
        
        // Filled portion based on value
        double t = double(value - minVal) / double(maxVal - minVal);
        RECT barFill {bx, by, bx + (int)(barW * t), by + barH};
        HBRUSH fillBrush = CreateSolidBrush(hot ? RGB(120,180,255) : RGB(90,120,180));
        FillRect(memDC, &barFill, fillBrush);
        DeleteObject(fillBrush);
    };
    
    // Draw items and track their positions for mouse clicks
    int currentY = baseY;
    struct ItemRect { int idx; RECT r; bool isSlider; };
    std::vector<ItemRect> itemRects;
    
    auto addItemRect = [&](int idx, int y, bool isSlider) {
        RECT r {centerX - 250, y, centerX + 250, y + 40};
        itemRects.push_back({idx, r, isSlider});
    };
    
    drawToggle(L"MultiBall", config_->multiball, currentY, sel_ == idxMultiball_());
    addItemRect(idxMultiball_(), currentY, false);
    currentY += rowH;
    
    if (config_->multiball) {
        drawSlider(L"  Ball Count", config_->multiball_count, 2, 5, currentY, sel_ == idxMultiballCount_());
        addItemRect(idxMultiballCount_(), currentY, true);
        currentY += rowH;
    }
    
    drawToggle(L"Obstacles", config_->obstacles, currentY, sel_ == idxObstacles_());
    addItemRect(idxObstacles_(), currentY, false);
    currentY += rowH;
    
    if (config_->obstacles) {
        drawToggle(L"  Moving Obstacles", config_->obstacles_moving, currentY, sel_ == idxObstaclesMoving_());
        addItemRect(idxObstaclesMoving_(), currentY, false);
        currentY += rowH;
        
        drawToggle(L"  Gravity from Black Holes", config_->obstacles_gravity, currentY, sel_ == idxObstaclesGravity_());
        addItemRect(idxObstaclesGravity_(), currentY, false);
        currentY += rowH;
    }
    
    drawToggle(L"Black Holes", config_->blackholes, currentY, sel_ == idxBlackholes_());
    addItemRect(idxBlackholes_(), currentY, false);
    currentY += rowH;
    
    if (config_->blackholes) {
        drawToggle(L"  Moving Black Holes", config_->blackholes_moving, currentY, sel_ == idxBlackholesMoving_());
        addItemRect(idxBlackholesMoving_(), currentY, false);
        currentY += rowH;
        
        drawSlider(L"  Black Hole Count", config_->blackhole_count, 1, 5, currentY, sel_ == idxBlackholeCount_());
        addItemRect(idxBlackholeCount_(), currentY, true);
        currentY += rowH;
        
        drawToggle(L"  Destroy Balls on Contact", config_->blackholes_destroy_balls, currentY, sel_ == idxBlackholesDestroyBalls_());
        addItemRect(idxBlackholesDestroyBalls_(), currentY, false);
        currentY += rowH;
    }
    
    drawToggle(L"Three Enemies", config_->three_enemies, currentY, sel_ == idxThreeEnemies_());
    addItemRect(idxThreeEnemies_(), currentY, false);
    currentY += rowH;
    
    // Calculate scroll range
    int usableHeight = winH - bottomPanelH;
    int contentBottom = currentY + (int)(20*ui_scale);
    maxScroll_ = std::max(0, contentBottom - usableHeight);
    
    // Handle input for selected item
    bool changed = false;
    
    if (sel_ == idxMultiball_() && input.just_pressed(VK_SPACE)) {
        config_->multiball = !config_->multiball;
        changed = true;
    }
    else if (sel_ == idxMultiballCount_() && config_->multiball) {
        if (input.just_pressed(VK_LEFT) && config_->multiball_count > 2) {
            config_->multiball_count--;
            changed = true;
        }
        if (input.just_pressed(VK_RIGHT) && config_->multiball_count < 5) {
            config_->multiball_count++;
            changed = true;
        }
    }
    else if (sel_ == idxObstacles_() && input.just_pressed(VK_SPACE)) {
        config_->obstacles = !config_->obstacles;
        changed = true;
    }
    else if (sel_ == idxObstaclesMoving_() && config_->obstacles && input.just_pressed(VK_SPACE)) {
        config_->obstacles_moving = !config_->obstacles_moving;
        changed = true;
    }
    else if (sel_ == idxObstaclesGravity_() && config_->obstacles && input.just_pressed(VK_SPACE)) {
        config_->obstacles_gravity = !config_->obstacles_gravity;
        changed = true;
    }
    else if (sel_ == idxBlackholes_() && input.just_pressed(VK_SPACE)) {
        config_->blackholes = !config_->blackholes;
        changed = true;
    }
    else if (sel_ == idxBlackholesMoving_() && config_->blackholes && input.just_pressed(VK_SPACE)) {
        config_->blackholes_moving = !config_->blackholes_moving;
        changed = true;
    }
    else if (sel_ == idxBlackholeCount_() && config_->blackholes) {
        if (input.just_pressed(VK_LEFT) && config_->blackhole_count > 1) {
            config_->blackhole_count--;
            changed = true;
        }
        if (input.just_pressed(VK_RIGHT) && config_->blackhole_count < 5) {
            config_->blackhole_count++;
            changed = true;
        }
    }
    else if (sel_ == idxBlackholesDestroyBalls_() && config_->blackholes && input.just_pressed(VK_SPACE)) {
        config_->blackholes_destroy_balls = !config_->blackholes_destroy_balls;
        changed = true;
    }
    else if (sel_ == idxThreeEnemies_() && input.just_pressed(VK_SPACE)) {
        config_->three_enemies = !config_->three_enemies;
        changed = true;
    }
    
    if (changed) changedSinceOpen_ = true;
    
    // Bottom panel with buttons
    int panelY = winH - bottomPanelH;
    RECT panelRect {0, panelY, winW, winH};
    HBRUSH panelBrush = CreateSolidBrush(RGB(20,20,30));
    FillRect(memDC, &panelRect, panelBrush);
    DeleteObject(panelBrush);
    
    // Draw buttons
    int btnW = 120;
    int btnH = 40;
    int btnY = panelY + 30;
    
    RECT applyBtn {centerX - btnW - 20, btnY, centerX - 20, btnY + btnH};
    RECT cancelBtn {centerX + 20, btnY, centerX + btnW + 20, btnY + btnH};
    
    HBRUSH applyBrush = CreateSolidBrush(RGB(50,120,50));
    FillRect(memDC, &applyBtn, applyBrush);
    DeleteObject(applyBrush);
    DrawTextW(memDC, L"Apply", -1, &applyBtn, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    
    HBRUSH cancelBrush = CreateSolidBrush(RGB(120,50,50));
    FillRect(memDC, &cancelBtn, cancelBrush);
    DeleteObject(cancelBrush);
    DrawTextW(memDC, L"Cancel", -1, &cancelBtn, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    
    // Handle mouse clicks on buttons
    if (input.click && last_click_x >= 0 && last_click_y >= 0) {
        // Check Apply/Cancel buttons first
        if (last_click_x >= applyBtn.left && last_click_x <= applyBtn.right &&
            last_click_y >= applyBtn.top && last_click_y <= applyBtn.bottom) {
            last_click_x = last_click_y = -1;
            return Action::Commit;
        }
        if (last_click_x >= cancelBtn.left && last_click_x <= cancelBtn.right &&
            last_click_y >= cancelBtn.top && last_click_y <= cancelBtn.bottom) {
            *config_ = original_;
            last_click_x = last_click_y = -1;
            return Action::Cancel;
        }
        
        // Check item clicks (toggles and sliders)
        for (const auto& item : itemRects) {
            if (last_click_x >= item.r.left && last_click_x <= item.r.right &&
                last_click_y >= item.r.top && last_click_y <= item.r.bottom) {
                
                // Update selection to clicked item
                sel_ = item.idx;
                
                // For toggles, toggle the value on click
                if (!item.isSlider) {
                    if (item.idx == idxMultiball_()) {
                        config_->multiball = !config_->multiball;
                        changed = true;
                    }
                    else if (item.idx == idxObstacles_()) {
                        config_->obstacles = !config_->obstacles;
                        changed = true;
                    }
                    else if (item.idx == idxObstaclesMoving_()) {
                        config_->obstacles_moving = !config_->obstacles_moving;
                        changed = true;
                    }
                    else if (item.idx == idxObstaclesGravity_()) {
                        config_->obstacles_gravity = !config_->obstacles_gravity;
                        changed = true;
                    }
                    else if (item.idx == idxBlackholes_()) {
                        config_->blackholes = !config_->blackholes;
                        changed = true;
                    }
                    else if (item.idx == idxBlackholesMoving_()) {
                        config_->blackholes_moving = !config_->blackholes_moving;
                        changed = true;
                    }
                    else if (item.idx == idxBlackholesDestroyBalls_()) {
                        config_->blackholes_destroy_balls = !config_->blackholes_destroy_balls;
                        changed = true;
                    }
                    else if (item.idx == idxThreeEnemies_()) {
                        config_->three_enemies = !config_->three_enemies;
                        changed = true;
                    }
                }
                // For sliders, clicking the left half decreases, right half increases
                else {
                    int clickX = last_click_x - item.r.left;
                    int itemWidth = item.r.right - item.r.left;
                    bool clickedLeft = clickX < itemWidth / 2;
                    
                    if (item.idx == idxMultiballCount_()) {
                        if (clickedLeft && config_->multiball_count > 2) {
                            config_->multiball_count--;
                            changed = true;
                        }
                        else if (!clickedLeft && config_->multiball_count < 5) {
                            config_->multiball_count++;
                            changed = true;
                        }
                    }
                    else if (item.idx == idxBlackholeCount_()) {
                        if (clickedLeft && config_->blackhole_count > 1) {
                            config_->blackhole_count--;
                            changed = true;
                        }
                        else if (!clickedLeft && config_->blackhole_count < 5) {
                            config_->blackhole_count++;
                            changed = true;
                        }
                    }
                }
                
                break;
            }
        }
        
        last_click_x = last_click_y = -1;
    }
    
    if (changed) changedSinceOpen_ = true;
    
    return Action::None;
}
