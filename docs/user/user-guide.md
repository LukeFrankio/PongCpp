# PongCpp User Guide

Welcome to PongCpp! This guide will help you understand and play both versions of the game.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Console Version](#console-version)  
3. [Windows GUI Version](#windows-gui-version)
4. [Game Rules](#game-rules)
5. [Controls](#controls)
6. [Configuration](#configuration)
7. [Troubleshooting](#troubleshooting)

## Overview

PongCpp comes in two versions:

- **Console Version** (`pong.exe`): Text-based version that runs in any terminal
- **Windows GUI Version** (`pong_win.exe`): Graphical version with mouse support and settings

### System Requirements

**Console Version:**

- Any Windows, Linux, or POSIX-compatible system
- Terminal/console window
- Keyboard for input

**Windows GUI Version:**

- Windows 7 or later
- Mouse and/or keyboard
- ~5MB free disk space for settings and high scores

## Console Version

The console version displays the game using ASCII characters in your terminal window.

### Starting the Game

1. Open a command prompt or terminal
2. Navigate to the directory containing `pong.exe`
3. Run: `pong.exe` (Windows) or `./pong` (Linux)

### Gameplay

- The game field is displayed using ASCII characters
- Your paddle is on the left side (represented by `|` characters)
- The AI opponent is on the right side
- The ball bounces between paddles and walls
- Score is displayed at the top

### Console Controls

| Key | Action |
|-----|--------|
| W   | Move paddle up |
| S   | Move paddle down |
| Q   | Quit game |

**Note:** The console version also supports arrow keys for the right paddle (for testing purposes).

## Windows GUI Version

The Windows GUI version provides a modern graphical interface with additional features.

### Getting Started

Choose the version that best fits your system and preferences:

### Console Version (`pong` or `pong.exe`)

The console version runs in any terminal and is perfect for:

- Systems without GUI support
- Remote/SSH sessions  
- Minimal resource usage
- Cross-platform compatibility

### Windows GUI Version (`pong_win.exe`)

The GUI version provides enhanced features:

- Smooth graphics and animations
- Mouse control support
- Settings persistence
- High score tracking
- Configuration menus

## Windows GUI — Quick Start

1. Double-click `pong_win.exe` or run it from the command line
2. The game window will open and display the main game area
3. The game starts immediately

### Features

- **Smooth Graphics**: Real-time rendering using Windows GDI
- **Multiple Control Modes**: Keyboard or mouse input
- **Settings Persistence**: Your preferences are saved automatically
- **High Scores**: Track your best games with player names
- **DPI Awareness**: Scales properly on high-resolution displays

### Visual Elements

- **Game Field**: Dark background with white borders
- **Paddles**: White rectangular paddles on left and right
- **Ball**: White circular ball that bounces realistically
- **Score Display**: Current score shown at the top
- **Status**: Control mode and AI difficulty shown in title bar

## Game Rules

### Objective

Score points by getting the ball past your opponent's paddle.

### Scoring

- You score when the ball passes the AI paddle (right side)
- The AI scores when the ball passes your paddle (left side)
- Games typically play to 10 or 15 points (no automatic limit)

### Physics

- The ball bounces off top and bottom walls
- Ball speed increases slightly with each paddle hit
- Paddle movement affects ball direction and spin
- Ball contact point on paddle influences bounce angle

### AI Behavior

- The AI tracks the ball position
- AI difficulty affects reaction speed and accuracy
- Easy: Slower reactions, occasional misses
- Normal: Good tracking with realistic limitations
- Hard: Fast reactions, nearly perfect play

## Controls

### Console Version Controls

| Key | Action |
|-----|--------|
| W   | Move paddle up |
| S   | Move paddle down |
| Q   | Quit game |

### Windows GUI Controls

#### Keyboard Mode

| Key | Action |
|-----|--------|
| W   | Move paddle up |
| S   | Move paddle down |
| ESC | Exit game |
| Right-click | Open settings menu |

#### Mouse Mode

| Action | Control |
|--------|---------|
| Mouse movement | Control paddle position |
| ESC | Exit game |
| Right-click | Open settings menu |

## Configuration

### Windows GUI Settings

The Windows version allows you to configure:

1. **Control Mode**
   - Keyboard: Use W/S keys
   - Mouse: Mouse cursor controls paddle

2. **AI Difficulty**
   - Easy: AI is slower and less accurate
   - Normal: Balanced gameplay
   - Hard: AI is fast and precise

### Accessing Settings

1. Right-click anywhere in the game window
2. Select "Settings" from the context menu
3. Choose your preferred control mode and AI difficulty
4. Click "OK" to apply changes

Settings are automatically saved to `settings.json` in the same directory as the game.

### High Scores

The Windows version tracks high scores automatically:

- Scores are saved when you achieve a new personal best
- You'll be prompted to enter your name for high scores
- High scores are saved to `highscores.json`
- View high scores through the right-click menu

## Troubleshooting

### Console Version Issues

**Game doesn't respond to keys:**

- Make sure the terminal window has focus
- Try pressing keys more deliberately
- On some systems, try Ctrl+C to exit if stuck

**Display looks garbled:**

- Try resizing your terminal window
- Ensure your terminal supports ANSI escape sequences
- Try a different terminal program

**Build issues:**

- Make sure you have CMake 3.10+ installed
- For Windows, use Visual Studio or MSVC compiler
- For Linux, ensure g++ or clang++ supports C++17

### Windows GUI Issues

**Window appears blank or unresponsive:**

- Try running the console version to check for error messages
- Update your graphics drivers
- Try running as administrator

**High DPI display problems:**

- The game should automatically handle DPI scaling
- If text appears blurry, check Windows display scaling settings
- Try running in compatibility mode for older Windows versions

**Settings not saving:**

- Check that the game directory is writable
- Look for `settings.json` and `highscores.json` files
- Try running as administrator if needed

**Performance issues:**

- Close other applications to free up system resources
- The game is lightweight and should run on most systems
- Try adjusting Windows visual effects settings

### General Issues

**Game runs too fast or slow:**

- The game uses real-time physics with automatic frame timing
- Very old or very new systems might need different timing
- Try closing other applications

**Controls feel unresponsive:**

- Ensure the game window has focus
- Try switching between keyboard and mouse modes (GUI version)
- Check that no other applications are capturing input

## Getting Help

If you continue to experience issues:

1. Check the console version first - it often provides error messages
2. Ensure you have the latest Visual C++ redistributables (Windows)
3. Try running the game from a command prompt to see error output
4. Check that antivirus software isn't blocking the game

## Advanced Usage

### Command Line Options

Currently, the games don't accept command line parameters, but you can:

- Run multiple instances for testing
- Use different directories for separate configurations
- Create shortcuts with specific working directories

### File Locations

**Settings file:** `settings.json` (same directory as executable)
**High scores:** `highscores.json` (same directory as executable)

These files use human-readable JSON format and can be edited manually if needed.

### Building from Source

See the main README.md for complete build instructions. The game can be compiled on Windows, Linux, and other POSIX-compatible systems.
