// GameOfLife.h - Header file for base class

#pragma once
#include <vector>
#include <cstdint>

class GameOfLife {
public:
    // Cell state representation
    enum class CellState {
        DEAD = 0,
        ALIVE = 1
    };

    // Virtual destructor for proper cleanup of derived classes
    virtual ~GameOfLife() = default;

    // Board operations
    virtual void resetBoard() = 0;

    // Cell operations
    [[nodiscard]] virtual CellState getCell(uint32_t x, uint32_t y) const = 0;
    virtual void setCell(uint32_t x, uint32_t y, CellState state) = 0;

    // Get multiple cells in the specified region
    // Returns cells in a 1D array in row-major order
    [[nodiscard]] virtual std::vector<CellState> getCells(uint32_t startX, uint32_t startY, uint32_t width, uint32_t height) const = 0;

    // Set multiple cells starting at the offset position
    // cells should be a 1D array in row-major order
    virtual void setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height, const std::vector<CellState>& cells) = 0;

    // Generation operations
    virtual void runGeneration() = 0;
    virtual void runGenerations(uint32_t count) = 0;
};