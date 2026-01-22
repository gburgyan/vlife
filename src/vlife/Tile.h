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
    uint64_t changesAccumulator{0};  // OR of all changeBuff values, for quick Phase 2 skip
    uint32_t liveCount; // Tracks the number of live cells in this tile

    // Activity tracking for tile-level skip optimization
    // True if the tile has any non-zero content (live cells or neighbor counts)
    // This allows skipping completely dead tiles during generation scan
    bool hasAnyActivity{false};

    // Block-level modification tracking (64 bits = 8x8 blocks of 4x4 cells each)
    // Bit index: ((y & 0x1C) << 1) | (x >> 2)
    // Set in Phase 2 when neighbor counts updated; cleared at Phase 1 start
    uint64_t wasModified{~0ULL};  // Initialize to all-modified for first gen

    // Cooldown counter for tile eviction - prevents flapping
    // Tiles must remain inactive for TILE_COOLDOWN_GENERATIONS before being evicted
    size_t cooldownCounter{TILE_COOLDOWN_GENERATIONS};

    std::mutex tileMutex;

public:
    Tile(VLife *board, int32_t tileX, int32_t tileY);

    void setNeighbor(Tile *tile, int dx, int dy);

    void runGenerationPrepare();

    // Template parameter controls whether atomic operations are used for boundary cells:
    // - UseAtomics=true: Use CAS loops for boundary updates (required for parallel execution)
    // - UseAtomics=false: Use direct writes (safe for sequential execution, ~10-20% faster)
    template<bool UseAtomics = true>
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
    // Template parameter controls atomic vs non-atomic for cross-tile updates
    template<bool UseAtomics = true>
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

    // Non-atomic versions for sequential execution (no concurrent access possible)
    // These provide ~10-20% speedup by eliminating CAS loop overhead
    inline void nonAtomicApplyDelta(int x, int y, int8_t delta);
    void nonAtomicAddBoundaryDeltas(int y, const int8_t* deltaArray);

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

    // Activity tracking methods for tile-level skip optimization
    inline void markWordActive(uint32_t) {
        hasAnyActivity = true;
    }

    // Check if the tile has any potential activity (live cells or neighbor counts)
    // Used for tile-level skip optimization
    inline bool hasActivity() const {
        return hasAnyActivity;
    }

    // Block modification tracking methods (64 bits = 8x8 blocks of 4x4 cells)
    // Mark block containing (x,y) as modified
    inline void markBlockModified(int x, int y) {
        int blockIdx = ((y & 0x1C) << 1) | (x >> 2);
        wasModified |= (1ULL << blockIdx);
    }

    // Atomic version for cross-tile updates
    inline void atomicMarkBlockModified(int x, int y) {
        int blockIdx = ((y & 0x1C) << 1) | (x >> 2);
        __atomic_fetch_or(&wasModified, 1ULL << blockIdx, __ATOMIC_RELAXED);
    }

    // Mark the 4 corner blocks of a cell's 3x3 affected area
    // Optimized to check boundaries once and batch interior updates
    inline void markChangeCorners(int x, int y) {
        // Check boundaries once
        bool leftOut = (x - 1) < 0;
        bool rightOut = (x + 1) >= TILE_WIDTH;
        bool topOut = (y - 1) < 0;
        bool bottomOut = (y + 1) >= TILE_HEIGHT;

        // Fast path: all corners interior (most common case)
        if (!leftOut && !rightOut && !topOut && !bottomOut) {
            // Compute all 4 block indices and OR together
            int ul = (((y - 1) & 0x1C) << 1) | ((x - 1) >> 2);
            int ur = (((y - 1) & 0x1C) << 1) | ((x + 1) >> 2);
            int ll = (((y + 1) & 0x1C) << 1) | ((x - 1) >> 2);
            int lr = (((y + 1) & 0x1C) << 1) | ((x + 1) >> 2);
            wasModified |= (1ULL << ul) | (1ULL << ur) | (1ULL << ll) | (1ULL << lr);
            return;
        }

        // Compute adjusted coordinates for each boundary case
        int leftX = leftOut ? TILE_WIDTH - 1 : x - 1;
        int rightX = rightOut ? 0 : x + 1;
        int topY = topOut ? TILE_HEIGHT - 1 : y - 1;
        int bottomY = bottomOut ? 0 : y + 1;

        // Upper-left corner
        if (topOut && leftOut) {
            if (upLeft) upLeft->atomicMarkBlockModified(leftX, topY);
        } else if (topOut) {
            if (up) up->atomicMarkBlockModified(leftX, topY);
        } else if (leftOut) {
            if (left) left->atomicMarkBlockModified(leftX, topY);
        } else {
            markBlockModified(leftX, topY);
        }

        // Upper-right corner
        if (topOut && rightOut) {
            if (upRight) upRight->atomicMarkBlockModified(rightX, topY);
        } else if (topOut) {
            if (up) up->atomicMarkBlockModified(rightX, topY);
        } else if (rightOut) {
            if (right) right->atomicMarkBlockModified(rightX, topY);
        } else {
            markBlockModified(rightX, topY);
        }

        // Lower-left corner
        if (bottomOut && leftOut) {
            if (downLeft) downLeft->atomicMarkBlockModified(leftX, bottomY);
        } else if (bottomOut) {
            if (down) down->atomicMarkBlockModified(leftX, bottomY);
        } else if (leftOut) {
            if (left) left->atomicMarkBlockModified(leftX, bottomY);
        } else {
            markBlockModified(leftX, bottomY);
        }

        // Lower-right corner
        if (bottomOut && rightOut) {
            if (downRight) downRight->atomicMarkBlockModified(rightX, bottomY);
        } else if (bottomOut) {
            if (down) down->atomicMarkBlockModified(rightX, bottomY);
        } else if (rightOut) {
            if (right) right->atomicMarkBlockModified(rightX, bottomY);
        } else {
            markBlockModified(rightX, bottomY);
        }
    }

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
