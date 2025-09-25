cmake_minimum_required(VERSION 3.10)
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

# Print build information
message(STATUS "PongCpp build configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Documentation: ${BUILD_DOCUMENTATION}")
if(WIN32)
    message(STATUS "  Windows GUI target: pong_win")
endif()
message(STATUS "  Console target: pong")
