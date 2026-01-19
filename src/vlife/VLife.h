//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <future>
#include <functional>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include "../GameOfLife.h"

// Forward declaration
class Tile;

// Simple thread pool for parallel generation processing
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    // Submit a batch of tasks and wait for all to complete
    void runBatch(const std::vector<std::function<void()>>& tasks);

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> taskQueue;
    std::mutex queueMutex;
    std::condition_variable taskAvailable;
    std::condition_variable tasksDone;
    std::atomic<size_t> pendingTasks{0};
    std::atomic<bool> stop{false};

    void workerLoop();
};

// Packed deltas for neighbor count updates when cell pairs change
// Used by the LUT-based optimization in runGenerationChanges
struct PackedDeltas {
    int8_t sameRow[2];     // [0]=x-1 neighbor, [1]=x+2 neighbor (horizontal, same row)
    int8_t verticalRow[4]; // [0]=x-1, [1]=x, [2]=x+1, [3]=x+2 (for rows above/below)
};

// Tile dimensions in base-2 multiples
#define TILE_WIDTH_2 5 // 2^5 = 32
#define TILE_HEIGHT_2 5 // 2^5 = 32

// Tile dimensions in base 10
#define TILE_WIDTH (1 << TILE_WIDTH_2) // 32
#define TILE_HEIGHT (1 << TILE_HEIGHT_2) // 32

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

    // Enable or disable parallel processing (for benchmarking)
    void setParallelEnabled(bool enabled) { parallelEnabled = enabled; }
    bool isParallelEnabled() const { return parallelEnabled; }

    // Gets a tile at the specified coordinates. Creates it if it doesn't exist.
    Tile *getTile(int32_t tileX, int32_t tileY);

    // Gets a tile only if it already exists, returns nullptr otherwise
    Tile *getTileIfExists(int32_t tileX, int32_t tileY);

    // Removes a tile at the specified coordinates
    void removeTile(int32_t tileX, int32_t tileY);

private:
    void populateRuleLUT();
    void populateUpdateLUT();

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

    // Mutex for thread-safe tile creation during parallel processing
    // Uses shared_mutex for reader-writer locking: multiple readers (tile lookups)
    // can proceed concurrently, while writes (tile creation) require exclusive access
    mutable std::shared_mutex tilesMutex;

    // Spatial ordering for cache-friendly iteration
    std::vector<Tile*> spatialOrder;
    bool spatialOrderDirty = true;

    // 4-color scheme for parallel processing: tiles colored by (tileX % 2, tileY % 2)
    // Color 0: (even X, even Y), Color 1: (odd X, even Y)
    // Color 2: (even X, odd Y),  Color 3: (odd X, odd Y)
    // Tiles of the same color are at least 2 steps apart, so they share no neighbors
    std::vector<Tile*> colorGroups[4];
    bool parallelEnabled = true;

    // Thread pool for parallel processing (lazy initialized)
    std::unique_ptr<ThreadPool> threadPool;

    // Rebuild the spatial order vector
    void rebuildSpatialOrder();

    // Rebuild color groups for parallel processing
    void rebuildColorGroups();

    // Sequential implementation (fallback for small tile counts)
    void runGenerationSequential();

    // Evict tiles with no live cells
    void evictDeadTiles();

public:
    std::byte ruleLUT[256];
    std::int16_t updateLUT[1024];
    PackedDeltas deltaLUT[16];  // LUT for neighbor count updates indexed by change/alive flags
};
