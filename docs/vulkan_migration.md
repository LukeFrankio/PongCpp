# Vulkan Migration Guide

## Overview

This document describes the migration of the Pong game from Win32 GDI to the Vulkan graphics API, providing modern, cross-platform, hardware-accelerated rendering.

## Architecture

### Original Implementation
- **Graphics API**: Win32 GDI (software-based)
- **Platform**: Windows only
- **Rendering**: CPU-based with double-buffering using memory device contexts
- **Performance**: Limited to software rendering

### New Vulkan Implementation
- **Graphics API**: Vulkan 1.0+
- **Platform**: Cross-platform (Windows via Win32, Linux via X11)
- **Rendering**: GPU-accelerated with proper command buffer management
- **Performance**: Hardware-accelerated with modern GPU features

## Key Components

### VulkanRenderer (`src/vulkan/vulkan_renderer.h/.cpp`)
Core Vulkan abstraction providing:
- Instance and device management
- Surface creation (platform-specific)
- Swapchain management
- Command buffer recording
- Basic drawing primitives

### Platform-Specific Game Classes
- **Windows**: `game_vulkan.h/.cpp` - Uses Win32 API for window management
- **Linux**: `game_vulkan_linux.h/.cpp` - Uses X11 for window management

### Features Implemented
- [x] Cross-platform Vulkan initialization
- [x] Basic window management (Win32/X11)
- [x] Command buffer recording and submission
- [x] Swapchain setup and presentation
- [ ] Rectangle rendering (TODO)
- [ ] Text rendering (TODO)
- [ ] Game logic integration (TODO)

## Build Requirements

### Dependencies
- **Vulkan SDK**: Headers and loader library
- **Platform Libraries**:
  - Windows: `user32.lib`
  - Linux: `libX11`

### CMake Configuration
The build system automatically detects Vulkan availability and builds the appropriate platform target:

```cmake
find_package(Vulkan)
if (Vulkan_FOUND)
    # Platform-specific build configuration
endif()
```

## Build Instructions

### Linux
```bash
# Install dependencies
sudo apt install libvulkan-dev vulkan-tools libx11-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4

# Run
./pong_vulkan
```

### Windows
```powershell
# Ensure Vulkan SDK is installed
# Build using existing build.bat or CMake
.\build.bat Release
.\build\Release\pong_vulkan.exe
```

## Migration Status

### Completed
- [x] Core Vulkan renderer abstraction
- [x] Cross-platform window management
- [x] Build system integration
- [x] Basic frame rendering loop

### In Progress
- [ ] Complete swapchain implementation
- [ ] Rectangle rendering primitives
- [ ] Text rendering system
- [ ] Game logic integration

### Future Enhancements
- [ ] Shader compilation system
- [ ] Advanced rendering features (anti-aliasing, effects)
- [ ] Performance optimization
- [ ] Error handling and fallback mechanisms

## Performance Benefits

The Vulkan implementation provides several advantages over the original GDI approach:

1. **Hardware Acceleration**: Direct GPU rendering vs CPU software rendering
2. **Cross-Platform**: Runs on Windows, Linux, and potentially other platforms
3. **Modern API**: Access to modern GPU features and optimization techniques
4. **Scalability**: Better performance scaling with complex graphics

## Compatibility

- **Minimum Vulkan Version**: 1.0
- **Supported Platforms**: Windows (Win32), Linux (X11)
- **GPU Requirements**: Any Vulkan-compatible graphics card
- **Fallback**: Original GDI version (`pong_win`) remains available

## Development Notes

The migration maintains the existing game architecture while replacing only the rendering subsystem. This ensures:
- Minimal disruption to game logic
- Preservation of existing features (settings, high scores)
- Easy comparison between implementations
- Gradual migration path