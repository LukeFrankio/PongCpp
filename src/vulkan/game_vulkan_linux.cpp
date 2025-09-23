#include "game_vulkan_linux.h"
#include "../core/game_core.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

namespace pong {

VulkanGameLinux::VulkanGameLinux() : m_renderer(std::make_unique<VulkanRenderer>()) {
}

VulkanGameLinux::~VulkanGameLinux() {
    shutdown();
}

int VulkanGameLinux::run() {
    if (!initializeWindow()) {
        std::cerr << "Failed to initialize X11 window" << std::endl;
        return -1;
    }
    
    if (!initializeVulkan()) {
        std::cerr << "Failed to initialize Vulkan renderer" << std::endl;
        return -1;
    }
    
    gameLoop();
    
    return 0;
}

bool VulkanGameLinux::initializeWindow() {
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        std::cerr << "Cannot open X11 display" << std::endl;
        return false;
    }
    
    int screen = DefaultScreen(m_display);
    Window root = RootWindow(m_display, screen);
    
    // Create window
    m_window = XCreateSimpleWindow(
        m_display,
        root,
        0, 0,
        m_windowWidth, m_windowHeight,
        1,
        BlackPixel(m_display, screen),
        BlackPixel(m_display, screen)
    );
    
    if (!m_window) {
        std::cerr << "Failed to create X11 window" << std::endl;
        return false;
    }
    
    // Set window properties
    XStoreName(m_display, m_window, "Pong - Vulkan");
    
    // Handle window close events
    m_wmDeleteMessage = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &m_wmDeleteMessage, 1);
    
    // Select input events
    XSelectInput(m_display, m_window, 
                 ExposureMask | KeyPressMask | KeyReleaseMask | 
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    
    // Map window
    XMapWindow(m_display, m_window);
    XFlush(m_display);
    
    return true;
}

bool VulkanGameLinux::initializeVulkan() {
    VulkanInitInfo initInfo{};
    initInfo.appName = "Pong - Vulkan";
    initInfo.appVersion = VK_MAKE_VERSION(1, 0, 0);
    initInfo.enableValidationLayers = true; // Enable for development
    initInfo.windowWidth = m_windowWidth;
    initInfo.windowHeight = m_windowHeight;
    initInfo.display = m_display;
    initInfo.window = m_window;
    
    return m_renderer->initialize(initInfo);
}

void VulkanGameLinux::shutdown() {
    if (m_renderer) {
        m_renderer->shutdown();
    }
    
    if (m_display) {
        if (m_window) {
            XDestroyWindow(m_display, m_window);
            m_window = 0;
        }
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

void VulkanGameLinux::gameLoop() {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    while (m_running) {
        // Handle X11 events
        while (XPending(m_display)) {
            XEvent event;
            XNextEvent(m_display, &event);
            handleEvent(event);
        }
        
        if (!m_running) break;
        
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
        startTime = currentTime;
        
        // Update game logic
        handleInput();
        update(deltaTime);
        
        // Render frame
        render();
        
        // Cap framerate to ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void VulkanGameLinux::handleEvent(XEvent& event) {
    switch (event.type) {
    case ClientMessage:
        if ((Atom)event.xclient.data.l[0] == m_wmDeleteMessage) {
            m_running = false;
        }
        break;
        
    case ConfigureNotify:
        if (event.xconfigure.width != m_windowWidth || event.xconfigure.height != m_windowHeight) {
            m_windowWidth = event.xconfigure.width;
            m_windowHeight = event.xconfigure.height;
            // TODO: Handle window resize for Vulkan swapchain recreation
        }
        break;
        
    case KeyPress:
        {
            KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key < 256) {
                m_keyDown[key] = true;
            }
        }
        break;
        
    case KeyRelease:
        {
            KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key < 256) {
                m_keyDown[key] = false;
            }
        }
        break;
        
    case ButtonPress:
        if (event.xbutton.button == Button1) {
            m_mousePressed = true;
        }
        break;
        
    case ButtonRelease:
        if (event.xbutton.button == Button1) {
            m_mousePressed = false;
        }
        break;
        
    case MotionNotify:
        m_mouseX = event.xmotion.x;
        m_mouseY = event.xmotion.y;
        break;
    }
}

void VulkanGameLinux::handleInput() {
    // Handle keyboard input for quit ('q' key)
    if (m_keyDown['q'] || m_keyDown['Q']) {
        m_running = false;
    }
    
    // TODO: Handle game-specific input (paddle movement, menu navigation)
}

void VulkanGameLinux::update(float deltaTime) {
    // TODO: Update game logic
    // - Update paddle positions
    // - Update ball physics
    // - Check collisions
    // - Update score
}

void VulkanGameLinux::render() {
    if (!m_renderer->isInitialized()) {
        return;
    }
    
    if (!m_renderer->beginFrame()) {
        return;
    }
    
    // Clear background
    m_renderer->clear(0.0f, 0.0f, 0.1f, 1.0f);
    
    // TODO: Render game objects
    // For now, just draw some placeholder rectangles
    float centerX = m_renderer->getFramebufferWidth() / 2.0f;
    float centerY = m_renderer->getFramebufferHeight() / 2.0f;
    
    // Left paddle placeholder
    m_renderer->drawRect(50, centerY - 50, 10, 100, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // Right paddle placeholder
    m_renderer->drawRect(m_renderer->getFramebufferWidth() - 60, centerY - 50, 10, 100, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // Ball placeholder
    m_renderer->drawRect(centerX - 5, centerY - 5, 10, 10, 1.0f, 1.0f, 1.0f, 1.0f);
    
    m_renderer->endFrame();
}

} // namespace pong

// C-style entry point for compatibility
int run_vulkan_pong_linux() {
    pong::VulkanGameLinux game;
    return game.run();
}