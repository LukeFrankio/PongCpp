#pragma once

#include <windows.h>
#include <memory>
#include "vulkan_renderer.h"

namespace pong {

// Vulkan-based windowed Pong implementation
// This is meant to replace the GDI-based game_win.cpp implementation

class VulkanGame {
public:
    VulkanGame();
    ~VulkanGame();
    
    int run(HINSTANCE hInstance, int nCmdShow);

private:
    bool initializeWindow(HINSTANCE hInstance, int nCmdShow);
    bool initializeVulkan();
    void shutdown();
    void gameLoop();
    void handleInput();
    void update(float deltaTime);
    void render();
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    // Window state
    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    int m_windowWidth = 800;
    int m_windowHeight = 600;
    int m_dpi = 96;
    bool m_running = true;
    
    // Input state
    bool m_keyDown[256] = {};
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mousePressed = false;
    
    // Game state
    int m_uiMode = 0; // 0 = gameplay, 1 = menu
    
    // Rendering
    std::unique_ptr<VulkanRenderer> m_renderer;
    
    // Timing
    double m_lastTime = 0.0;
};

} // namespace pong

// C-style entry point for compatibility
int run_vulkan_pong(HINSTANCE hInstance, int nCmdShow);