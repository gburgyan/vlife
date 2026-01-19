//
// VLife Benchmark Patterns
//

#pragma once

#include "../../src/vlife/VLife.h"
#include <random>

namespace BenchmarkPatterns {

// Gosper Glider Gun - sustained activity, tests steady-state
// Places a glider gun at the specified offset
inline void setupGliderGun(VLife& board, int offsetX = 0, int offsetY = 0) {
    // Gosper Glider Gun pattern
    // Left square
    board.setCell(offsetX + 1, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 6, GameOfLife::CellState::ALIVE);

    // Left part
    board.setCell(offsetX + 11, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 11, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 11, offsetY + 7, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 12, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 12, offsetY + 8, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 13, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 13, offsetY + 9, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 14, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 14, offsetY + 9, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 15, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 16, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 16, offsetY + 8, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 17, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 17, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 17, offsetY + 7, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 18, offsetY + 6, GameOfLife::CellState::ALIVE);

    // Right part
    board.setCell(offsetX + 21, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 21, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 21, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 22, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 22, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 22, offsetY + 5, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 23, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 23, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 25, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 25, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 25, offsetY + 6, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 25, offsetY + 7, GameOfLife::CellState::ALIVE);

    // Right square
    board.setCell(offsetX + 35, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 35, offsetY + 4, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 36, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 36, offsetY + 4, GameOfLife::CellState::ALIVE);
}

// Setup multiple glider guns
inline void setupGliderGuns(VLife& board, int count) {
    for (int i = 0; i < count; i++) {
        int offsetX = (i % 4) * 50;
        int offsetY = (i / 4) * 30;
        setupGliderGun(board, offsetX, offsetY);
    }
}

// Random Soup - high change rate, stresses all code paths
inline void setupRandomSoup(VLife& board, int width, int height, double density = 0.3, uint32_t seed = 42) {
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

// Lightweight Spaceship (LWSS) - moving pattern
inline void setupLWSS(VLife& board, int offsetX, int offsetY) {
    //  .X..X
    //  X....
    //  X...X
    //  XXXX.
    board.setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 3, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 3, GameOfLife::CellState::ALIVE);
}

// Spaceship Fleet - moving patterns, tests tile creation/eviction
inline void setupSpaceships(VLife& board, int count) {
    for (int i = 0; i < count; i++) {
        int offsetX = (i % 10) * 20;
        int offsetY = (i / 10) * 10;
        setupLWSS(board, offsetX, offsetY);
    }
}

// Static Block Grid - minimal changes, tests prepare overhead
inline void setupBlockGrid(VLife& board, int gridSize) {
    for (int y = 0; y < gridSize; y++) {
        for (int x = 0; x < gridSize; x++) {
            int offsetX = x * 4;
            int offsetY = y * 4;
            // 2x2 block
            board.setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
            board.setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
            board.setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
            board.setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
        }
    }
}

// Glider - simple moving pattern
inline void setupGlider(VLife& board, int offsetX, int offsetY) {
    //  .X.
    //  ..X
    //  XXX
    board.setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Multiple gliders - tests tile boundary crossing and dead tile eviction
inline void setupGliders(VLife& board, int count) {
    for (int i = 0; i < count; i++) {
        int offsetX = (i % 20) * 10;
        int offsetY = (i / 20) * 10;
        setupGlider(board, offsetX, offsetY);
    }
}

// Acorn - methuselah that grows extensively before stabilizing
inline void setupAcorn(VLife& board, int offsetX = 100, int offsetY = 100) {
    //  .X.....
    //  ...X...
    //  XX..XXX
    board.setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 3, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 4, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 5, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 6, offsetY + 2, GameOfLife::CellState::ALIVE);
}

} // namespace BenchmarkPatterns
