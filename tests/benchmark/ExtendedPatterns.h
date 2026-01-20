//
// VLife Extended Benchmark Patterns
// Additional patterns for comprehensive activity-proportional analysis
//

#pragma once

#include "../../src/vlife/VLife.h"
#include <random>
#include <cmath>

namespace ExtendedPatterns {

// ============================================================================
// DENSITY SWEEP PATTERNS
// For analyzing performance vs. population density
// ============================================================================

// Random soup with configurable density - core pattern for density sweeps
inline void setupDensitySoup(VLife& board, int width, int height,
                             double density, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (dist(rng) < density) {
                board.setCell(x, y, GameOfLife::CellState::ALIVE);
            }
        }
    }
}

// ============================================================================
// STILL LIFE PATTERNS
// Zero activity after setup - tests overhead of inactive patterns
// ============================================================================

// Block - simplest still life
inline void setupBlock(VLife& board, int offsetX, int offsetY) {
    board.setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
}

// Beehive - common still life
inline void setupBeehive(VLife& board, int offsetX, int offsetY) {
    //  .XX.
    //  X..X
    //  .XX.
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Loaf - another common still life
inline void setupLoaf(VLife& board, int offsetX, int offsetY) {
    //  .XX.
    //  X..X
    //  .X.X
    //  ..X.
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 3, GameOfLife::CellState::ALIVE);
}

// ============================================================================
// OSCILLATOR PATTERNS
// Periodic activity - tests consistent workload
// ============================================================================

// Blinker - period 2
inline void setupBlinker(VLife& board, int offsetX, int offsetY) {
    board.setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
}

// Toad - period 2
inline void setupToad(VLife& board, int offsetX, int offsetY) {
    //  .XXX
    //  XXX.
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 1, GameOfLife::CellState::ALIVE);
}

// Beacon - period 2
inline void setupBeacon(VLife& board, int offsetX, int offsetY) {
    //  XX..
    //  X...
    //  ...X
    //  ..XX
    board.setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 3, GameOfLife::CellState::ALIVE);
}

// Pulsar - period 3, larger oscillator
inline void setupPulsar(VLife& board, int offsetX, int offsetY) {
    // Complex period-3 oscillator with 48 cells
    int pattern[48][2] = {
        // Top section
        {2, 0}, {3, 0}, {4, 0}, {8, 0}, {9, 0}, {10, 0},
        // Upper left
        {0, 2}, {0, 3}, {0, 4},
        {5, 2}, {5, 3}, {5, 4},
        // Upper right
        {7, 2}, {7, 3}, {7, 4},
        {12, 2}, {12, 3}, {12, 4},
        // Middle top
        {2, 5}, {3, 5}, {4, 5}, {8, 5}, {9, 5}, {10, 5},
        // Middle bottom
        {2, 7}, {3, 7}, {4, 7}, {8, 7}, {9, 7}, {10, 7},
        // Lower left
        {0, 8}, {0, 9}, {0, 10},
        {5, 8}, {5, 9}, {5, 10},
        // Lower right
        {7, 8}, {7, 9}, {7, 10},
        {12, 8}, {12, 9}, {12, 10},
        // Bottom section
        {2, 12}, {3, 12}, {4, 12}, {8, 12}, {9, 12}, {10, 12}
    };
    for (int i = 0; i < 48; i++) {
        board.setCell(offsetX + pattern[i][0], offsetY + pattern[i][1],
                      GameOfLife::CellState::ALIVE);
    }
}

// Pentadecathlon - period 15
inline void setupPentadecathlon(VLife& board, int offsetX, int offsetY) {
    // Period 15 oscillator
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Grid of blinkers for testing regular oscillation
inline void setupBlinkerGrid(VLife& board, int gridSize, int spacing = 6) {
    for (int y = 0; y < gridSize; y++) {
        for (int x = 0; x < gridSize; x++) {
            setupBlinker(board, x * spacing, y * spacing);
        }
    }
}

// ============================================================================
// METHUSELAH PATTERNS
// Chaotic patterns that grow before stabilizing
// ============================================================================

// R-pentomino - classic methuselah, stabilizes at generation 1103
inline void setupRPentomino(VLife& board, int offsetX = 100, int offsetY = 100) {
    //  .XX
    //  XX.
    //  .X.
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Diehard - dies completely after 130 generations
inline void setupDiehard(VLife& board, int offsetX = 100, int offsetY = 100) {
    //  ......X.
    //  XX......
    //  .X...XXX
    board.setCell(offsetX + 6, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 6, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 7, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Pi-heptomino - another methuselah
inline void setupPiHeptomino(VLife& board, int offsetX = 100, int offsetY = 100) {
    //  XXX
    //  X.X
    //  X.X
    board.setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// ============================================================================
// SPACESHIP PATTERNS
// Moving patterns that test tile boundary crossing
// ============================================================================

// Middleweight spaceship (MWSS)
inline void setupMWSS(VLife& board, int offsetX, int offsetY) {
    //  ..X...
    //  X...X.
    //  .....X
    //  X....X
    //  .XXXXX
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 4, GameOfLife::CellState::ALIVE);
}

// Heavyweight spaceship (HWSS)
inline void setupHWSS(VLife& board, int offsetX, int offsetY) {
    //  ..XX...
    //  X....X.
    //  ......X
    //  X.....X
    //  .XXXXXX
    board.setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 6, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 6, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 6, offsetY + 4, GameOfLife::CellState::ALIVE);
}

// ============================================================================
// COMPOSITE PATTERNS FOR SPECIFIC TESTS
// ============================================================================

// Mixed pattern: still lifes + oscillators + spaceships
// Tests handling of varied activity levels in same simulation
inline void setupMixedPattern(VLife& board) {
    // Add some blocks (still lifes)
    for (int i = 0; i < 10; i++) {
        setupBlock(board, i * 20, 0);
    }
    // Add some blinkers (oscillators)
    for (int i = 0; i < 10; i++) {
        setupBlinker(board, i * 20, 50);
    }
    // Add some gliders (spaceships) - using simple inline definition
    for (int i = 0; i < 5; i++) {
        int ox = i * 40;
        int oy = 100;
        board.setCell(ox + 1, oy, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 2, oy + 1, GameOfLife::CellState::ALIVE);
        board.setCell(ox, oy + 2, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 1, oy + 2, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 2, oy + 2, GameOfLife::CellState::ALIVE);
    }
}

// Pattern designed to stress tile boundaries
// Gliders that cross many tile boundaries
inline void setupBoundaryCrossingPattern(VLife& board, int count = 100) {
    // Place gliders at diagonal positions so they cross maximum tile boundaries
    for (int i = 0; i < count; i++) {
        int ox = i * 35;  // Not aligned to 32, so crosses tiles
        int oy = i * 35;
        board.setCell(ox + 1, oy, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 2, oy + 1, GameOfLife::CellState::ALIVE);
        board.setCell(ox, oy + 2, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 1, oy + 2, GameOfLife::CellState::ALIVE);
        board.setCell(ox + 2, oy + 2, GameOfLife::CellState::ALIVE);
    }
}

// High-activity ring pattern - circular arrangement maximizes tile count
inline void setupCircularPattern(VLife& board, int centerX, int centerY,
                                  int radius, double density = 0.3,
                                  uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (int y = centerY - radius; y <= centerY + radius; y++) {
        for (int x = centerX - radius; x <= centerX + radius; x++) {
            double dx = x - centerX;
            double dy = y - centerY;
            if (dx*dx + dy*dy <= radius*radius && dist(rng) < density) {
                board.setCell(x, y, GameOfLife::CellState::ALIVE);
            }
        }
    }
}

// ============================================================================
// BENCHMARK HELPER FUNCTIONS
// ============================================================================

// Calculate expected tile count for a rectangular region
inline size_t expectedTileCount(int width, int height) {
    int tilesX = (width + 31) / 32;
    int tilesY = (height + 31) / 32;
    return tilesX * tilesY;
}

// Estimate activity ratio (changes/population) based on pattern type
inline const char* patternActivityDescription(const char* patternType) {
    // Returns description of expected activity ratio
    if (strcmp(patternType, "still_life") == 0) return "zero (k=0)";
    if (strcmp(patternType, "oscillator") == 0) return "periodic (k~0.1-0.5 * N)";
    if (strcmp(patternType, "spaceship") == 0) return "low (k~0.2 * N)";
    if (strcmp(patternType, "methuselah") == 0) return "variable (k~0.01-0.2 * N)";
    if (strcmp(patternType, "random_soup") == 0) return "high (k~0.05-0.15 * N)";
    return "unknown";
}

} // namespace ExtendedPatterns
