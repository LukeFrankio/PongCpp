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

# Vulkan renderer option
option(BUILD_VULKAN "Build Vulkan renderer with Slang shaders" OFF)

# Vulkan renderer implementation
if(BUILD_VULKAN AND WIN32)
    # Find Vulkan SDK
    find_package(Vulkan QUIET)
    
    # Find Slang SDK - check SLANG_DIR first, then within Vulkan SDK
    find_path(SLANG_INCLUDE_DIR slang.h
        PATHS 
            $ENV{SLANG_DIR}/include
            $ENV{VULKAN_SDK}/Include/slang  # Slang included in Vulkan SDK
            ${Vulkan_INCLUDE_DIRS}/slang    # Use Vulkan include dirs
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/slang/include
    )
    find_library(SLANG_LIBRARY slang
        PATHS 
            $ENV{SLANG_DIR}/lib
            $ENV{SLANG_DIR}/bin  # Slang sometimes puts DLLs here
            $ENV{VULKAN_SDK}/Lib  # Slang included in Vulkan SDK
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/slang/lib
    )
    
    if(Vulkan_FOUND AND SLANG_INCLUDE_DIR AND SLANG_LIBRARY)
        message(STATUS "Vulkan SDK found: ${Vulkan_LIBRARIES}")
        message(STATUS "Slang SDK found: ${SLANG_LIBRARY}")
        
        add_executable(pong_vulkan
            src/win/main_vulkan.cpp
            src/win/game_vulkan.cpp
            src/vulkan/vulkan_renderer.cpp
            src/vulkan/vulkan_context.cpp
            src/vulkan/vulkan_memory.cpp
            src/vulkan/slang_compiler.cpp
            src/vulkan/vulkan_math.h
            src/core/game_core.cpp
            src/win/settings.cpp
            src/win/highscores.cpp
        )
        
        target_link_libraries(pong_vulkan PRIVATE 
            ${Vulkan_LIBRARIES}
            ${SLANG_LIBRARY}
            user32 
            gdi32
        )
        target_include_directories(pong_vulkan PRIVATE 
            ${Vulkan_INCLUDE_DIRS}
            ${SLANG_INCLUDE_DIR}
        )
        set_target_properties(pong_vulkan PROPERTIES WIN32_EXECUTABLE YES)
        add_dependencies(pong_vulkan setup-dist)
        
        # Custom target for copying Slang shaders to output directory
        add_custom_target(compile-shaders
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan/shaders ${DIST_DIR}/debug
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan/shaders ${DIST_DIR}/release
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Compiling Slang shaders"
        )
        add_dependencies(pong_vulkan compile-shaders)
        
    else()
        if(NOT Vulkan_FOUND)
            message(WARNING "Vulkan SDK not found. Set VULKAN_SDK environment variable.")
        endif()
        if(NOT SLANG_INCLUDE_DIR OR NOT SLANG_LIBRARY)
            message(WARNING "Slang SDK not found. Set SLANG_DIR environment variable.")
        endif()
        message(WARNING "Vulkan renderer disabled due to missing dependencies.")
    endif()
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
message(STATUS "  Vulkan renderer: ${BUILD_VULKAN}")
message(STATUS "  Output directory: ${DIST_DIR}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "  Console target: pong -> dist/debug/pong.exe")
else()
    message(STATUS "  Console target: pong -> dist/release/pong.exe")
endif()
if(WIN32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "  Windows GUI target: pong_win -> dist/debug/pong_win.exe")
    else()
        message(STATUS "  Windows GUI target: pong_win -> dist/release/pong_win.exe")
    endif()
    if(BUILD_VULKAN AND TARGET pong_vulkan)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "  Vulkan target: pong_vulkan -> dist/debug/pong_vulkan.exe")
        else()
            message(STATUS "  Vulkan target: pong_vulkan -> dist/release/pong_vulkan.exe")
        endif()
    endif()
endif()
