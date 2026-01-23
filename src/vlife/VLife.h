//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <tbb/concurrent_vector.h>
#include "../GameOfLife.h"
#include "TilePool.h"

// Forward declaration
class Tile;

#ifdef VLIFE_METRICS_ENABLED
class MetricsCollector;
#endif

// Packed deltas for neighbor count updates when cell pairs change
// Used by the LUT-based optimization in runGenerationChanges
struct PackedDeltas {
    int8_t sameRow[2];     // [0]=x-1 neighbor, [1]=x+2 neighbor (horizontal, same row)
    int8_t verticalRow[4]; // [0]=x-1, [1]=x, [2]=x+1, [3]=x+2 (for rows above/below)
};

// Tile dimensions as compile-time constants
// Using constexpr instead of macros for type safety and namespace hygiene
namespace VLifeConstants {
    constexpr int TILE_WIDTH_BITS = 5;   // 2^5 = 32
    constexpr int TILE_HEIGHT_BITS = 5;  // 2^5 = 32
    constexpr int TILE_WIDTH = 1 << TILE_WIDTH_BITS;   // 32
    constexpr int TILE_HEIGHT = 1 << TILE_HEIGHT_BITS; // 32
}

// Preserve macros for backward compatibility with existing code
#define TILE_WIDTH_2 VLifeConstants::TILE_WIDTH_BITS
#define TILE_HEIGHT_2 VLifeConstants::TILE_HEIGHT_BITS
#define TILE_WIDTH VLifeConstants::TILE_WIDTH
#define TILE_HEIGHT VLifeConstants::TILE_HEIGHT

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

    // Enable or disable buffered boundary optimization (for A/B comparison)
    // When enabled, cross-tile boundary deltas are buffered locally and applied
    // with a single atomic fetch_add per word instead of individual CAS operations
    void setBufferedBoundaryEnabled(bool enabled) { bufferedBoundaryEnabled = enabled; }
    bool isBufferedBoundaryEnabled() const { return bufferedBoundaryEnabled; }

    // Gets a tile at the specified coordinates. Creates it if it doesn't exist.
    Tile *getTile(int32_t tileX, int32_t tileY);

    // Gets a tile only if it already exists, returns nullptr otherwise
    Tile *getTileIfExists(int32_t tileX, int32_t tileY);

    // Removes a tile at the specified coordinates
    void removeTile(int32_t tileX, int32_t tileY);

    // Returns the current number of tiles
    size_t getTileCount() const { return tiles.size(); }

    // Calculate total live cells across all tiles
    uint64_t getTotalLiveCells() const;

    // Get board extent (min/max tile coordinates)
    void getBoardExtent(int32_t& minX, int32_t& maxX, int32_t& minY, int32_t& maxY) const;

    // Get current generation number mod 256 for tile eviction timestamps
    uint8_t getCurrentGenerationMod256() const {
        return static_cast<uint8_t>(generationNumber);
    }

#ifdef VLIFE_METRICS_ENABLED
    // Metrics collection support
    void setMetricsCollector(MetricsCollector* collector) { metricsCollector = collector; }
    MetricsCollector* getMetricsCollector() const { return metricsCollector; }
#endif

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

    // Map to store tiles (pool manages lifetime)
    std::unordered_map<TileCoord, Tile*, TileCoordHash> tiles;

    // Tile pool allocator for efficient allocation/deallocation
    TilePool tilePool{this};

    // Mutex for thread-safe tile creation during parallel processing
    // Uses shared_mutex for reader-writer locking: multiple readers (tile lookups)
    // can proceed concurrently, while writes (tile creation) require exclusive access
    mutable std::shared_mutex tilesMutex;

    bool parallelEnabled = true;
    // Buffered boundary optimization - accumulates deltas locally then applies them
    // in batch at the end of runGenerationChanges(). Reduces redundant tile lookups.
    bool bufferedBoundaryEnabled = true;

    // Generation counter for metrics
    uint64_t generationNumber = 0;

    // Queue of tiles that had changes in Phase 1, for efficient Phase 2 processing
    // Using persistent members to amortize allocation across generations
    tbb::concurrent_vector<Tile*> tilesWithChanges;  // For parallel path
    std::vector<Tile*> changedTilesSequential;       // For sequential path

#ifdef VLIFE_METRICS_ENABLED
    MetricsCollector* metricsCollector = nullptr;
#endif

    // Sequential implementation (fallback for small tile counts)
    void runGenerationSequential();

    // Evict tiles with no live cells
    void evictDeadTiles();

public:
    std::byte ruleLUT[256];
    PackedDeltas deltaLUT[16];  // LUT for neighbor count updates indexed by change/alive flags
};
