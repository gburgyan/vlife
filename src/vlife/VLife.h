//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <unordered_map>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <cassert>
#include "../GameOfLife.h"
#include "TilePool.h"

// Pre-allocated atomic queue for lock-free concurrent writes
// Optimized for: multiple producers, single iteration pass, bulk clear between generations
// Trade-off: Fixed max capacity for O(1) push and zero merge overhead
template<typename T, size_t MaxSize = 65536>
struct AtomicQueue {
    std::array<T, MaxSize> data;
    std::atomic<size_t> count{0};

    void push_back(T item) {
        size_t idx = count.fetch_add(1, std::memory_order_relaxed);
        assert(idx < MaxSize && "AtomicQueue overflow - increase MaxSize");
        data[idx] = item;
    }

    void clear() {
        count.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] size_t size() const {
        return count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool empty() const {
        return size() == 0;
    }

    T* begin() { return data.data(); }
    T* end() { return data.data() + size(); }
    [[nodiscard]] const T* begin() const { return data.data(); }
    [[nodiscard]] const T* end() const { return data.data() + size(); }
};

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

// Compact precomputed corner block masks for markChangeCorners interior fast path
// Exploits y-symmetry and encodes change state directly in LUT index for 16x size reduction
// Index: (yClass << 6) | (xPair << 2) | changeState
//   yClass: localY & 3 (0-3) - position within 4-row block
//   xPair: baseX >> 1 (0-15) - which cell pair in the row
//   changeState: (leftChanged << 1) | rightChanged (0-3)
// At runtime, x-block masks are shifted to the correct block-row position
struct CompactCornerMask {
    uint8_t upper;  // Combined x-block bits for upper corners (y-1)
    uint8_t lower;  // Combined x-block bits for lower corners (y+1)
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

    // Phase 1 queue optimization: add a tile to the next generation's Phase 1 queue
    // Called from Tile::runGenerationChanges() when a tile modifies itself or neighbors
    // Thread-safe for parallel Phase 2 execution
    void addToNextPhase1Queue(Tile* tile);

    // Swap Phase 1 queues between generations
    // Called at the start of Phase 1 to switch from building queue to processing queue
    void swapPhase1Queues();


#ifdef VLIFE_METRICS_ENABLED
    // Metrics collection support
    void setMetricsCollector(MetricsCollector* collector) { metricsCollector = collector; }
    MetricsCollector* getMetricsCollector() const { return metricsCollector; }
#endif

private:
    void populateRuleLUT();
    void populateUpdateLUT();
    void populateCornerMaskLUT();

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

    // Generation counter for metrics
    uint64_t generationNumber = 0;

    // Queue of tiles that had changes in Phase 1, for efficient Phase 2 processing
    // Using AtomicQueue for lock-free concurrent writes with zero merge overhead
    AtomicQueue<Tile*> tilesWithChanges;  // For parallel path
    std::vector<Tile*> changedTilesSequential;       // For sequential path

    // Phase 1 queue optimization: instead of scanning all tiles, track which tiles
    // need Phase 1 processing. Built during Phase 2 of previous generation.
    // Double-buffered with index swap for zero-copy queue rotation:
    // - phase1Queues[currentPhase1Queue] is the current generation's queue to process
    // - phase1Queues[1 - currentPhase1Queue] is the next generation's queue being built
    AtomicQueue<Tile*> phase1Queues[2];
    AtomicQueue<Tile*>* phase1Queue{&phase1Queues[0]};      // Pointer to current generation's queue
    AtomicQueue<Tile*>* nextPhase1Queue{&phase1Queues[1]};  // Pointer to next generation's queue

    // Flag indicating Phase 1 queue needs bootstrap (first gen or after setCell)
    // Without this flag, the queue bootstrap would run every generation when patterns stabilize
    // (empty queue), causing static patterns to be re-processed unnecessarily
    bool phase1QueueNeedsBootstrap{true};

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
    CompactCornerMask compactCornerMaskLUT[256];  // LUT for markChangeCorners interior fast path (4 yClasses × 16 xPairs × 4 changeStates)
};
