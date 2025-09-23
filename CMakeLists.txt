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

# On Windows enable linking to required system libraries (none external)
if (WIN32)
    # nothing required
endif()
