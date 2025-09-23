#include "game_vulkan.h"
#include "../core/game_core.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace pong {

static const wchar_t VULKAN_CLASS_NAME[] = L"PongVulkanWindowClass";

VulkanGame::VulkanGame() : m_renderer(std::make_unique<VulkanRenderer>()) {
}

VulkanGame::~VulkanGame() {
    shutdown();
}

int VulkanGame::run(HINSTANCE hInstance, int nCmdShow) {
    if (!initializeWindow(hInstance, nCmdShow)) {
        std::cerr << "Failed to initialize window" << std::endl;
        return -1;
    }
    
    if (!initializeVulkan()) {
        std::cerr << "Failed to initialize Vulkan renderer" << std::endl;
        return -1;
    }
    
    gameLoop();
    
    return 0;
}

bool VulkanGame::initializeWindow(HINSTANCE hInstance, int nCmdShow) {
    m_hinstance = hInstance;
    
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = VULKAN_CLASS_NAME;
    
    if (!RegisterClassExW(&wc)) {
        return false;
    }
    
    // Create window
    m_hwnd = CreateWindowExW(
        0,
        VULKAN_CLASS_NAME,
        L"Pong - Vulkan",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        m_windowWidth,
        m_windowHeight,
        nullptr,
        nullptr,
        hInstance,
        this
    );
    
    if (!m_hwnd) {
        return false;
    }
    
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    
    return true;
}

bool VulkanGame::initializeVulkan() {
    VulkanInitInfo initInfo{};
    initInfo.appName = "Pong - Vulkan";
    initInfo.appVersion = VK_MAKE_VERSION(1, 0, 0);
    initInfo.enableValidationLayers = true; // Enable for development
    initInfo.windowWidth = m_windowWidth;
    initInfo.windowHeight = m_windowHeight;
    initInfo.hwnd = m_hwnd;
    initInfo.hinstance = m_hinstance;
    
    return m_renderer->initialize(initInfo);
}

void VulkanGame::shutdown() {
    if (m_renderer) {
        m_renderer->shutdown();
    }
    
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    
    if (m_hinstance) {
        UnregisterClassW(VULKAN_CLASS_NAME, m_hinstance);
    }
}

void VulkanGame::gameLoop() {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    MSG msg = {};
    while (m_running) {
        // Handle Windows messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
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

void VulkanGame::handleInput() {
    // Handle keyboard input for quit
    if (m_keyDown['Q']) {
        m_running = false;
    }
    
    // TODO: Handle game-specific input (paddle movement, menu navigation)
}

void VulkanGame::update(float deltaTime) {
    // TODO: Update game logic
    // - Update paddle positions
    // - Update ball physics
    // - Check collisions
    // - Update score
}

void VulkanGame::render() {
    if (!m_renderer->isInitialized()) {
        return;
    }
    
    if (!m_renderer->beginFrame()) {
        return;
    }
    
    // Clear background
    m_renderer->clear(0.0f, 0.0f, 0.1f, 1.0f);
    
    // TODO: Render game objects
    // - Draw paddles
    // - Draw ball
    // - Draw score
    // - Draw UI elements
    
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

LRESULT CALLBACK VulkanGame::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    VulkanGame* game = nullptr;
    
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        game = reinterpret_cast<VulkanGame*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(game));
    } else {
        game = reinterpret_cast<VulkanGame*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (game) {
        switch (uMsg) {
        case WM_DESTROY:
            game->m_running = false;
            PostQuitMessage(0);
            return 0;
            
        case WM_SIZE:
            {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                game->m_windowWidth = width;
                game->m_windowHeight = height;
                // TODO: Handle window resize for Vulkan swapchain recreation
            }
            return 0;
            
        case WM_KEYDOWN:
            if (wParam < 256) {
                game->m_keyDown[wParam] = true;
            }
            return 0;
            
        case WM_KEYUP:
            if (wParam < 256) {
                game->m_keyDown[wParam] = false;
            }
            return 0;
            
        case WM_MOUSEMOVE:
            game->m_mouseX = LOWORD(lParam);
            game->m_mouseY = HIWORD(lParam);
            return 0;
            
        case WM_LBUTTONDOWN:
            game->m_mousePressed = true;
            return 0;
            
        case WM_LBUTTONUP:
            game->m_mousePressed = false;
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace pong

// C-style entry point for compatibility
int run_vulkan_pong(HINSTANCE hInstance, int nCmdShow) {
    pong::VulkanGame game;
    return game.run(hInstance, nCmdShow);
}