#pragma once

#include <memory>
#include <X11/Xlib.h>
#include "vulkan_renderer.h"

namespace pong {

// Linux X11-based Vulkan Pong implementation
class VulkanGameLinux {
public:
    VulkanGameLinux();
    ~VulkanGameLinux();
    
    int run();

private:
    bool initializeWindow();
    bool initializeVulkan();
    void shutdown();
    void gameLoop();
    void handleInput();
    void update(float deltaTime);
    void render();
    void handleEvent(XEvent& event);
    
    // X11 state
    Display* m_display = nullptr;
    Window m_window = 0;
    Atom m_wmDeleteMessage;
    int m_windowWidth = 800;
    int m_windowHeight = 600;
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
int run_vulkan_pong_linux();