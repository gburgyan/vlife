# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VLife is a Conway's Game of Life implementation with two versions:
1. A simple implementation (SimpleGameOfLife)
2. A more optimized version (VLife) that is in development

The project uses C++ with Qt5 for the UI, and is built with CMake.

## Build Commands

To build the project:
```bash
mkdir -p build
cd build
cmake ..
make
```

To run the application:
```bash
cd build
./GameOfLifeApp
```

## Project Structure

### Architecture

- **GameOfLife** (interface): Abstract base class defining the Game of Life interface
  - `GameOfLife.h` provides the interface with core methods for manipulating cell states and advancing generations

- **SimpleGameOfLife** (implementation): A straightforward implementation using a sparse representation
  - Uses an unordered_map to store only live cells
  - Implements standard Conway's Game of Life rules

- **VLife** (implementation): An optimized implementation using a tiled approach
  - Uses fixed-size tiles to store and process cells efficiently
  - Includes optimizations for faster generation processing
  - Currently under development

- **UI Component**: Qt-based visualization
  - `GameOfLifeView` provides a graphical interface using Qt5
  - Includes controls for running/stopping simulation, stepping through generations, and zoom

### Key Components

1. **GameOfLife Interface** (`src/GameOfLife.h`):
   - Defines the core interface for the Game of Life implementation
   - Cell state representation and operations
   - Generation operations

2. **SimpleGameOfLife** (`src/SimpleGameOfLife.h`, `src/SimpleGameOfLife.cpp`):
   - Sparse representation that only tracks live cells
   - Uses an unordered_map with custom hashing for cell coordinates

3. **VLife Components** (`src/vlife/`):
   - `VLife.h`, `VLife.cpp`: Optimized implementation using tiles
   - `Tile.h`, `Tile.cpp`: Implementation of the tile-based approach
   - **Cell Representation**: 
     - Each cell nibble (4 bits) represents two cells
     - The high bit (bit 3) indicates the liveness of that cell
     - The lower three bits (bits 0-2) store the number of living neighbors, excluding the paired cell
     - Cell's true neighbor count can be inferred by adding the stored neighbor count and the state of its paired cell
     - Cells are processed using lookup tables for efficient generation updates

4. **UI Components** (`src/GameOfLifeView.h`, `src/GameOfLifeView.cpp`):
   - Qt-based UI with controls for the simulation
   - Graphical representation of the game board

## Development Notes

- The project uses a CMake-based build system
- Qt5 is required for the UI components and is detected via Homebrew on macOS
- The code uses C++20 features
- The VLife implementation is a work in progress and focuses on performance optimization