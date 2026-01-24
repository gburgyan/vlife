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

// Eviction optimization: periodic scan with age threshold
// EVICTION_CHECK_INTERVAL must be a power of 2 for fast modulo via bitmask
static constexpr size_t EVICTION_CHECK_INTERVAL = 16;  // Check every N generations
static constexpr uint8_t EVICTION_AGE_THRESHOLD = 4;   // Evict if empty for N generations

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

    // Per-row activity tracking for tile eviction (8 bits = 8 block rows)
    // Bit is set when content found during Phase 1 scan
    // Rows with rowMask == 0 preserve their activity bit (content unchanged)
    uint8_t activityRows{0};

    // Block-level modification tracking (64 bits = 8x8 blocks of 4x4 cells each)
    // Bit index: ((y & 0x1C) << 1) | (x >> 2)
    // Set in Phase 2 when neighbor counts updated; cleared at Phase 1 start
    uint64_t wasModified{~0ULL};  // Initialize to all-modified for first gen

    // Generation timestamp for eviction optimization
    // Tracks when the tile was last modified (wraps at 256, uses modular arithmetic)
    uint8_t lastModifiedGeneration{0};

    // Atomic flag to prevent duplicate Phase 1 queue entries
    // Set by tryQueueForPhase1(), cleared by clearQueueFlag()
    std::atomic<uint8_t> queuedForPhase1{0};

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

    // Column delta methods: apply accumulated deltas to a vertical column
    // Used for buffered left/right neighbor updates (one queue check per neighbor)
    void atomicAddColumnDeltas(int x, const int8_t* deltaArray);
    void nonAtomicAddColumnDeltas(int x, const int8_t* deltaArray);

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
    // Mark the block row containing this word as having activity
    // Called by setCell/updateNeighborCount to ensure new content is processed
    inline void markWordActive(uint32_t wordIdx) {
        // Each block row has 8 words (64-bit values)
        int blockRow = wordIdx / 8;
        activityRows |= (1 << blockRow);
    }

    // Check if the tile has changes to apply in Phase 2
    // Used for Phase 2 queue optimization to skip tiles with no changes
    inline bool hasChanges() const {
        return changesAccumulator != 0;
    }

    // Check if a neighbor tile is already in Phase 2 queue
    // If so, it will self-queue for Phase 1, so we don't need to queue it
    // Safe to call from other tiles during Phase 2 (changesAccumulator is read-only)
    inline bool willSelfQueueForPhase1() const {
        return changesAccumulator != 0;
    }

    // Check if the tile is safe to evict
    // A tile is safe to evict only if: no live cells, no activity, and no pending modifications
    // - active==0 && mod!=0: Phase 2 just changed something, tile is in play
    // - active!=0 && mod==0: Static pattern (e.g., block) - has content but no pending changes
    inline bool isSafeToEvict() const {
        return liveCount == 0 && activityRows == 0 && wasModified == 0;
    }

    // Block modification tracking methods (64 bits = 8x8 blocks of 4x4 cells)
    // Mark block containing (x,y) as modified
    // NOTE: Does NOT queue for Phase 1 - caller is responsible for batching queue calls
    inline void markBlockModified(int x, int y) {
        int blockIdx = ((y & 0x1C) << 1) | (x >> 2);
        wasModified |= (1ULL << blockIdx);
    }

    // Atomic version for cross-tile updates
    // NOTE: Does NOT queue for Phase 1 - caller is responsible for batching queue calls
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

        // Slow path: handle boundaries
        // Compute adjusted coordinates for each boundary case
        int leftX = leftOut ? TILE_WIDTH - 1 : x - 1;
        int rightX = rightOut ? 0 : x + 1;
        int topY = topOut ? TILE_HEIGHT - 1 : y - 1;
        int bottomY = bottomOut ? 0 : y + 1;

        // Upper-left corner
        if (topOut && leftOut) {
            if (upLeft) { upLeft->atomicMarkBlockModified(leftX, topY); }
        } else if (topOut) {
            if (up) { up->atomicMarkBlockModified(leftX, topY); }
        } else if (leftOut) {
            if (left) { left->atomicMarkBlockModified(leftX, topY); }
        } else {
            markBlockModified(leftX, topY);
        }

        // Upper-right corner
        if (topOut && rightOut) {
            if (upRight) { upRight->atomicMarkBlockModified(rightX, topY); }
        } else if (topOut) {
            if (up) { up->atomicMarkBlockModified(rightX, topY); }
        } else if (rightOut) {
            if (right) { right->atomicMarkBlockModified(rightX, topY); }
        } else {
            markBlockModified(rightX, topY);
        }

        // Lower-left corner
        if (bottomOut && leftOut) {
            if (downLeft) { downLeft->atomicMarkBlockModified(leftX, bottomY); }
        } else if (bottomOut) {
            if (down) { down->atomicMarkBlockModified(leftX, bottomY); }
        } else if (leftOut) {
            if (left) { left->atomicMarkBlockModified(leftX, bottomY); }
        } else {
            markBlockModified(leftX, bottomY);
        }

        // Lower-right corner
        if (bottomOut && rightOut) {
            if (downRight) { downRight->atomicMarkBlockModified(rightX, bottomY); }
        } else if (bottomOut) {
            if (down) { down->atomicMarkBlockModified(rightX, bottomY); }
        } else if (rightOut) {
            if (right) { right->atomicMarkBlockModified(rightX, bottomY); }
        } else {
            markBlockModified(rightX, bottomY);
        }
    }

    // Getter for last modified generation (for eviction logic)
    uint8_t getLastModifiedGeneration() const { return lastModifiedGeneration; }

    // Update the last modified generation timestamp
    void updateLastModified(uint8_t currentGen) { lastModifiedGeneration = currentGen; }

    // Phase 1 queue helpers - atomic to avoid duplicate queue entries in parallel Phase 2
    // Returns true if this tile was successfully queued (first to claim), false if already queued
    inline bool tryQueueForPhase1() {
        uint8_t expected = 0;
        return queuedForPhase1.compare_exchange_strong(expected, 1,
            std::memory_order_relaxed, std::memory_order_relaxed);
    }

    // Clear the queue flag - called at start of Phase 1 when processing this tile
    inline void clearQueueFlag() {
        queuedForPhase1.store(0, std::memory_order_relaxed);
    }

    // Friend declarations to allow access to private members
    friend class VLife;
    friend class VLifeTest;
    friend class TileEdgeTest;
};
