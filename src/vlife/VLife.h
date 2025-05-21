//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <memory>
#include <unordered_map>
#include "../GameOfLife.h"

// Forward declaration
class Tile;

// Tile dimensions in base-2 multiples
#define TILE_WIDTH_2 5 // 2^5 = 32
#define TILE_HEIGHT_2 5 // 2^5 = 32

// Tile dimensions in base 10
#define TILE_WIDTH 1 << TILE_WIDTH_2 // 32
#define TILE_HEIGHT 1 << TILE_HEIGHT_2 // 32

class VLife : public GameOfLife {

public:
    VLife();
    ~VLife() override;

    void resetBoard() override;

    [[nodiscard]] CellState getCell(uint32_t x, uint32_t y) const override;
    void setCell(uint32_t x, uint32_t y, CellState state) override;

    [[nodiscard]] std::vector<CellState> getCells(uint32_t startX, uint32_t startY, uint32_t width,
                                                  uint32_t height) const override;

    void setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height,
                  const std::vector<CellState> &cells) override;

    void runGeneration() override;
    void runGenerations(uint32_t count) override;

    // Gets a tile at the specified coordinates. Creates it if it doesn't exist.
    Tile *getTile(int32_t tileX, int32_t tileY);

    // Gets a tile only if it already exists, returns nullptr otherwise
    Tile *getTileIfExists(int32_t tileX, int32_t tileY);

    // Removes a tile at the specified coordinates
    void removeTile(int32_t tileX, int32_t tileY);

private:
    void populateRuleLUT();

    // Structure to represent tile coordinates as a key
    struct TileCoord {
        int32_t x;
        int32_t y;

        bool operator==(const TileCoord &other) const { return x == other.x && y == other.y; }
    };

    // Hash function for TileCoord
    struct TileCoordHash {
        std::size_t operator()(const TileCoord &coord) const {
            return std::hash<int32_t>()(coord.x) ^ (std::hash<int32_t>()(coord.y) << 1);
        }
    };

    // Map to store tiles
    std::unordered_map<TileCoord, std::unique_ptr<Tile>, TileCoordHash> tiles;

public:
    std::byte ruleLUT[256];
};
