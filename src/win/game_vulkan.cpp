/**
 * @file game_vulkan.cpp
 * @brief Vulkan-based Windows GUI implementation of PongCpp
 * 
 * This file implements a complete Windows GUI version of Pong using
 * Win32 APIs for windowing and Vulkan API for hardware-accelerated
 * rendering. Features include:
 * - DPI-aware window management
 * - Real-time mouse and keyboard input
 * - Settings and high score persistence (reused from GDI version)
 * - Context menus for configuration  
 * - Vulkan-based graphics rendering with enhanced visual effects
 */

#include "game_vulkan.h"
#include "../core/game_core.h"
#include "settings.h"
#include "highscores.h"
#include "../vulkan/vulkan_context.h"
#include "../vulkan/vulkan_memory.h"
#include "../vulkan/vulkan_renderer.h"
#include "../vulkan/slang_compiler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <algorithm>

/// Window class name for registration
static const wchar_t CLASS_NAME[] = L"PongVulkanWindowClass";

/**
 * @brief Window state and Vulkan resources
 * 
 * Contains all window-specific state, input handling, and Vulkan rendering
 * resources needed for the game loop.
 */
struct VulkanWinState {
    // Window properties
    int width = 800;
    int height = 600;
    int dpi = 96;
    bool running = true;
    
    // Input state
    bool key_down[256] = {};
    int mouse_x = 0;
    int mouse_y = 0;
    int last_click_x = -1;
    int last_click_y = -1;
    bool mouse_pressed = false;
    
    // UI state
    bool capture_name = false;
    std::wstring name_buf;
    int ui_mode = 0; // 0 = gameplay, 1 = menu, 2 = name entry
    int menu_click_index = -1;
    
    // Vulkan resources
    std::unique_ptr<VulkanContext> vk_context;
    std::unique_ptr<VulkanMemoryManager> vk_memory;
    std::unique_ptr<VulkanRenderer> vk_renderer;
    std::unique_ptr<SlangCompiler> slang_compiler;
    
    HINSTANCE hInstance = nullptr;
};

/**
 * @brief Window procedure for handling Win32 messages
 * 
 * Processes window messages including input events, resize, DPI changes,
 * and window lifecycle events. Updates the VulkanWinState accordingly.
 */
LRESULT CALLBACK VulkanWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static int msgCount = 0;
    msgCount++;
    
    // Log critical messages
    if (uMsg == WM_DESTROY || uMsg == WM_CLOSE || uMsg == WM_SIZE || uMsg == WM_DPICHANGED || msgCount % 100 == 0) {
        std::cout << "[DEBUG] WindowProc: Message " << msgCount << ", uMsg=0x" << std::hex << uMsg << std::dec << std::endl;
    }
    
    VulkanWinState *st = (VulkanWinState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
    case WM_CREATE:
        std::cout << "[DEBUG] WM_CREATE received" << std::endl;
        return 0;
        
    case WM_SIZE:
        if (st && st->vk_renderer) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            std::cout << "[DEBUG] WM_SIZE: Resizing to " << w << "x" << h << std::endl;
            st->width = w;
            st->height = h;
            // Vulkan swapchain recreation will be handled in render loop
        }
        return 0;
        
    case WM_DPICHANGED:
        if (st) {
            // Update DPI and apply suggested window size
            int newDpi = (int)LOWORD(wParam);
            st->dpi = newDpi;
            RECT* prc = (RECT*)lParam;
            if (prc) {
                SetWindowPos(hwnd, NULL, prc->left, prc->top, 
                    prc->right - prc->left, prc->bottom - prc->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                st->width = prc->right - prc->left;
                st->height = prc->bottom - prc->top;
            }
        }
        return 0;
        
    case WM_KEYDOWN:
        if (st && wParam < 256) {
            st->key_down[wParam] = true;
        }
        return 0;
        
    case WM_KEYUP:
        if (st && wParam < 256) {
            st->key_down[wParam] = false;
        }
        return 0;
        
    case WM_MOUSEMOVE:
        if (st) {
            st->mouse_x = LOWORD(lParam);
            st->mouse_y = HIWORD(lParam);
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        if (st) {
            st->mouse_pressed = true;
            st->last_click_x = LOWORD(lParam);
            st->last_click_y = HIWORD(lParam);
        }
        return 0;
        
    case WM_LBUTTONUP:
        if (st) {
            st->mouse_pressed = false;
        }
        return 0;
        
    case WM_RBUTTONDOWN:
        // Handle right-click context menu (similar to GDI version)
        if (st && st->ui_mode == 0) {
            st->ui_mode = 1; // Switch to menu mode
        }
        return 0;
        
    case WM_DESTROY:
        std::cout << "[DEBUG] WM_DESTROY received" << std::endl;
        if (st) {
            std::cout << "[DEBUG] Setting running=false" << std::endl;
            st->running = false;
        }
        std::cout << "[DEBUG] Posting quit message" << std::endl;
        PostQuitMessage(0);
        return 0;
        
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

/**
 * @brief Initialize Vulkan rendering system
 * 
 * Creates and initializes all Vulkan resources including context, memory manager,
 * compiler, and renderer. Returns false if initialization fails.
 */
static bool InitializeVulkan(VulkanWinState& state, HWND hwnd) {
    std::cout << "[DEBUG] InitializeVulkan() starting..." << std::endl;
    std::cout.flush();
    try {
        // Create Slang compiler first (needed for shaders)
        std::cout << "[DEBUG] Creating Slang compiler..." << std::endl;
        state.slang_compiler = std::make_unique<SlangCompiler>();
        std::cout << "[DEBUG] Slang compiler created, initializing..." << std::endl;
        if (!state.slang_compiler->initialize()) {
            std::cerr << "[ERROR] Failed to initialize Slang compiler" << std::endl;
            return false;
        }
        std::cout << "[DEBUG] Slang compiler initialized successfully." << std::endl;
        
        // Create Vulkan context
        std::cout << "[DEBUG] Creating Vulkan context..." << std::endl;
        state.vk_context = std::make_unique<VulkanContext>();
        std::cout << "[DEBUG] Vulkan context created, initializing with hwnd=" << hwnd << "..." << std::endl;
        if (!state.vk_context->initialize(hwnd, state.hInstance, true)) {  // Enable validation
            std::cerr << "[ERROR] Failed to initialize Vulkan context" << std::endl;
            return false;
        }
        std::cout << "[DEBUG] Vulkan context initialized successfully." << std::endl;
        
        // Create memory manager
        std::cout << "[DEBUG] Creating Vulkan memory manager..." << std::endl;
        state.vk_memory = std::make_unique<VulkanMemoryManager>();
        std::cout << "[DEBUG] Vulkan memory manager created." << std::endl;
        
        // Initialize memory manager
        std::cout << "[DEBUG] Initializing Vulkan memory manager..." << std::endl;
        if (!state.vk_memory->initialize(state.vk_context.get())) {
            std::cerr << "[ERROR] Failed to initialize Vulkan memory manager: " << state.vk_memory->getLastError() << std::endl;
            return false;
        }
        std::cout << "[DEBUG] Vulkan memory manager initialized successfully." << std::endl;
        
        // Create renderer
        std::cout << "[DEBUG] Creating Vulkan renderer..." << std::endl;
        state.vk_renderer = std::make_unique<VulkanRenderer>();
        std::cout << "[DEBUG] Vulkan renderer created, initializing with size " << state.width << "x" << state.height << "..." << std::endl;
        
        if (!state.vk_renderer->initialize(
            state.vk_context.get(), 
            state.vk_memory.get(), 
            state.slang_compiler.get(),
            state.width, 
            state.height)) {
            std::cerr << "[ERROR] Failed to initialize Vulkan renderer" << std::endl;
            return false;
        }
        std::cout << "[DEBUG] Vulkan renderer initialized successfully." << std::endl;
        
        // Set up game coordinates (same as Win32 version)
        std::cout << "[DEBUG] Setting up game coordinates..." << std::endl;
        state.vk_renderer->setGameCoordinates(100.0f, 75.0f);
        std::cout << "[DEBUG] Game coordinates set to 100x75." << std::endl;
        
        // Set up game coordinate system (matching Win32 version)
        std::cout << "[DEBUG] Initializing game coordinates..." << std::endl;
        state.vk_renderer->setGameCoordinates(100.0f, 75.0f);  // Game world: 100x75 units
        std::cout << "[DEBUG] Game coordinates initialized successfully." << std::endl;
        
        std::cout << "[DEBUG] All Vulkan components initialized successfully." << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Vulkan initialization failed with exception: " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "[ERROR] Vulkan initialization failed with unknown exception" << std::endl;
        return false;
    }
}

/**
 * @brief Main entry point for Vulkan-accelerated Pong game
 * 
 * Creates Win32 window, initializes Vulkan rendering, and runs the main
 * game loop with timing, input handling, and rendering.
 */
int run_vulkan_pong(HINSTANCE hInstance, int nCmdShow) {
    std::cout << "[DEBUG] run_vulkan_pong() starting..." << std::endl;
    std::cout.flush();
    
    // Register window class
    std::cout << "[DEBUG] Registering window class..." << std::endl;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = VulkanWindowProc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClassW(&wc)) {
        std::cerr << "[ERROR] Failed to register window class. Error: " << GetLastError() << std::endl;
        return -1;
    }
    std::cout << "[DEBUG] Window class registered successfully." << std::endl;
    
    // Create window state
    std::cout << "[DEBUG] Creating window state..." << std::endl;
    VulkanWinState state;
    state.hInstance = hInstance;
    std::cout << "[DEBUG] Window state created." << std::endl;
    
    // Create window
    std::cout << "[DEBUG] Creating window (" << state.width << "x" << state.height << ")..." << std::endl;
    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Pong (Vulkan)", 
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 
        state.width, state.height, 
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        std::cerr << "[ERROR] Failed to create window. Error: " << GetLastError() << std::endl;
        return -1;
    }
    std::cout << "[DEBUG] Window created successfully. HWND: " << hwnd << std::endl;
    std::cout.flush();
    
    // Associate state with window
    std::cout << "[DEBUG] Associating state with window..." << std::endl;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)&state);
    
    // Show window
    std::cout << "[DEBUG] Showing window..." << std::endl;
    ShowWindow(hwnd, nCmdShow);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    UpdateWindow(hwnd);
    std::cout << "[DEBUG] Window shown and focused." << std::endl;
    
    // Initialize Vulkan
    std::cout << "[DEBUG] Starting Vulkan initialization..." << std::endl;
    std::cout.flush();
    if (!InitializeVulkan(state, hwnd)) {
        std::cerr << "[ERROR] Vulkan initialization failed, destroying window." << std::endl;
        DestroyWindow(hwnd);
        return -1;
    }
    std::cout << "[DEBUG] Vulkan initialization completed successfully." << std::endl;
    
    // Load settings and high scores (reuse from GDI version)
    std::cout << "[DEBUG] Loading settings and high scores..." << std::endl;
    SettingsManager settingsMgr;
    HighScores hsMgr;
    
    std::wstring exeDir;
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        std::wstring sp(path);
        size_t pos = sp.find_last_of(L"\\/");
        exeDir = (pos == std::wstring::npos) ? L"" : sp.substr(0, pos + 1);
    }
    std::wcout << L"[DEBUG] Executable directory: " << exeDir << std::endl;
    
    std::cout << "[DEBUG] Loading settings from settings.json..." << std::endl;
    Settings settings = settingsMgr.load(exeDir + L"settings.json");
    std::cout << "[DEBUG] Loading high scores from highscores.json..." << std::endl;
    auto highList = hsMgr.load(exeDir + L"highscores.json", 10);
    int high_score = (highList.empty()) ? 0 : highList.front().score;
    std::cout << "[DEBUG] Settings and high scores loaded. High score: " << high_score << std::endl;
    
    // Game configuration
    std::cout << "[DEBUG] Setting up game configuration..." << std::endl;
    enum ControlMode { CTRL_KEYBOARD = 0, CTRL_MOUSE = 1 } ctrl = CTRL_KEYBOARD;
    enum AIDifficulty { AI_EASY = 0, AI_NORMAL = 1, AI_HARD = 2 } ai = AI_NORMAL;
    
    // Apply loaded settings
    if (settings.control_mode == 1) ctrl = CTRL_MOUSE;
    if (settings.ai == 0) ai = AI_EASY; 
    else if (settings.ai == 2) ai = AI_HARD; 
    else ai = AI_NORMAL;
    std::cout << "[DEBUG] Control mode: " << (ctrl == CTRL_MOUSE ? "MOUSE" : "KEYBOARD") << ", AI difficulty: " << (int)ai << std::endl;
    
    bool settings_changed = false;
    
    // Create game core
    std::cout << "[DEBUG] Creating game core..." << std::endl;
    GameCore core;
    std::cout << "[DEBUG] Game core created." << std::endl;
    
    // Game timing
    auto last = std::chrono::steady_clock::now();
    const double target_dt = 1.0 / 60.0;
    
    // UI state
    bool inMenu = false;  // Start directly in game mode for testing
    int menuIndex = 0; // 0: control, 1: ai, 2: start, 3: manage highscores, 4: quit
    
    std::cout << "[DEBUG] Starting main game loop..." << std::endl;
    int frameCount = 0;
    // Main game loop
    while (state.running) {
        frameCount++;
        if (frameCount % 60 == 0) {
            std::cout << "[DEBUG] Frame " << frameCount << " - Still running" << std::endl;
        }
        // Process Windows messages
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        if (!state.running) break;
        
        // Calculate frame time
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        
        // Limit frame rate
        if (dt < target_dt) {
            std::this_thread::sleep_for(
                std::chrono::microseconds((int)((target_dt - dt) * 1000000))
            );
            continue;
        }
        
        // Handle input and update game state
        if (inMenu) {
            // Menu navigation (keyboard)
            if (state.key_down[VK_UP] || state.key_down['W']) {
                menuIndex = (menuIndex - 1 + 5) % 5;
                // Debounce key
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            if (state.key_down[VK_DOWN] || state.key_down['S']) {
                menuIndex = (menuIndex + 1) % 5;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            
            // Menu selection
            if (state.key_down[VK_RETURN] || state.key_down[VK_SPACE] || 
                (state.last_click_x >= 0 && state.last_click_y >= 0)) {
                    
                switch (menuIndex) {
                case 0: // Toggle control mode
                    ctrl = (ctrl == CTRL_KEYBOARD) ? CTRL_MOUSE : CTRL_KEYBOARD;
                    settings.control_mode = (ctrl == CTRL_MOUSE) ? 1 : 0;
                    settings_changed = true;
                    break;
                case 1: // Toggle AI difficulty
                    ai = (AIDifficulty)((ai + 1) % 3);
                    settings.ai = (int)ai;
                    settings_changed = true;
                    break;
                case 2: // Start game
                    inMenu = false;
                    core.reset();
                    break;
                case 3: // Manage high scores (simplified for now)
                    // TODO: Implement high score management UI
                    break;
                case 4: // Quit
                    state.running = false;
                    break;
                }
                
                // Clear click state
                state.last_click_x = -1;
                state.last_click_y = -1;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        } else {
            // Game input handling
            if (ctrl == CTRL_KEYBOARD) {
                if (state.key_down['W'] || state.key_down[VK_UP]) {
                    // Move paddle up (negative Y in game coordinates)
                    core.move_left_by(-dt * 50.0); // Move at 50 units/second
                } else if (state.key_down['S'] || state.key_down[VK_DOWN]) {
                    // Move paddle down (positive Y in game coordinates)
                    core.move_left_by(dt * 50.0);
                }
            } else {
                // Mouse control - map mouse Y to paddle position
                double mouse_norm = (double)state.mouse_y / state.height;
                mouse_norm = (std::max)(0.0, (std::min)(1.0, mouse_norm));
                double target_y = mouse_norm * 75.0; // Game height is 75
                core.set_left_y(target_y);
            }
            
            // Update game
            core.update(dt);
            
            // Check for escape to return to menu
            if (state.key_down[VK_ESCAPE]) {
                inMenu = true;
                state.key_down[VK_ESCAPE] = false;
            }
        }
        
        // Render frame
        if (frameCount % 60 == 0) {
            std::cout << "[DEBUG] Starting frame render..." << std::endl;
        }
        try {
            if (frameCount % 60 == 0) {
                std::cout << "[DEBUG] Calling beginFrame()..." << std::endl;
            }
            state.vk_renderer->beginFrame();
            
            if (inMenu) {
                // Render menu - simple colored background for now
                // TODO: Implement menu rendering using text/UI system
                
            } else {
                // Normal rendering path restored. Debug-only test triangle was removed.
                // All game geometry will be rendered using the usual game rendering code below.
                
                auto& gs = core.state();
                double ui_scale = (double)state.dpi / 96.0;
                
                // Draw center line (dashed) - using game coordinates
                for (int i = 0; i < 10; i++) {
                    float y = (i * 2 + 1) * 75.0f / 20.0f;  // Distribute segments across game height (75 units)
                    state.vk_renderer->drawRectangle(
                        VulkanMath::Vec2(50.0f, y),      // Center X in game coordinates (50 out of 100)
                        VulkanMath::Vec2(1.0f, 2.0f),    // Size in game coordinates
                        VulkanMath::Vec3(1.0f, 0.0f, 0.0f)  // BRIGHT RED
                    );
                }
                
                // Draw paddles - using game coordinates directly
                // Debug: Print game state once per 60 frames
                static int debug_counter = 0;
                if (debug_counter++ % 60 == 0) {
                    std::cout << "[DEBUG] Game state: ball(" << gs.ball_x << "," << gs.ball_y 
                              << "), left_y=" << gs.left_y << ", right_y=" << gs.right_y 
                              << ", paddle_h=" << gs.paddle_h << std::endl;
                }
                
                // Left paddle
                state.vk_renderer->drawRectangle(
                    VulkanMath::Vec2(2.0f, (float)(gs.left_y + gs.paddle_h/2)),
                    VulkanMath::Vec2(1.0f, (float)gs.paddle_h),
                    VulkanMath::Vec3(0.0f, 1.0f, 0.0f)  // BRIGHT GREEN for visibility
                );
                
                // Right paddle
                state.vk_renderer->drawRectangle(
                    VulkanMath::Vec2(98.0f, (float)(gs.right_y + gs.paddle_h/2)),
                    VulkanMath::Vec2(1.0f, (float)gs.paddle_h),
                    VulkanMath::Vec3(0.0f, 1.0f, 0.0f)  // BRIGHT GREEN for visibility
                );
                
                // Draw ball - using game coordinates directly
                state.vk_renderer->drawCircle(
                    VulkanMath::Vec2((float)gs.ball_x, (float)gs.ball_y),
                    1.0f,  // Ball radius in game coordinates
                    VulkanMath::Vec3(1.0f, 1.0f, 0.0f)  // BRIGHT YELLOW for visibility
                );
                
                // TODO: Implement text rendering for scores
            }
            
            if (frameCount % 60 == 0) {
                std::cout << "[DEBUG] Calling endFrame()..." << std::endl;
            }
            state.vk_renderer->endFrame();
            
            // Debug: Sample pixel colors to see what's actually being rendered
            state.vk_renderer->debugSamplePixelColors(frameCount);
            
            if (frameCount % 60 == 0) {
                std::cout << "[DEBUG] Frame render completed successfully." << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] Render error at frame " << frameCount << ": " << e.what() << std::endl;
            std::cerr << "[ERROR] Breaking from main loop due to render error." << std::endl;
            break;
        }
        catch (...) {
            std::cerr << "[ERROR] Unknown render error at frame " << frameCount << std::endl;
            std::cerr << "[ERROR] Breaking from main loop due to unknown render error." << std::endl;
            break;
        }
    }
    
    std::cout << "[DEBUG] Main loop exited. Cleaning up..." << std::endl;
    
    // Save settings if changed
    if (settings_changed) {
        std::cout << "[DEBUG] Saving changed settings..." << std::endl;
        settingsMgr.save(exeDir + L"settings.json", settings);
        std::cout << "[DEBUG] Settings saved." << std::endl;
    }
    
    // Cleanup is handled by destructors
    std::cout << "[DEBUG] Destroying window..." << std::endl;
    DestroyWindow(hwnd);
    std::cout << "[DEBUG] Window destroyed. Exiting with code 0." << std::endl;
    return 0;
}