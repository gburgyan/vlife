//
// Created by George Burgyan on 3/1/25.
//

#include "VLife.h"
#include "Tile.h"

VLife::VLife() {
    resetBoard();
    populateRuleLUT();
}

VLife::~VLife() {
    // Clear all tiles
    tiles.clear();
}

void VLife::resetBoard() {
    // Clear all existing tiles
    tiles.clear();
}

Tile *VLife::getTile(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};

    // Check if the tile already exists
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }

    // Create a new tile
    auto newTile = std::make_unique<Tile>(this, tileX, tileY);
    Tile *tilePtr = newTile.get();
    tiles[coord] = std::move(newTile);

    // Connect to neighboring tiles if they exist

    // Check left (-1, 0)
    auto leftTile = getTileIfExists(tileX - 1, tileY);
    if (leftTile) {
        tilePtr->setNeighbor(leftTile, -1, 0);
    }

    // Check right (1, 0)
    auto rightTile = getTileIfExists(tileX + 1, tileY);
    if (rightTile) {
        tilePtr->setNeighbor(rightTile, 1, 0);
    }

    // Check up (0, -1)
    auto upTile = getTileIfExists(tileX, tileY - 1);
    if (upTile) {
        tilePtr->setNeighbor(upTile, 0, -1);
    }

    // Check down (0, 1)
    auto downTile = getTileIfExists(tileX, tileY + 1);
    if (downTile) {
        tilePtr->setNeighbor(downTile, 0, 1);
    }

    return tilePtr;
}

// Helper method to get a tile only if it already exists
Tile *VLife::getTileIfExists(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }
    return nullptr;
}

GameOfLife::CellState VLife::getCell(uint32_t x, uint32_t y) const {
    // Calculate which tile this cell belongs to
    int32_t tileX = static_cast<int32_t>(x) >> TILE_WIDTH_2;
    int32_t tileY = static_cast<int32_t>(y) >> TILE_HEIGHT_2;

    // Calculate the position within the tile
    uint32_t localX = x & (TILE_WIDTH - 1);
    uint32_t localY = y & (TILE_HEIGHT - 1);

    // Try to find the tile
    TileCoord coord{tileX, tileY};
    auto it = tiles.find(coord);
    if (it == tiles.end()) {
        // Tile doesn't exist, cell is dead
        return CellState::DEAD;
    }

    // Use Tile's getCell method to check if the cell is alive
    bool isAlive = it->second->getCell(localX, localY);

    return isAlive ? CellState::ALIVE : CellState::DEAD;
}

void VLife::setCell(uint32_t x, uint32_t y, CellState state) {
    // Calculate which tile this cell belongs to
    int32_t tileX = static_cast<int32_t>(x) >> TILE_WIDTH_2;
    int32_t tileY = static_cast<int32_t>(y) >> TILE_HEIGHT_2;

    // Get or create the tile
    Tile *tile = getTile(tileX, tileY);

    // Calculate the position within the tile
    uint32_t localX = x & (TILE_WIDTH - 1);
    uint32_t localY = y & (TILE_HEIGHT - 1);

    // Use Tile's setCell method to set the cell state
    tile->setCell(localX, localY, state == CellState::ALIVE);
}

std::vector<GameOfLife::CellState> VLife::getCells(uint32_t startX, uint32_t startY, uint32_t width,
                                                   uint32_t height) const {
    std::vector<CellState> result;
    result.reserve(width * height);

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            result.push_back(getCell(startX + x, startY + y));
        }
    }

    return result;
}

void VLife::setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height,
                     const std::vector<CellState> &cells) {
    if (cells.size() != width * height) {
        return; // Error: cells vector size doesn't match width*height
    }

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            setCell(offsetX + x, offsetY + y, cells[y * width + x]);
        }
    }
}

void VLife::runGeneration() {
    // First pass: prepare all tiles for the generation
    for (auto &tilePair: tiles) {
        tilePair.second->runGenerationPrepare();
    }

    // Second pass: apply the changes
    for (auto &tilePair: tiles) {
        tilePair.second->runGenerationChanges();
    }
}

void VLife::runGenerations(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        runGeneration();
    }
}

void VLife::removeTile(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};

    // Find the tile
    auto it = tiles.find(coord);
    if (it == tiles.end()) {
        return; // Tile doesn't exist
    }

    Tile *tile = it->second.get();

    // Unlink from neighbors
    if (tile->left) {
        tile->left->right = nullptr;
    }
    if (tile->right) {
        tile->right->left = nullptr;
    }
    if (tile->up) {
        tile->up->down = nullptr;
    }
    if (tile->down) {
        tile->down->up = nullptr;
    }

#ifdef DEBUG_VLIFE
    // Verify all cells are dead
    for (int i = 0; i < TILE_64S; i++) {
        if (tile->cells[i] != 0) {
            // Some cells are still alive, don't remove the tile
            return;
        }
    }
#endif

    // Remove the tile from the map
    tiles.erase(it);
}

void VLife::populateRuleLUT() {
    int minBorn = 3;
    int maxBorn = 3;
    int minSurvive = 2;
    int maxSurvive = 3;

    for (int i = 0; i < 256; i++) {
        int leftAlive = i & 0x10;
        int leftNeighbors = i & 0xE0 >> 5;
        int rightAlive = i & 0x01;
        int rightNeighbors = i & 0x0E >> 1;

        leftNeighbors += rightAlive;
        rightNeighbors += leftAlive;

        bool leftNewAlive;
        bool leftChanged;
        bool rightNewAlive;
        bool rightChanged;

        if (leftAlive) {
            if (leftNeighbors < minSurvive || leftNeighbors > maxSurvive) {
                leftNewAlive = false;
                leftChanged = true;
            } else {
                leftNewAlive = true;
                leftChanged = false;
            }
        } else {
            if (leftNeighbors >= minBorn && leftNeighbors <= maxBorn) {
                leftNewAlive = true;
                leftChanged = true;
            } else {
                leftNewAlive = false;
                leftChanged = false;
            }
        }

        if (rightAlive) {
            if (rightNeighbors < minSurvive || rightNeighbors > maxSurvive) {
                rightNewAlive = false;
                rightChanged = true;
            } else {
                rightNewAlive = true;
                rightChanged = false;
            }
        } else {
            if (rightNeighbors >= minBorn && rightNeighbors <= maxBorn) {
                rightNewAlive = true;
                rightChanged = true;
            } else {
                rightNewAlive = false;
                rightChanged = false;
            }
        }

        int result = leftChanged << 1 | rightChanged;
        ruleLUT[i] = static_cast<std::byte>(result);
    }
}
