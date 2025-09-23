cmake_minimum_required(VERSION 3.8)
project(pong LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pong
    src/main.cpp
    src/platform_win.cpp
    src/platform_posix.cpp
    src/game.cpp
    src/core/game_core.cpp
)

# Windowed Win32 Pong (no external libs)
if (WIN32)
    add_executable(pong_win
        src/win/game_win.cpp
        src/win/main_win.cpp
        src/win/highscores.cpp
        src/win/settings.cpp
        src/win/game_win.h
        src/core/game_core.cpp
    )
    target_link_libraries(pong_win PRIVATE user32 gdi32)
    set_target_properties(pong_win PROPERTIES WIN32_EXECUTABLE YES)
endif()

# Vulkan-based Pong
find_package(Vulkan)
if (Vulkan_FOUND)
    if (WIN32)
        add_executable(pong_vulkan
            src/vulkan/main_vulkan.cpp
            src/vulkan/game_vulkan.cpp
            src/vulkan/vulkan_renderer.cpp
            src/vulkan/game_vulkan.h
            src/vulkan/vulkan_renderer.h
            src/core/game_core.cpp
        )
        target_link_libraries(pong_vulkan PRIVATE user32)
        set_target_properties(pong_vulkan PROPERTIES WIN32_EXECUTABLE YES)
        target_compile_definitions(pong_vulkan PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    else()
        # Linux/Unix build
        find_package(X11 REQUIRED)
        add_executable(pong_vulkan
            src/vulkan/main_vulkan_linux.cpp
            src/vulkan/game_vulkan_linux.cpp
            src/vulkan/vulkan_renderer.cpp
            src/vulkan/game_vulkan_linux.h
            src/vulkan/vulkan_renderer.h
            src/core/game_core.cpp
        )
        target_link_libraries(pong_vulkan PRIVATE ${X11_LIBRARIES})
        target_include_directories(pong_vulkan PRIVATE ${X11_INCLUDE_DIR})
        target_compile_definitions(pong_vulkan PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    endif()
    
    target_include_directories(pong_vulkan PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(pong_vulkan PRIVATE ${Vulkan_LIBRARIES})
    
    message(STATUS "Vulkan found - building pong_vulkan target")
else()
    message(STATUS "Vulkan not found - skipping pong_vulkan target")
endif()

# On Windows enable linking to required system libraries (none external)
if (WIN32)
    # nothing required
endif()
