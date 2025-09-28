/**
 * @file docs/README.md
 * @brief Documentation overview and navigation
 */

# PongCpp Documentation

Welcome to the comprehensive documentation for PongCpp, a classic Pong game implementation in C++ with dual frontend support.

## Documentation Overview

This documentation is organized into several sections to serve different audiences and use cases.

### User Documentation

**[User Guide](user/user-guide.md)**
Complete guide for end users covering installation, gameplay, controls, and troubleshooting for both console and Windows GUI versions.

### Developer Documentation

**[Architecture Guide](developer/architecture.md)**
Technical overview of the codebase architecture, design principles, and implementation details for developers who want to understand or contribute to the project.

**[API Reference](developer/api-reference.md)**
Quick reference for all public classes, methods, and interfaces in the PongCpp codebase.

### Generated Documentation

**[Doxygen API Documentation](doxygen/html/index.html)**
Complete API documentation generated from source code comments using Doxygen. Available after running `cmake --build . --target docs`.

## Quick Start

### For Users

1. Download or build the appropriate version for your platform
2. Console version: Run `pong` or `pong.exe` in a terminal
3. Windows GUI: Double-click `pong_win.exe`
4. See the [User Guide](user/user-guide.md) for detailed instructions

### For Developers

1. Clone the repository and review the [Architecture Guide](developer/architecture.md)
2. Build the project using CMake: `cmake --build build --config Release`
3. Explore the source code with the [API Reference](developer/api-reference.md)
4. Generate Doxygen docs: `cmake -DBUILD_DOCUMENTATION=ON .. && cmake --build . --target docs`

## Project Structure

```text
PongCpp/
├── src/                    # Source code
│   ├── core/              # Platform-independent game logic
│   ├── win/               # Windows GUI implementation
│   ├── platform*          # Platform abstraction layer
│   ├── main.cpp           # Console entry point
│   └── game.*             # Console interface
├── docs/                  # Documentation
│   ├── user/              # User guides
│   ├── developer/         # Technical documentation
│   └── doxygen/          # Generated API docs (after build)
├── build/                 # Build output directory
├── CMakeLists.txt         # Build configuration
├── Doxyfile              # Doxygen configuration
└── README.md             # Main project README
```

## Key Features

- **Dual Frontend Support**: Console (text-based) and Windows GUI versions
- **Cross-Platform**: Console version runs on Windows, Linux, and POSIX systems
- **No External Dependencies**: Uses only standard library and OS APIs
- **Modern C++**: Written in C++17 with clean, well-documented code
- **Realistic Physics**: Advanced ball-paddle collision with spin effects
- **Configurable AI**: Multiple difficulty levels for the computer opponent
- **Settings Persistence**: Save preferences and high scores (GUI version)

## Building Documentation

### Prerequisites

- Doxygen (for API documentation generation)
- CMake 3.10+ (for build system)

### Generate All Documentation

```bash
# Configure with documentation enabled
cmake -DBUILD_DOCUMENTATION=ON -S . -B build

# Build the project and documentation
cmake --build build --config Release --target docs

# Documentation will be available in docs/doxygen/html/
```

### Documentation Targets

- `docs`: Generate Doxygen API documentation
- `clean-docs`: Remove generated documentation files

## Contributing to Documentation

We welcome contributions to improve the documentation:

1. **User Guides**: Help improve clarity and add missing information
2. **Technical Docs**: Enhance architecture explanations and add examples  
3. **API Comments**: Improve Doxygen comments in source code
4. **Examples**: Add code examples and usage patterns

### Documentation Standards

- Use clear, concise language
- Include code examples where helpful
- Maintain consistent formatting
- Update all relevant sections when making changes
- Test documentation examples to ensure they work

## Support and Feedback

For questions about using or developing PongCpp:

1. Check the appropriate documentation section first
2. Review the generated Doxygen documentation for API details
3. Look at the source code comments for implementation details
4. Create an issue if you find documentation gaps or errors

## Version Information

This documentation corresponds to PongCpp version 1.0. The documentation is updated with each release to reflect current functionality and API changes.

## License

The documentation is provided under the same license as the PongCpp project. See the main project files for license details.
