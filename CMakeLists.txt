cmake_minimum_required(VERSION 3.10)
project(pong LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optionally enforce 64-bit build. Default ON on Windows (recommended there), OFF elsewhere
if (WIN32)
    option(ENFORCE_64BIT "Fail configuration if not building a 64-bit binary (recommended on Windows)" ON)
else()
    option(ENFORCE_64BIT "Fail configuration if not building a 64-bit binary (recommended on Windows)" OFF)
endif()

# New: allow opting into a strict fatal error if desired
option(ENFORCE_64BIT_FATAL "Treat 64-bit enforcement as fatal error (default OFF). Set -DENFORCE_64BIT_FATAL=ON to make configuration fail on non-64-bit systems." OFF)

# New option to allow suppressing the non-fatal 64-bit warning
option(ENFORCE_64BIT_WARN "Show a non-fatal warning when not building 64-bit (can be turned OFF to silence)" ON)

if (ENFORCE_64BIT)
    # Only check if CMAKE_SIZEOF_VOID_P is known to avoid spurious warnings
    if (DEFINED CMAKE_SIZEOF_VOID_P)
        if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            if (ENFORCE_64BIT_FATAL)
                message(FATAL_ERROR "64-bit build required (pointer size = ${CMAKE_SIZEOF_VOID_P}). Configure with -DENFORCE_64BIT=OFF to override or disable -DENFORCE_64BIT_FATAL.")
            elseif (ENFORCE_64BIT_WARN)
                # Use STATUS instead of WARNING to avoid emitting a CMake "Warning" entry during normal 32-bit configurations.
                # This still displays informative output but won't be treated as a warning by CMake.
                message(STATUS "64-bit build recommended (pointer size = ${CMAKE_SIZEOF_VOID_P}). If you intend to build 32-bit, reconfigure with -DENFORCE_64BIT=OFF. To make this check fatal, set -DENFORCE_64BIT_FATAL=ON.")
            endif()
        endif()
    else()
        # Pointer size unknown — skip the check rather than warn spuriously
        message(STATUS "ENFORCE_64BIT is enabled but CMAKE_SIZEOF_VOID_P is not defined; skipping pointer-size check.")
    endif()
endif()

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

## Console target sources discovered recursively
file(GLOB_RECURSE PONG_CONSOLE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/console/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
)

add_executable(pong ${PONG_CONSOLE_SOURCES})
target_include_directories(pong PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windowed Win32 Pong (no external libs)
if (WIN32)
    ## Windows GUI target sources (recursive). We intentionally separate core & platform neutral code.
    file(GLOB_RECURSE PONG_WIN_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    )
    add_executable(pong_win ${PONG_WIN_SOURCES})
    target_include_directories(pong_win PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(pong_win PRIVATE user32 gdi32)
    set_target_properties(pong_win PROPERTIES WIN32_EXECUTABLE YES)
    
    # Phase 1 Performance Optimizations for path tracer
    if(MSVC)
        # MSVC optimization flags
        target_compile_options(pong_win PRIVATE
            $<$<CONFIG:Release>:/O2>           # Maximum optimization
            $<$<CONFIG:Release>:/Oi>           # Enable intrinsic functions
            $<$<CONFIG:Release>:/GL>           # Whole program optimization
            $<$<CONFIG:Release>:/fp:fast>      # Fast floating-point model
            $<$<CONFIG:Release>:/arch:AVX2>    # Enable AVX2 instructions
            $<$<CONFIG:Release>:/Gy>           # Enable function-level linking
        )
        target_link_options(pong_win PRIVATE
            $<$<CONFIG:Release>:/LTCG>         # Link-time code generation
            $<$<CONFIG:Release>:/OPT:REF>      # Remove unreferenced functions
            $<$<CONFIG:Release>:/OPT:ICF>      # Identical COMDAT folding
        )
    else()
        # GCC/Clang optimization flags
        target_compile_options(pong_win PRIVATE
            $<$<CONFIG:Release>:-O3>           # Maximum optimization
            $<$<CONFIG:Release>:-ffast-math>   # Fast math (trade precision for speed)
            $<$<CONFIG:Release>:-march=native> # Optimize for build machine CPU
            $<$<CONFIG:Release>:-flto>         # Link-time optimization
            $<$<CONFIG:Release>:-funroll-loops> # Unroll loops
        )
        target_link_options(pong_win PRIVATE
            $<$<CONFIG:Release>:-flto>         # Link-time optimization
        )
    endif()
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
message(STATUS "PongCpp build configuration (recursive source discovery enabled):")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Documentation: ${BUILD_DOCUMENTATION}")
message(STATUS "  Output directory: ${DIST_DIR}")
message(STATUS "  Note: Using file(GLOB_RECURSE ...) for source discovery. If you add new source files, re-run CMake configure to ensure they're picked up.")
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
