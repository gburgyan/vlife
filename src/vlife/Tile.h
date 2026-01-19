//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <mutex>
#include "VLife.h"

#define TILE_CELLS TILE_WIDTH *TILE_HEIGHT
#define TILE_BYTES TILE_CELLS / 2
#define TILE_64S TILE_BYTES / 8
#define TILE_CHANGE_64S TILE_CELLS / 64
#define TILE_64S_WIDTH TILE_WIDTH / 64
#define TILE_64S_HEIGHT TILE_HEIGHT / 64

class Tile {
    VLife *board;
    int32_t tileX;
    int32_t tileY;

    Tile *left;
    Tile *right;
    Tile *up;
    Tile *down;

    uint64_t cells[TILE_64S]{};
    uint64_t changes[TILE_CHANGE_64S]{};
    uint32_t liveCount; // Tracks the number of live cells in this tile

    std::mutex tileMutex;

public:
    Tile(VLife *board, int32_t tileX, int32_t tileY);

    void setNeighbor(Tile *tile, int dx, int dy);

    void runGenerationPrepare();
    void runGenerationChanges();

    void lockTile();
    void unlockTile();

    // Cell access methods
    bool getCell(uint32_t localX, uint32_t localY) const;
    void setCell(uint32_t localX, uint32_t localY, bool alive);
    void updateNeighborCount(int localX, int localY, bool increment);

    // Helper for runGenerationChanges - updates all 8 neighbors when a cell toggles
    void updateNeighborsForChangedCell(int localX, int localY, bool becameAlive);

    // LUT-based optimization helpers for runGenerationChanges
    // Apply a single delta to a cell's neighbor count (inline for performance)
    inline void applyDelta(int x, int y, int8_t delta);

    // Apply vertical deltas to 4 consecutive cells in a row (x-1, x, x+1, x+2)
    // baseX is the x coordinate of the right cell in the pair (even x)
    void applyVerticalDeltas(int baseX, int y, const int8_t* deltas);

    // Apply deltas for a cell pair using LUT (optimized version)
    void applyDeltasForCellPair(int baseX, int localY, const PackedDeltas& deltas);

    // Getters for tile coordinates
    int32_t getTileX() const { return tileX; }
    int32_t getTileY() const { return tileY; }

    // Getters for neighbor tiles
    Tile *getLeftTile() const { return left; }
    Tile *getRightTile() const { return right; }
    Tile *getUpTile() const { return up; }
    Tile *getDownTile() const { return down; }
    
    // Getter for the number of live cells
    uint32_t getLiveCount() const { return liveCount; }

    // Friend declarations to allow access to private members
    friend class VLife;
    friend class VLifeTest;
};
