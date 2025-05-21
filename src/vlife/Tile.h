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

    uint64_t cells[TILE_64S];
    uint64_t changes[TILE_CHANGE_64S];

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

    // Getters for tile coordinates
    int32_t getTileX() const { return tileX; }
    int32_t getTileY() const { return tileY; }

    // Getters for neighbor tiles
    Tile *getLeftTile() const { return left; }
    Tile *getRightTile() const { return right; }
    Tile *getUpTile() const { return up; }
    Tile *getDownTile() const { return down; }

    // Friend declaration to allow VLife to access private members
    friend class VLife;
};
