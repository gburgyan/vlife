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

To build with AVX-512 optimizations:
```bash
mkdir -p build
cd build
cmake -DENABLE_AVX512=ON ..
make
```

To run the application:
```bash
cd build
./GameOfLifeApp
```

To run tests:
```bash
cd build
ctest --output-on-failure
```

To run benchmarks:
```bash
cd build
./VLifeBenchmark --quick    # Quick benchmark
./VLifeBenchmark            # Full benchmark
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

## AVX-512 Optimization

The VLife implementation includes an optional AVX-512 SIMD optimization for the `runGenerationPrepare` phase, which identifies cells that will change state in the next generation.

### How It Works

The standard implementation uses a 256-entry lookup table (LUT) to determine which cells change. The AVX-512 version replaces LUT lookups with direct SIMD computation:

- Processes 64 bytes (64 cell pairs) per iteration using 512-bit registers
- Uses mask-based bit extraction to apply Conway's rules in parallel
- Eliminates random memory accesses from LUT lookups

### Build Configuration

Enable with `-DENABLE_AVX512=ON`:

- **On x86_64 with AVX-512 support**: Uses native AVX-512 intrinsics (`-mavx512f -mavx512bw -mavx512vl`)
- **On other platforms (Apple Silicon, older x86)**: Uses [SIMDE](https://github.com/simd-everywhere/simde) for portable emulation

Runtime dispatch automatically selects the appropriate code path based on CPU capabilities.

### Key Files

- `src/vlife/CpuFeatures.h`: Runtime CPU feature detection
- `src/vlife/Tile.cpp`: Contains both scalar and AVX-512 implementations of `runGenerationPrepare`
- `CMakeLists.txt`: Build configuration for AVX-512 support
- `.github/workflows/avx512-test.yml`: CI pipeline for AVX-512 testing

### Testing

The AVX-512 implementation can be tested for correctness on any platform using SIMDE emulation. For performance testing, use x86_64 hardware with AVX-512 support (Intel Ice Lake+, AMD Zen 4+).

```bash
# Build with AVX-512 (uses SIMDE on non-x86 platforms)
cmake -DENABLE_AVX512=ON ..
make

# Run tests to verify correctness
ctest --output-on-failure
```

### Expected Performance

On actual AVX-512 hardware, the optimization targets the `runGenerationPrepare` phase (~60% of execution time):
- **Phase 1 (Prepare)**: 2-4x speedup from eliminating LUT lookups
- **Overall**: 1.5-2.5x speedup depending on grid density and activity patterns

Note: AVX-512 can cause frequency throttling on some Intel CPUs, which may reduce net benefit.