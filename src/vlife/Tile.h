//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include "VLife.h"

#define TILE_CELLS TILE_WIDTH * TILE_HEIGHT
#define TILE_BYTES TILE_CELLS / 2
#define TILE_64S TILE_BYTES / 8
#define TILE_CHANGE_64S TILE_CELLS / 64
#define TILE_64S_WIDTH TILE_WIDTH / 64
#define TILE_64S_HEIGHT TILE_HEIGHT / 64

class Tile {
    VLife* board;
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
    void runGenerationPrepare();
    void runGenerationChanges();

    void lockTile();
    void unlockTile();
};

