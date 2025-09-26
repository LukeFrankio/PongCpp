cmake_minimum_required(VERSION 3.10)
project(pong LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set up dist directory for final output with configuration subdirectories
set(DIST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dist)
file(MAKE_DIRECTORY ${DIST_DIR})
file(MAKE_DIRECTORY ${DIST_DIR}/debug)
file(MAKE_DIRECTORY ${DIST_DIR}/release)

# Set runtime output directory to dist/config for all configurations
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${DIST_DIR}/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${DIST_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${DIST_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${DIST_DIR}/debug)

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
        src/win/soft_renderer.cpp
        src/win/game_win.h
        src/win/soft_renderer.h
        src/core/game_core.cpp
    )
    target_link_libraries(pong_win PRIVATE user32 gdi32)
    set_target_properties(pong_win PROPERTIES WIN32_EXECUTABLE YES)
endif()

# On Windows enable linking to required system libraries (none external)
if (WIN32)
    # nothing required
endif()

# Documentation generation with Doxygen
option(BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" OFF)

if(BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    
    if(DOXYGEN_FOUND)
        # Set input and output files
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        
        # Request to configure the file
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        message("Doxygen build started")
        
        # Note: do not put "ALL" - this builds docs together with application EVERY time!
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM)
            
        # Install documentation
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen/html/ 
                    DESTINATION ${CMAKE_INSTALL_PREFIX}/docs 
                    OPTIONAL)
        endif()
    else()
        message("Doxygen needs to be installed to generate the documentation")
    endif()
endif()

# Custom target for cleaning documentation
add_custom_target(clean-docs
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen
    COMMENT "Cleaning generated documentation")

# Custom target to ensure dist directories exist
add_custom_target(setup-dist ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/debug
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/release
    COMMENT "Setting up dist directories")

# Make executables depend on dist setup
add_dependencies(pong setup-dist)
if(WIN32)
    add_dependencies(pong_win setup-dist)
endif()

# Custom target for cleaning dist directory
add_custom_target(clean-dist
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/debug
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}/release
    COMMENT "Cleaning dist directory")

# Print build information
message(STATUS "PongCpp build configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Documentation: ${BUILD_DOCUMENTATION}")
message(STATUS "  Output directory: ${DIST_DIR}")
if(WIN32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "  Windows GUI target: pong_win -> dist/debug/pong_win.exe")
    else()
        message(STATUS "  Windows GUI target: pong_win -> dist/release/pong_win.exe")
    endif()
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "  Console target: pong -> dist/debug/pong.exe")
else()
    message(STATUS "  Console target: pong -> dist/release/pong.exe")
endif()
