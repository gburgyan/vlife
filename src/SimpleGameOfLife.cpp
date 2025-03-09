// GameOfLife.cpp - Implementation file for Game of Life library

#include <algorithm>
#include <unordered_set>
#include "SimpleGameOfLife.h"

SimpleGameOfLife::SimpleGameOfLife() {
    resetBoard();
}

void SimpleGameOfLife::resetBoard() {
    cells.clear();
}

SimpleGameOfLife::CellState SimpleGameOfLife::getCell(uint32_t x, uint32_t y) const {
    CellCoord coord{x, y};
    auto it = cells.find(coord);
    if (it != cells.end()) {
        return it->second;
    }
    return CellState::DEAD;
}

void SimpleGameOfLife::setCell(uint32_t x, uint32_t y, CellState state) {
    CellCoord coord{x, y};
    if (state == CellState::ALIVE) {
        cells[coord] = state;
    } else {
        // If the cell is dead, remove it from our sparse representation
        cells.erase(coord);
    }
}

std::vector<SimpleGameOfLife::CellState> SimpleGameOfLife::getCells(uint32_t startX, uint32_t startY, uint32_t width, uint32_t height) const {
    std::vector<CellState> result(width * height, CellState::DEAD);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            result[y * width + x] = getCell(startX + x, startY + y);
        }
    }
    
    return result;
}

void SimpleGameOfLife::setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height,
                           const std::vector<CellState>& cellsData) {
    if (cellsData.size() != width * height) {
        // Handle error: size mismatch
        return;
    }
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            setCell(offsetX + x, offsetY + y, cellsData[y * width + x]);
        }
    }
}

int SimpleGameOfLife::countLiveNeighbors(uint32_t x, uint32_t y) const {
    int count = 0;
    
    // Check all 8 neighboring cells
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            // Skip the center cell itself
            if (dx == 0 && dy == 0) continue;
            
            // Handle edge cases with wraparound (due to uint32_t)
            uint32_t nx = (x + dx) & 0xFFFFFFFF;
            uint32_t ny = (y + dy) & 0xFFFFFFFF;
            
            if (getCell(nx, ny) == CellState::ALIVE) {
                count++;
            }
        }
    }
    
    return count;
}

SimpleGameOfLife::CellState SimpleGameOfLife::getNextCellState(uint32_t x, uint32_t y, CellState currentState) const {
    int liveNeighbors = countLiveNeighbors(x, y);
    
    // Apply Conway's Game of Life rules
    if (currentState == CellState::ALIVE) {
        // Any live cell with fewer than two live neighbors dies (underpopulation)
        // Any live cell with more than three live neighbors dies (overpopulation)
        if (liveNeighbors < 2 || liveNeighbors > 3) {
            return CellState::DEAD;
        }
        // Any live cell with two or three live neighbors lives on
        return CellState::ALIVE;
    } else {
        // Any dead cell with exactly three live neighbors becomes alive (reproduction)
        if (liveNeighbors == 3) {
            return CellState::ALIVE;
        }
        return CellState::DEAD;
    }
}

void SimpleGameOfLife::runGeneration() {
    // Since we only store live cells, we need to consider all cells that might become alive
    // This includes all current live cells and their neighbors
    std::unordered_set<CellCoord, CellCoordHash> cellsToCheck;
    
    // Add all currently live cells
    for (const auto& cell : cells) {
        CellCoord coord = cell.first;
        cellsToCheck.insert(coord);
        
        // Add all neighbors
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                
                uint32_t nx = (coord.x + dx) & 0xFFFFFFFF;
                uint32_t ny = (coord.y + dy) & 0xFFFFFFFF;
                
                cellsToCheck.insert({nx, ny});
            }
        }
    }
    
    // Calculate the next state
    std::unordered_map<CellCoord, CellState, CellCoordHash> nextGeneration;
    
    for (const auto& coord : cellsToCheck) {
        CellState currentState = getCell(coord.x, coord.y);
        CellState nextState = getNextCellState(coord.x, coord.y, currentState);
        
        if (nextState == CellState::ALIVE) {
            nextGeneration[coord] = nextState;
        }
    }
    
    // Update the state
    cells = std::move(nextGeneration);
}

void SimpleGameOfLife::runGenerations(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        runGeneration();
    }
}
