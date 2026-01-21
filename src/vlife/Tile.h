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

// Number of generations a dead tile must remain inactive before being evicted
// This prevents "flapping" (tiles being repeatedly created and destroyed)
static constexpr uint8_t TILE_COOLDOWN_GENERATIONS = 4;

// Align Tile to 64-byte cache line boundaries to prevent false sharing
// when multiple threads update adjacent tiles in parallel
class alignas(64) Tile {
    VLife *board;
    int32_t tileX;
    int32_t tileY;

    Tile *left;
    Tile *right;
    Tile *up;
    Tile *down;
    Tile *upLeft;
    Tile *upRight;
    Tile *downLeft;
    Tile *downRight;

    uint64_t cells[TILE_64S]{};
    uint64_t changes[TILE_CHANGE_64S]{};
    uint32_t liveCount; // Tracks the number of live cells in this tile

    // Activity tracking for hierarchical skip optimization
    // Each bit in activityMask corresponds to one 64-bit word in cells[]
    // A word is "active" if it has any non-zero content (live cells or neighbor counts)
    // This allows skipping dead regions during generation scan
    uint64_t activityMask{};

    // Cooldown counter for tile eviction - prevents flapping
    // Tiles must remain inactive for TILE_COOLDOWN_GENERATIONS before being evicted
    size_t cooldownCounter{TILE_COOLDOWN_GENERATIONS};

    std::mutex tileMutex;

public:
    Tile(VLife *board, int32_t tileX, int32_t tileY);

    void setNeighbor(Tile *tile, int dx, int dy);

    void runGenerationPrepare();
    void runGenerationChanges();

#ifdef VLIFE_AVX512_ENABLED
    // AVX-512 optimized version of runGenerationPrepare
    // Uses direct SIMD computation instead of LUT lookups
    void runGenerationPrepare_AVX512();
#endif

    void lockTile();
    void unlockTile();

    // Cell access methods
    bool getCell(uint32_t localX, uint32_t localY) const;
    void setCell(uint32_t localX, uint32_t localY, bool alive);
    void updateNeighborCount(int localX, int localY, bool increment);

    // Check if a cell is on the boundary of the tile (could race with neighbor tiles)
    // Boundary cells: first/last row or first/last column
    inline bool isBoundaryCell(int x, int y) const {
        return x == 0 || x == TILE_WIDTH - 1 || y == 0 || y == TILE_HEIGHT - 1;
    }

    // LUT-based optimization helpers for runGenerationChanges
    // Apply a single delta to a cell's neighbor count (inline for performance)
    // Uses atomic for boundary cells when in full parallel mode
    inline void applyDelta(int x, int y, int8_t delta);

    // Apply vertical deltas to 4 consecutive cells in a row (x-1, x, x+1, x+2)
    // baseX is the x coordinate of the right cell in the pair (even x)
    void applyVerticalDeltas(int baseX, int y, const int8_t* deltas);

    // Apply deltas for a cell pair using LUT (optimized version)
    void applyDeltasForCellPair(int baseX, int localY, const PackedDeltas& deltas);

    // Atomic versions for cross-tile updates (thread-safe for parallel processing)
    // These use compare-and-swap to safely update neighbor counts when
    // multiple tiles are processed in parallel
    void atomicApplyDelta(int x, int y, int8_t delta);
    void atomicApplyVerticalDeltas(int baseX, int y, const int8_t* deltas);

    // Buffered boundary optimization: apply accumulated deltas from an int8_t array
    // Each element contains the total delta for that cell's neighbor count
    // This batches all boundary deltas for a row and applies them efficiently
    void atomicAddBoundaryDeltas(int y, const int8_t* deltaArray);

    // Helper to ensure a neighbor tile exists, creating it on demand if needed
    // Returns the neighbor tile pointer (never null after call)
    Tile* ensureNeighborTile(int dx, int dy);

    // Getters for tile coordinates
    int32_t getTileX() const { return tileX; }
    int32_t getTileY() const { return tileY; }

    // Getters for neighbor tiles
    Tile *getLeftTile() const { return left; }
    Tile *getRightTile() const { return right; }
    Tile *getUpTile() const { return up; }
    Tile *getDownTile() const { return down; }
    Tile *getUpLeftTile() const { return upLeft; }
    Tile *getUpRightTile() const { return upRight; }
    Tile *getDownLeftTile() const { return downLeft; }
    Tile *getDownRightTile() const { return downRight; }
    
    // Getter for the number of live cells
    uint32_t getLiveCount() const { return liveCount; }

    // Activity mask methods for hierarchical skip optimization
    inline void markWordActive(uint32_t wordIdx) {
        activityMask |= (1ULL << wordIdx);
    }

    // Check if the tile has any potential activity (live cells or neighbor counts)
    // Used for tile-level skip optimization
    inline bool hasActivity() const {
        return activityMask != 0;
    }

    // Get the number of active word groups (for profiling/debugging)
    inline int getActiveWordCount() const {
        return __builtin_popcountll(activityMask);
    }

    // Estimate the amount of work for this tile (higher = more work)
    // Used for work-weighted scheduling to put heavy tiles first
    inline uint32_t estimateWork() const {
        return __builtin_popcountll(activityMask);
    }

    // Rebuild the activity mask by scanning all words
    void rebuildActivityMask();

    // Cooldown methods for tile eviction management
    // Reset cooldown counter when tile becomes active
    void resetCooldown() { cooldownCounter = TILE_COOLDOWN_GENERATIONS; }

    // Decrement cooldown, returns true when tile should be evicted
    bool decrementCooldown() {
        if (cooldownCounter == 0) {
            cooldownCounter = TILE_COOLDOWN_GENERATIONS;
            return false;  // First time inactive, start cooldown
        }
        return --cooldownCounter == 0;  // Evict when counter reaches 0
    }

    // Friend declarations to allow access to private members
    friend class VLife;
    friend class VLifeTest;
    friend class TileEdgeTest;
};
