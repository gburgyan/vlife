// GameOfLife.h - Header file for Game of Life library

#pragma once
#include "GameOfLife.h"
#include <unordered_map>

class SimpleGameOfLife : public GameOfLife {
public:
    // Constructor
    SimpleGameOfLife();

    // Board operations
    void resetBoard() override;

    // Cell operations
    [[nodiscard]] CellState getCell(uint32_t x, uint32_t y) const override;
    void setCell(uint32_t x, uint32_t y, CellState state) override;

    // Get multiple cells in the specified region
    [[nodiscard]] std::vector<CellState> getCells(uint32_t startX, uint32_t startY, uint32_t width, uint32_t height) const override;

    // Set multiple cells starting at the offset position
    void setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height, const std::vector<CellState>& cells) override;

    // Generation operations
    void runGeneration() override;
    void runGenerations(uint32_t count) override;

private:
    // Since the board is 2^32 × 2^32, we can't store all cells.
    // Instead, we'll use a sparse representation that only tracks live cells.
    struct CellCoord {
        uint32_t x;
        uint32_t y;

        bool operator==(const CellCoord& other) const {
            return x == other.x && y == other.y;
        }
    };

    struct CellCoordHash {
        std::size_t operator()(const CellCoord& cell) const {
            // Combine the hash of x and y using XOR and bit shifting
            return static_cast<size_t>(cell.x) ^
                   static_cast<size_t>(cell.y) << 16;
        }
    };

    // Sparse representation to store only live cells
    std::unordered_map<CellCoord, CellState, CellCoordHash> cells;

    // Helper methods
    [[nodiscard]] int countLiveNeighbors(uint32_t x, uint32_t y) const;
    [[nodiscard]] CellState getNextCellState(uint32_t x, uint32_t y, CellState currentState) const;
};