//
// Created by George Burgyan on 3/1/25.
//

#include "Tile.h"
#include "VLifeMetrics.h"
#include <cstring>
#include <mutex>

// Cross-platform prefetch and SIMD support
#if defined(__x86_64__) || defined(_M_X64)
    #include <xmmintrin.h>
    #define PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
    #define PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
    #define USE_NEON_SIMD 1
#else
    #define PREFETCH(addr) ((void)0)
#endif

// AVX-512 support (either native or via SIMDE emulation)
#ifdef VLIFE_AVX512_ENABLED
    #include "CpuFeatures.h"
    #ifdef VLIFE_AVX512_NATIVE
        #include <immintrin.h>
    #else
        // SIMDE provides portable AVX-512 emulation
        // Note: SIMDE_ENABLE_NATIVE_ALIASES is defined via CMake
        #include <simde/x86/avx512.h>
    #endif
#endif

Tile::Tile(VLife *board, int32_t tileX, int32_t tileY) :
    board(board), tileX(tileX), tileY(tileY), left(nullptr), right(nullptr), up(nullptr), down(nullptr),
    liveCount(0), activityRows(0) {
    // Initialize all cells to zero (dead)
    std::memset(cells, 0, sizeof(cells));
    std::memset(changes, 0, sizeof(changes));
}

void Tile::setNeighbor(Tile *tile, int dx, int dy) {
    if (dx == -1 && dy == 0) {
        left = tile;
        if (tile) {
            tile->right = this;
        }
    } else if (dx == 1 && dy == 0) {
        right = tile;
        if (tile) {
            tile->left = this;
        }
    } else if (dx == 0 && dy == -1) {
        up = tile;
        if (tile) {
            tile->down = this;
        }
    } else if (dx == 0 && dy == 1) {
        down = tile;
        if (tile) {
            tile->up = this;
        }
    }
    // Note: Diagonal neighbors are no longer stored - they are accessed via
    // cardinal neighbor navigation (e.g., up->left for upLeft)
}

void Tile::lockTile() { tileMutex.lock(); }

void Tile::unlockTile() { tileMutex.unlock(); }

bool Tile::getCell(uint32_t localX, uint32_t localY) const {
    // Calculate cell index and bit position
    uint32_t cellIdx = (localY * TILE_WIDTH + localX) / 16;
    uint32_t bitPos = ((localY * TILE_WIDTH + localX) % 16) * 4;

    // High bit of each nibble indicates alive cell
    return (cells[cellIdx] & (1ULL << (bitPos+3))) != 0;
}

void Tile::setCell(uint32_t localX, uint32_t localY, bool alive) {
    // Calculate cell index and bit position
    uint32_t cellIdx = (localY * TILE_WIDTH + localX) / 16;
    uint32_t bitPos = ((localY * TILE_WIDTH + localX) % 16) * 4;

    // Get current state of the cell
    bool currentlyAlive = (cells[cellIdx] & (1ULL << (bitPos+3))) != 0;

    // If state isn't changing, do nothing
    if (currentlyAlive == alive) {
        return;
    }

    // Set or clear the bit based on the alive state
    if (alive) {
        cells[cellIdx] |= (1ULL << (bitPos+3));
        liveCount++; // Increment live count when a cell becomes alive
    } else {
        cells[cellIdx] &= ~(1ULL << (bitPos+3));
        liveCount--; // Decrement live count when a cell becomes dead
    }

    // Mark this word as active in the activity mask
    markWordActive(cellIdx);

    // Mark this block as modified for Phase 1 processing
    markBlockModified(localX, localY);

    // Calculate the positions of the 8 neighbors
    // Note: dx[3]=(-1,0)=left neighbor, dx[4]=(1,0)=right neighbor
    int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // Determine which neighbor is the paired cell (shares same byte)
    // For even x: paired cell is at x+1 (right neighbor, i=4)
    // For odd x: paired cell is at x-1 (left neighbor, i=3)
    int pairedNeighborIdx = (localX & 1) ? 3 : 4;

    for (int i = 0; i < 8; i++) {
        // Skip the paired cell - its neighbor count is handled implicitly
        if (i == pairedNeighborIdx) {
            continue;
        }

        int nx = static_cast<int>(localX) + dx[i];
        int ny = static_cast<int>(localY) + dy[i];

        // Handle neighbors that are in this tile
        if (nx >= 0 && nx < TILE_WIDTH && ny >= 0 && ny < TILE_HEIGHT) {
            updateNeighborCount(nx, ny, alive);
        }
        // Handle neighbors that are in adjacent tiles
        else {
            // Determine which adjacent tile and local coordinates
            int tileOffsetX = 0;
            int tileOffsetY = 0;
            int adjLocalX = nx;
            int adjLocalY = ny;

            if (nx < 0) {
                tileOffsetX = -1;
                adjLocalX = TILE_WIDTH - 1;
            } else if (nx >= TILE_WIDTH) {
                tileOffsetX = 1;
                adjLocalX = 0;
            }

            if (ny < 0) {
                tileOffsetY = -1;
                adjLocalY = TILE_HEIGHT - 1;
            } else if (ny >= TILE_HEIGHT) {
                tileOffsetY = 1;
                adjLocalY = 0;
            }

            // Get the adjacent tile, creating it on demand if necessary
            // This ensures neighbor counts are correctly propagated even when
            // cells are set before all tiles exist
            Tile* adjTile = nullptr;
            if (tileOffsetX == -1 && tileOffsetY == 0) {
                adjTile = left;
                if (!adjTile) {
                    adjTile = board->getTile(tileX - 1, tileY);
                    left = adjTile;
                }
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == 1 && tileOffsetY == 0) {
                adjTile = right;
                if (!adjTile) {
                    adjTile = board->getTile(tileX + 1, tileY);
                    right = adjTile;
                }
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == 0 && tileOffsetY == -1) {
                adjTile = up;
                if (!adjTile) {
                    adjTile = board->getTile(tileX, tileY - 1);
                    up = adjTile;
                }
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == 0 && tileOffsetY == 1) {
                adjTile = down;
                if (!adjTile) {
                    adjTile = board->getTile(tileX, tileY + 1);
                    down = adjTile;
                }
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == -1 && tileOffsetY == -1) {
                // Diagonal: up-left - fetch directly from board
                adjTile = board->getTile(tileX - 1, tileY - 1);
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == 1 && tileOffsetY == -1) {
                // Diagonal: up-right - fetch directly from board
                adjTile = board->getTile(tileX + 1, tileY - 1);
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == -1 && tileOffsetY == 1) {
                // Diagonal: down-left - fetch directly from board
                adjTile = board->getTile(tileX - 1, tileY + 1);
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            } else if (tileOffsetX == 1 && tileOffsetY == 1) {
                // Diagonal: down-right - fetch directly from board
                adjTile = board->getTile(tileX + 1, tileY + 1);
                adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
            }

            // Queue the adjacent tile for Phase 1 processing (if not already queued)
            // This ensures cells that might be born/die due to neighbor count changes are processed
            if (adjTile && adjTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(adjTile);
            }
        }
    }

}

// Helper method to update the neighbor count of a cell
void Tile::updateNeighborCount(int localX, int localY, bool increment) {
    uint32_t cellIdx = (localY * TILE_WIDTH + localX) / 16;
    uint32_t bitPos = ((localY * TILE_WIDTH + localX) % 16) * 4;

    // Extract the current neighbor count (bits 0-2)
    uint64_t mask = 0x7ULL << bitPos; // Mask for the neighbor count bits
    uint64_t currentCount = (cells[cellIdx] & mask) >> bitPos;

    // Increment or decrement the count
    if (increment) {
        currentCount++;
    } else {
        currentCount--;
    }

    // Update the neighbor count (clear bits and then set to new value)
    cells[cellIdx] &= ~mask;
    cells[cellIdx] |= (currentCount << bitPos);

    // Mark this word as active (neighbor count change means potential birth)
    markWordActive(cellIdx);

    // Mark this block as modified for Phase 1 processing
    markBlockModified(localX, localY);
}

bool Tile::runGenerationPrepare() {
#ifdef VLIFE_AVX512_ENABLED
    // Use AVX-512 optimized version if available
    if (CpuFeatures::hasAVX512Support()) {
        return runGenerationPrepare_AVX512();
    }
#endif

    // Consume modification mask (no expansion needed - markChangeCorners already
    // marks all affected blocks during Phase 2)
    uint64_t modMask = wasModified;
    wasModified = 0;  // Clear for this generation's Phase 2

    // Reset row change mask for Phase 2 short-circuit optimization
    rowChangeMask = 0;

    // Nothing modified - nothing to scan
    if (modMask == 0) {
        return false;
    }

    // Note: activityRows is preserved for unscanned rows (rowMask == 0)
    // and updated only for rows we actually scan (rowMask != 0)

    auto ruleLUT = board->ruleLUT;

#ifdef USE_NEON_SIMD
    // Optimized ARM64 implementation with improved prefetch and word-level skipping
    // Uses scalar LUT lookups (cache-efficient) with direct result accumulation
    const uint8_t* __restrict ruleLUT_u8 = reinterpret_cast<const uint8_t*>(ruleLUT);

    // Process at block row granularity (8 words = 4 cell rows = 1 block row)
    for (int blockRow = 0; blockRow < 8; blockRow++) {
        // Check if this block row has any modifications
        uint8_t rowMask = (modMask >> (blockRow * 8)) & 0xFF;
        if (rowMask == 0) {
            // Skip 8 words (4 cell rows) - changes[] stays zero from memset
            // Preserve this row's activity bit - row content is unchanged
            continue;
        }

        // Clear this row's activity bit since we're scanning it
        // Will be set if we find any content
        activityRows &= ~(1 << blockRow);

        // Process 8 words for this block row (2 iterations of 4 words each)
        int baseIdx = blockRow * 8;
        for (int iter = 0; iter < 2; iter++) {
            int i = baseIdx + iter * 4;

            // Prefetch 2 iterations ahead for better memory pipeline utilization
            if (i + 8 < TILE_64S) {
                PREFETCH(&cells[i + 8]);
            }

            // Quick check: skip if all 4 words are zero
            if (!(cells[i] || cells[i + 1] || cells[i + 2] || cells[i + 3])) {
                continue;
            }

            // Mark this block row as having activity since we found non-zero cells
            activityRows |= (1 << blockRow);

            uint64_t changeBuff = 0;

            // Fast path: if all blocks in row are modified, skip the per-word checks
            if (rowMask == 0xFF) {
                // Process word 0
                uint64_t slice = cells[i];
                if (slice != 0) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 56) | (static_cast<uint64_t>(p1) << 48);
                }
                // Process word 1
                slice = cells[i + 1];
                if (slice != 0) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 40) | (static_cast<uint64_t>(p1) << 32);
                }
                // Process word 2
                slice = cells[i + 2];
                if (slice != 0) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 24) | (static_cast<uint64_t>(p1) << 16);
                }
                // Process word 3
                slice = cells[i + 3];
                if (slice != 0) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 8) | static_cast<uint64_t>(p1);
                }
            } else {
                // Slow path: check per-word block masks for partial row activity
                // Even words (0,2) cover cells 0-15 → blocks 0-3 (bits 0-3 of rowMask)
                // Odd words (1,3) cover cells 16-31 → blocks 4-7 (bits 4-7 of rowMask)
                bool evenBlocksModified = (rowMask & 0x0F) != 0;
                bool oddBlocksModified = (rowMask & 0xF0) != 0;

                // Process word 0 (even) - check blocks 0-3
                uint64_t slice = cells[i];
                if (slice != 0 && evenBlocksModified) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 56) | (static_cast<uint64_t>(p1) << 48);
                }

                // Process word 1 (odd) - check blocks 4-7
                slice = cells[i + 1];
                if (slice != 0 && oddBlocksModified) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 40) | (static_cast<uint64_t>(p1) << 32);
                }

                // Process word 2 (even) - check blocks 0-3
                slice = cells[i + 2];
                if (slice != 0 && evenBlocksModified) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 24) | (static_cast<uint64_t>(p1) << 16);
                }

                // Process word 3 (odd) - check blocks 4-7
                slice = cells[i + 3];
                if (slice != 0 && oddBlocksModified) {
                    uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
                                  (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
                                  ruleLUT_u8[(slice >> 32) & 0xFF];
                    uint32_t p1 = (ruleLUT_u8[(slice >> 24) & 0xFF] << 6) |
                                  (ruleLUT_u8[(slice >> 16) & 0xFF] << 4) |
                                  (ruleLUT_u8[(slice >> 8) & 0xFF] << 2) |
                                  ruleLUT_u8[slice & 0xFF];
                    changeBuff |= (static_cast<uint64_t>(p0) << 8) | static_cast<uint64_t>(p1);
                }
            }

            // Split 64-bit changeBuff into two 32-bit row values
            // Upper 32 bits = row 0 (cells[i], cells[i+1])
            // Lower 32 bits = row 1 (cells[i+2], cells[i+3])
            int baseRow = i / 2;  // Each cell word pair covers one row
            uint32_t row0Changes = static_cast<uint32_t>(changeBuff >> 32);
            uint32_t row1Changes = static_cast<uint32_t>(changeBuff);

            changes[baseRow] = row0Changes;
            changes[baseRow + 1] = row1Changes;

            if (row0Changes) rowChangeMask |= (1U << baseRow);
            if (row1Changes) rowChangeMask |= (1U << (baseRow + 1));
        }  // end iter loop
    }  // end blockRow loop

#else
    // Scalar fallback for non-ARM platforms
    // Process at block row granularity (8 words = 4 cell rows = 1 block row)
    for (int blockRow = 0; blockRow < 8; blockRow++) {
        // Check if this block row has any modifications
        uint8_t rowMask = (modMask >> (blockRow * 8)) & 0xFF;
        if (rowMask == 0) {
            // Skip 8 words (4 cell rows) - changes[] stays zero from memset
            // Preserve this row's activity bit - row content is unchanged
            continue;
        }

        // Clear this row's activity bit since we're scanning it
        // Will be set if we find any content
        activityRows &= ~(1 << blockRow);

        // Process 8 words for this block row (2 iterations of 4 words each)
        int baseIdx = blockRow * 8;
        for (int iter = 0; iter < 2; iter++) {
            int i = baseIdx + iter * 4;

            // Prefetch next iteration's data
            if (i + 4 < TILE_64S) {
                PREFETCH(&cells[i + 4]);
            }

            // Quick check: skip if all 4 words are zero (no live cells, no neighbor counts)
            // This is the hierarchical skip optimization - skip dead regions
            uint64_t combined = cells[i] | cells[i + 1] | cells[i + 2] | cells[i + 3];
            if (combined == 0) {
                continue;  // Skip this group entirely - no activity possible
            }

            // Mark this block row as having activity since we found non-zero cells
            activityRows |= (1 << blockRow);

            uint64_t changeBuff = 0;

            // Fast path: if all blocks in row are modified, skip the per-word checks
            if (rowMask == 0xFF) {
                // Process word 0
                uint64_t slice = cells[i];
                if (slice != 0) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 62;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 60;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 58;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 56;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 54;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 52;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 50;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 48;
                }
                // Process word 1
                slice = cells[i + 1];
                if (slice != 0) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 46;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 44;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 42;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 40;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 38;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 36;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 34;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 32;
                }
                // Process word 2
                slice = cells[i + 2];
                if (slice != 0) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 30;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 28;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 26;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 24;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 22;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 20;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 18;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 16;
                }
                // Process word 3
                slice = cells[i + 3];
                if (slice != 0) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 56) & 0xFF)]) << 14;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 12;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 10;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 8;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 6;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 4;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 2;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]);
                }
            } else {
                // Slow path: check per-word block masks for partial row activity
                bool evenBlocksModified = (rowMask & 0x0F) != 0;
                bool oddBlocksModified = (rowMask & 0xF0) != 0;

                // Process word 0 (even) - check blocks 0-3
                uint64_t slice = cells[i];
                if (slice != 0 && evenBlocksModified) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 62;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 60;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 58;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 56;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 54;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 52;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 50;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 48;
                }

                // Process word 1 (odd) - check blocks 4-7
                slice = cells[i + 1];
                if (slice != 0 && oddBlocksModified) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 46;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 44;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 42;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 40;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 38;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 36;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 34;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 32;
                }

                // Process word 2 (even) - check blocks 0-3
                slice = cells[i + 2];
                if (slice != 0 && evenBlocksModified) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 30;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 28;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 26;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 24;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 22;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 20;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 18;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 16;
                }

                // Process word 3 (odd) - check blocks 4-7
                slice = cells[i + 3];
                if (slice != 0 && oddBlocksModified) {
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 56) & 0xFF)]) << 14;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 12;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 10;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 8;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 6;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 4;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 2;
                    changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]);
                }
            }

            // Split 64-bit changeBuff into two 32-bit row values
            // Upper 32 bits = row 0 (cells[i], cells[i+1])
            // Lower 32 bits = row 1 (cells[i+2], cells[i+3])
            int baseRow = i / 2;  // Each cell word pair covers one row
            uint32_t row0Changes = static_cast<uint32_t>(changeBuff >> 32);
            uint32_t row1Changes = static_cast<uint32_t>(changeBuff);

            changes[baseRow] = row0Changes;
            changes[baseRow + 1] = row1Changes;

            if (row0Changes) rowChangeMask |= (1U << baseRow);
            if (row1Changes) rowChangeMask |= (1U << (baseRow + 1));
        }  // end iter loop
    }  // end blockRow loop
#endif

    return rowChangeMask != 0;
}

#ifdef VLIFE_AVX512_ENABLED
// AVX-512 optimized version of runGenerationPrepare
// Replaces LUT lookups with direct SIMD computation for better throughput
//
// Cell format (per nibble): bit 3 = alive, bits 0-2 = neighbor count (excluding paired cell)
// Byte format: [left_alive:1][left_neighbors:3][right_alive:1][right_neighbors:3]
//
// Conway's Rules:
// - Survive: alive AND (neighbors == 2 OR neighbors == 3)
// - Birth: NOT alive AND neighbors == 3
//
// The true neighbor count = stored count + paired cell's alive state
bool Tile::runGenerationPrepare_AVX512() {
    // Consume modification mask (no expansion needed - markChangeCorners already
    // marks all affected blocks during Phase 2)
    uint64_t modMask = wasModified;
    wasModified = 0;  // Clear for this generation's Phase 2

    // Clear changes array
    std::memset(changes, 0, sizeof(changes));

    // Reset row change mask for Phase 2 short-circuit optimization
    rowChangeMask = 0;

    // Nothing modified - nothing to scan
    if (modMask == 0) {
        return false;
    }

    // Note: activityRows is preserved for unscanned rows (rowMask == 0)
    // and updated only for rows we actually scan (rowMask != 0)

    // Constants for byte-level operations
    // Note: AVX-512 doesn't have byte-level shifts, so we use masks and comparisons
    const __m512i mask_left_alive = _mm512_set1_epi8(static_cast<char>(0x80));   // bit 7
    const __m512i mask_left_neighbors = _mm512_set1_epi8(0x70);  // bits 6-4
    const __m512i mask_right_alive = _mm512_set1_epi8(0x08);     // bit 3
    const __m512i mask_right_neighbors = _mm512_set1_epi8(0x07); // bits 2-0

    // For comparisons, we need normalized values (shifted to bits 2-0)
    // left_neighbors needs to be shifted from bits 6-4 to bits 2-0 (divide by 16)
    // We'll compare against shifted constants instead
    const __m512i two_in_high_nibble = _mm512_set1_epi8(0x20);   // 2 << 4
    const __m512i three_in_high_nibble = _mm512_set1_epi8(0x30); // 3 << 4
    const __m512i twos = _mm512_set1_epi8(2);
    const __m512i threes = _mm512_set1_epi8(3);
    const __m512i ones = _mm512_set1_epi8(1);

    // Process 64 bytes at a time (64 cell pairs, 8 uint64_t words = 1 block row)
    const uint8_t* cellBytes = reinterpret_cast<const uint8_t*>(cells);

    for (int byteOffset = 0; byteOffset < TILE_BYTES; byteOffset += 64) {
        // Check if this block row has any modifications
        int blockRow = byteOffset / 64;
        uint8_t rowMask = (modMask >> (blockRow * 8)) & 0xFF;
        if (rowMask == 0) {
            // Skip this block row - changes[] stays zero from memset
            // Preserve this row's activity bit - row content is unchanged
            continue;
        }

        // Clear this row's activity bit since we're scanning it
        // Will be set if we find any content
        activityRows &= ~(1 << blockRow);

        // Load 64 bytes (64 cell pairs)
        __m512i data = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(cellBytes + byteOffset));

        // Quick check: skip if all zero (no activity)
        __mmask64 nonZeroMask = _mm512_test_epi8_mask(data, data);
        if (nonZeroMask == 0) {
            continue;
        }

        // Mark this block row as having activity since we found non-zero cells
        activityRows |= (1 << blockRow);

        // Extract components using masks (no shifts needed for detection)
        // Left cell alive: bit 7 set
        __mmask64 left_alive_mask = _mm512_test_epi8_mask(data, mask_left_alive);

        // Right cell alive: bit 3 set
        __mmask64 right_alive_mask = _mm512_test_epi8_mask(data, mask_right_alive);

        // For neighbor counts, we need to compute:
        // left_true_count = left_neighbors + right_alive
        // right_true_count = right_neighbors + left_alive
        //
        // Since we can't easily shift bytes in AVX-512, we'll process byte-by-byte
        // for the final change calculation. But we can still use SIMD for the masks.

        // Extract the raw neighbor count fields
        __m512i left_neighbors_raw = _mm512_and_si512(data, mask_left_neighbors);  // bits 6-4
        __m512i right_neighbors_raw = _mm512_and_si512(data, mask_right_neighbors); // bits 2-0

        // For left cell: true_count = (left_neighbors_raw >> 4) + right_alive
        // We'll check the conditions by comparing against shifted values
        //
        // left survives if: alive AND ((neighbors + right_alive) == 2 OR 3)
        // left born if: !alive AND (neighbors + right_alive) == 3

        // Case analysis for left cell:
        // If right_alive = 0: compare left_neighbors_raw with 0x20 (2<<4) or 0x30 (3<<4)
        // If right_alive = 1: compare left_neighbors_raw with 0x10 (1<<4) or 0x20 (2<<4)

        // Compute masks for left cell survival/birth
        // When right is dead: survive if neighbors == 2 or 3
        __mmask64 left_eq2_when_right_dead = _mm512_cmpeq_epi8_mask(left_neighbors_raw, two_in_high_nibble);
        __mmask64 left_eq3_when_right_dead = _mm512_cmpeq_epi8_mask(left_neighbors_raw, three_in_high_nibble);

        // When right is alive: survive if neighbors == 1 or 2 (since total becomes 2 or 3)
        const __m512i one_in_high_nibble = _mm512_set1_epi8(0x10);
        __mmask64 left_eq1_when_right_alive = _mm512_cmpeq_epi8_mask(left_neighbors_raw, one_in_high_nibble);
        __mmask64 left_eq2_when_right_alive = _mm512_cmpeq_epi8_mask(left_neighbors_raw, two_in_high_nibble);

        // Left survives: (right dead AND (n==2 OR n==3)) OR (right alive AND (n==1 OR n==2))
        __mmask64 left_survive_when_right_dead = left_eq2_when_right_dead | left_eq3_when_right_dead;
        __mmask64 left_survive_when_right_alive = left_eq1_when_right_alive | left_eq2_when_right_alive;
        __mmask64 left_survive = (~right_alive_mask & left_survive_when_right_dead) |
                                  (right_alive_mask & left_survive_when_right_alive);

        // Left born: !alive AND total_neighbors == 3
        // When right dead: born if neighbors == 3
        // When right alive: born if neighbors == 2
        __mmask64 left_birth_when_right_dead = left_eq3_when_right_dead;
        __mmask64 left_birth_when_right_alive = left_eq2_when_right_alive;
        __mmask64 left_birth = (~right_alive_mask & left_birth_when_right_dead) |
                               (right_alive_mask & left_birth_when_right_alive);

        // Left changes: (alive AND !survive) OR (!alive AND birth)
        __mmask64 left_changes = (left_alive_mask & ~left_survive) | (~left_alive_mask & left_birth);

        // Similar for right cell
        // right_true_count = right_neighbors_raw + left_alive (where left_alive is 0 or 1)
        // When left is dead: compare right_neighbors_raw with 2 or 3
        // When left is alive: compare right_neighbors_raw with 1 or 2

        __mmask64 right_eq2_when_left_dead = _mm512_cmpeq_epi8_mask(right_neighbors_raw, twos);
        __mmask64 right_eq3_when_left_dead = _mm512_cmpeq_epi8_mask(right_neighbors_raw, threes);
        __mmask64 right_eq1_when_left_alive = _mm512_cmpeq_epi8_mask(right_neighbors_raw, ones);
        __mmask64 right_eq2_when_left_alive = _mm512_cmpeq_epi8_mask(right_neighbors_raw, twos);

        __mmask64 right_survive_when_left_dead = right_eq2_when_left_dead | right_eq3_when_left_dead;
        __mmask64 right_survive_when_left_alive = right_eq1_when_left_alive | right_eq2_when_left_alive;
        __mmask64 right_survive = (~left_alive_mask & right_survive_when_left_dead) |
                                   (left_alive_mask & right_survive_when_left_alive);

        __mmask64 right_birth_when_left_dead = right_eq3_when_left_dead;
        __mmask64 right_birth_when_left_alive = right_eq2_when_left_alive;
        __mmask64 right_birth = (~left_alive_mask & right_birth_when_left_dead) |
                                (left_alive_mask & right_birth_when_left_alive);

        __mmask64 right_changes = (right_alive_mask & ~right_survive) | (~right_alive_mask & right_birth);

        // Pack change bits into the changes array (32-bit per row format)
        // Each byte becomes 2 bits: bit 1 = left changes, bit 0 = right changes
        //
        // IMPORTANT: The scalar code processes bytes in big-endian order within each
        // 64-bit word (MSB first via >> 56, >> 48, etc.), but our mask bits are in
        // memory/little-endian order (byte 0 at bit 0).
        //
        // Within each 64-bit word:
        //   Scalar byte index: 0 (MSB) -> mask bit 7, 1 -> mask bit 6, ..., 7 (LSB) -> mask bit 0
        //   So for cells[i], scalar byte j (from >>56, >>48, etc.) = memory byte (7-j) = mask bit i*8 + (7-j)
        //
        // New 32-bit per row layout:
        //   Row N: bits 31-16 from left-half cell word, bits 15-0 from right-half cell word
        //
        // Each 64-byte AVX block covers 4 rows (64 bytes / 16 bytes per row)
        int baseRow = byteOffset / 16;  // 16 bytes = 1 row (32 cells, 2 bytes per cell pair... wait no, 1 byte per cell pair)

        // Actually: 64 bytes = 64 cell pairs = 4 rows (16 cell pairs per row)
        // So byteOffset/64 = block row, and each block has 4 cell rows
        // baseRow = (byteOffset / 64) * 4 = blockRow * 4
        int baseRowCorrected = blockRow * 4;

        for (int rowInBlock = 0; rowInBlock < 4; rowInBlock++) {
            uint32_t rowChanges = 0;
            // Each row has 16 bytes (16 cell pairs)
            int baseByteInBlock = rowInBlock * 16;

            // Process 16 bytes (2 cell words) for this row
            for (int cellWord = 0; cellWord < 2; cellWord++) {
                // Process 8 bytes of this cell word
                for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
                    // Scalar code processes from MSB to LSB (byte 7 down to byte 0 in memory)
                    int memByteInWord = 7 - byteInWord;
                    int maskBit = baseByteInBlock + cellWord * 8 + memByteInWord;

                    int leftChange = (left_changes >> maskBit) & 1;
                    int rightChange = (right_changes >> maskBit) & 1;
                    int changePair = (leftChange << 1) | rightChange;

                    // Position in 32-bit row word:
                    // cellWord 0 contributes to bits 31-16, cellWord 1 to bits 15-0
                    // Within each half, byte 0 (MSB) is at highest position
                    int shift = 30 - (cellWord * 16) - (byteInWord * 2);
                    rowChanges |= static_cast<uint32_t>(changePair) << shift;
                }
            }

            int row = baseRowCorrected + rowInBlock;
            changes[row] = rowChanges;
            if (rowChanges) rowChangeMask |= (1U << row);
        }
    }

    return rowChangeMask != 0;
}
#endif // VLIFE_AVX512_ENABLED

// Helper: Accumulate deltas to a local int8_t array (no carry propagation issues)
// x is the cell x-coordinate within the row (0-31)
// delta is the value to add (-2 to +2 typically)
// Note: Branchless - adding 0 is a no-op, avoids branch misprediction
static inline void accumulateToDeltaArray(int8_t* deltaArray, int x, int8_t delta) {
    deltaArray[x] += delta;
}

// Helper: Accumulate vertical deltas (4 cells: x-1, x, x+1, x+2) to buffer arrays
// baseX is the x coordinate of the right cell in the pair (even x)
// Handles wrap-around for cells outside 0-31 range (corner cases)
// Note: Removed delta==0 check; adding 0 is a no-op, avoids branch misprediction
static inline void accumulateVerticalDeltasToArrays(int8_t* deltaArray, int baseX, const int8_t* deltas,
                                                     int8_t* leftCornerDelta, int8_t* rightCornerDelta) {
    int leftX = baseX - 1;
    for (int i = 0; i < 4; i++) {
        int x = leftX + i;
        int8_t delta = deltas[i];

        if (x >= 0 && x < TILE_WIDTH) {
            // Cell is within this tile's row
            accumulateToDeltaArray(deltaArray, x, delta);
        } else if (x < 0 && leftCornerDelta != nullptr) {
            // Left boundary: accumulate to left corner (cell x=31 in neighbor)
            *leftCornerDelta += delta;
        } else if (x >= TILE_WIDTH && rightCornerDelta != nullptr) {
            // Right boundary: accumulate to right corner (cell x=0 in neighbor)
            *rightCornerDelta += delta;
        }
    }
}

template<bool UseAtomics>
void Tile::runGenerationChanges() {
    // Tile-level short-circuit: skip if Phase 1 detected no changes
    if (rowChangeMask == 0) {
        return;
    }

    // Queue self for next generation's Phase 1 (this tile will have modified cells)
    board->addToNextPhase1Queue(this);

    // Update the last modified timestamp for eviction tracking
    lastModifiedGeneration = board->getCurrentGenerationMod256();

    // Process the changes array to find cells that need to toggle.
    // Uses LUT-based optimization to handle cell pairs together.
    //
    // For each cell pair that has changes:
    //   1. Toggle the alive bits
    //   2. Update liveCount
    //   3. Look up deltas in deltaLUT based on change states
    //   4. Apply deltas to all affected neighbors

    const PackedDeltas* deltaLUT = board->deltaLUT;

    // Stack-local buffers for neighbor boundary deltas (simple int8_t arrays)
    // Using int8_t avoids carry propagation issues that occur with packed nibbles
    int8_t upNeighborDeltas[TILE_WIDTH] = {0};    // For up tile's row 31
    int8_t downNeighborDeltas[TILE_WIDTH] = {0};  // For down tile's row 0
    int8_t leftNeighborDeltas[TILE_HEIGHT] = {0};   // For left tile's column 31
    int8_t rightNeighborDeltas[TILE_HEIGHT] = {0};  // For right tile's column 0
    // Corner deltas for diagonal neighbors (single cell each)
    int8_t upLeftCornerDelta = 0;    // For up-left tile's cell (31, 31)
    int8_t upRightCornerDelta = 0;   // For up-right tile's cell (0, 31)
    int8_t downLeftCornerDelta = 0;  // For down-left tile's cell (31, 0)
    int8_t downRightCornerDelta = 0; // For down-right tile's cell (0, 0)
    bool hasUpDeltas = false;
    bool hasDownDeltas = false;
    bool hasLeftDeltas = false;
    bool hasRightDeltas = false;

    // Use rowChangeMask to jump directly to rows with changes
    uint32_t remainingRows = rowChangeMask;
    while (remainingRows != 0) {
        // Find lowest set bit (row with changes)
        int localY = __builtin_ctz(remainingRows);
        remainingRows &= remainingRows - 1;  // Clear the lowest set bit

        uint32_t changeBits = changes[localY];

        // Prefetch cell words for this row's cells
        // Each row has 2 cell words (cells 0-15 and 16-31)
        int baseCellWordIdx = localY * 2;
        if (baseCellWordIdx + 4 < TILE_64S) {
            PREFETCH(&cells[baseCellWordIdx + 2]);
            PREFETCH(&cells[baseCellWordIdx + 3]);
        }

        // Process only non-zero bit pairs using leading zero count to skip directly to them
        // This eliminates unpredictable branches from the inner loop
        while (changeBits != 0) {
            // Find the position of the highest set bit in 32-bit word
            int leadingZeros = __builtin_clz(changeBits);
            // Convert to bit pair index (0-15, where 0 is bits 31:30)
            int bitPair = leadingZeros / 2;
            int shiftAmount = 30 - (bitPair * 2);

            // Extract the 2-bit pair (guaranteed non-zero since we found a set bit)
            int pairChangeBits = (changeBits >> shiftAmount) & 0x3;

            // Clear this bit pair so we don't process it again
            changeBits &= ~(0x3U << shiftAmount);

            // Calculate which cell word and bit position
            // bitPair 0-7 are in cell word 0 (left half), 8-15 are in cell word 1 (right half)
            int cellWordInRow = bitPair / 8;  // 0 or 1
            int pairInWord = bitPair % 8;

            int currentCellIdx = localY * 2 + cellWordInRow;
            int leftCellBitPos = (7 - pairInWord) * 8 + 7;   // Left cell alive bit
            int rightCellBitPos = (7 - pairInWord) * 8 + 3;  // Right cell alive bit

            // Calculate base cell x coordinate (right cell, even x)
            // Due to MSB-first byte processing in Phase 1, the bit layout is reversed:
            // - bitPair 0 (bits 31-30) = x=14,15 (MSB byte of left half)
            // - bitPair 7 (bits 17-16) = x=0,1 (LSB byte of left half)
            // - bitPair 8 (bits 15-14) = x=30,31 (MSB byte of right half)
            // - bitPair 15 (bits 1-0) = x=16,17 (LSB byte of right half)
            // Branchless formula: baseX = 14 + (bitPair >> 3) * 32 - bitPair * 2
            int correction = (bitPair >> 3) * 32;  // 0 for left half, 32 for right half
            int baseX = 14 + correction - bitPair * 2;  // Right cell x (even)

            // Determine states for LUT index:
            // bits [3:2] = left cell: 00=unchanged, 01=became alive, 10=died
            // bits [1:0] = right cell: 00=unchanged, 01=became alive, 10=died
            //
            // Branchless computation:
            // - Extract change flags (0 or 1)
            // - Compute wasAlive from current cell state
            // - state = 1 + wasAlive (1=became alive, 2=died)
            // - delta = 1 - 2*wasAlive (+1 if born, -1 if died)
            // - Mask by change flag to zero out unchanged cells

            int leftChanged = (pairChangeBits >> 1) & 1;
            int rightChanged = pairChangeBits & 1;

            // Read current alive states (0 or 1)
            int leftWasAlive = (cells[currentCellIdx] >> leftCellBitPos) & 1;
            int rightWasAlive = (cells[currentCellIdx] >> rightCellBitPos) & 1;

            // Toggle alive bits for changed cells using XOR mask
            uint64_t toggleMask = (static_cast<uint64_t>(leftChanged) << leftCellBitPos) |
                                  (static_cast<uint64_t>(rightChanged) << rightCellBitPos);
            cells[currentCellIdx] ^= toggleMask;

            // Compute states branchlessly: state = (1 + wasAlive) if changed, 0 otherwise
            int leftState = (1 + leftWasAlive) * leftChanged;
            int rightState = (1 + rightWasAlive) * rightChanged;

            // Compute liveCount deltas branchlessly: +1 if born, -1 if died, 0 if unchanged
            // delta = (1 - 2*wasAlive) * changed = changed - 2*wasAlive*changed
            int leftDelta = leftChanged - 2 * leftWasAlive * leftChanged;
            int rightDelta = rightChanged - 2 * rightWasAlive * rightChanged;
            liveCount += leftDelta + rightDelta;

#ifdef VLIFE_METRICS_ENABLED
            // Track cells born (born = changed && !wasAlive => delta > 0)
            // Track cells died (died = changed && wasAlive => delta < 0)
            int leftBorn = leftChanged * (1 - leftWasAlive);   // 1 if born, 0 otherwise
            int leftDied = leftChanged * leftWasAlive;         // 1 if died, 0 otherwise
            int rightBorn = rightChanged * (1 - rightWasAlive);
            int rightDied = rightChanged * rightWasAlive;
            VLIFE_METRICS_INC_BORN(leftBorn + rightBorn);
            VLIFE_METRICS_INC_DIED(leftDied + rightDied);
#endif

            // Mark 4x4 block corners for next generation's Phase 1 skipping
            // Left cell is at (baseX+1, localY), right cell is at (baseX, localY)
            // Interior fast path uses compact precomputed LUT (512 bytes vs 8KB)
            // Interior: all corner coordinates x-1, x+1, y-1, y+1 stay within [0, TILE_WIDTH-1/TILE_HEIGHT-1]
            // For cell pair at baseX: right cell at baseX needs x in [1,30], left cell at baseX+1 needs x in [0,30]
            // Combined: baseX >= 2 (so baseX-1 >= 1) and baseX <= 28 (so baseX+2 <= 30) and y in [1,30]
            bool isInterior = (baseX >= 2 && baseX <= 28 && localY >= 1 && localY <= 30);
            if (isInterior) {
                // Compact LUT fast path: encodes y-symmetry and change state in index
                // Single indexed lookup with no conditionals, then shift to correct block-row
                int yClass = localY & 3;
                int blockRow = localY >> 2;
                int changeState = (leftChanged << 1) | rightChanged;

                int lutIdx = (yClass << 6) | ((baseX >> 1) << 2) | changeState;
                const CompactCornerMask& mask = board->compactCornerMaskLUT[lutIdx];

                // Compute block-row positions for upper (y-1) and lower (y+1) corners
                // yClass 0: upper is in blockRow-1, lower in blockRow
                // yClass 3: upper is in blockRow, lower in blockRow+1
                // yClass 1,2: both in same blockRow
                int upperRow = blockRow - (yClass == 0);
                int lowerRow = blockRow + (yClass == 3);

                // Build 64-bit mask by shifting x-block masks to correct block-row positions
                wasModified |= (static_cast<uint64_t>(mask.upper) << (upperRow * 8))
                             | (static_cast<uint64_t>(mask.lower) << (lowerRow * 8));
            } else {
                // Slow path for boundary cells (~3%) - call the function
                if (leftChanged) markChangeCorners(baseX + 1, localY);
                if (rightChanged) markChangeCorners(baseX, localY);
            }

            // Build LUT index and apply deltas
            int lutIndex = (leftState << 2) | rightState;
            const PackedDeltas& deltas = deltaLUT[lutIndex];

            // Check if this cell pair is on a cross-tile boundary
            bool isTopRow = (localY == 0);
            bool isBottomRow = (localY == TILE_HEIGHT - 1);
            bool isLeftEdge = (baseX == 0);  // Cell pair at left edge (baseX=0 means x-1 is cross-tile)
            bool isRightEdge = (baseX == TILE_WIDTH - 2);  // Cell pair at right edge (baseX+2 is cross-tile)

            // Buffered path: accumulate cross-tile deltas locally
            // Buffer left/right same-row deltas for all rows
            // Buffer up/down vertical deltas only for boundary rows

            // Apply same-row horizontal neighbors (x-1 and x+2)
            int leftNeighborX = baseX - 1;
            int rightNeighborX = baseX + 2;

            if (deltas.sameRow[0] != 0) {
                if (leftNeighborX >= 0) {
                    applyDelta(leftNeighborX, localY, deltas.sameRow[0]);
                } else {
                    // Buffer for later flush to left tile's column 31
                    leftNeighborDeltas[localY] += deltas.sameRow[0];
                    hasLeftDeltas = true;
                }
            }

            if (deltas.sameRow[1] != 0) {
                if (rightNeighborX < TILE_WIDTH) {
                    applyDelta(rightNeighborX, localY, deltas.sameRow[1]);
                } else {
                    // Buffer for later flush to right tile's column 0
                    rightNeighborDeltas[localY] += deltas.sameRow[1];
                    hasRightDeltas = true;
                }
            }

            // Handle vertical deltas for row above (y-1)
            if (isTopRow) {
                // Cross-tile to up neighbor's row 31: buffer it
                // Also handle corner cases for up-left and up-right tiles
                int8_t* leftCorner = isLeftEdge ? &upLeftCornerDelta : nullptr;
                int8_t* rightCorner = isRightEdge ? &upRightCornerDelta : nullptr;
                accumulateVerticalDeltasToArrays(upNeighborDeltas, baseX, deltas.verticalRow,
                                                  leftCorner, rightCorner);
                hasUpDeltas = true;
            } else {
                // Within-tile row above
                applyVerticalDeltas(baseX, localY - 1, deltas.verticalRow);
            }

            // Handle vertical deltas for row below (y+1)
            if (isBottomRow) {
                // Cross-tile to down neighbor's row 0: buffer it
                int8_t* leftCorner = isLeftEdge ? &downLeftCornerDelta : nullptr;
                int8_t* rightCorner = isRightEdge ? &downRightCornerDelta : nullptr;
                accumulateVerticalDeltasToArrays(downNeighborDeltas, baseX, deltas.verticalRow,
                                                  leftCorner, rightCorner);
                hasDownDeltas = true;
            } else {
                // Within-tile row below
                applyVerticalDeltas(baseX, localY + 1, deltas.verticalRow);
            }
        }
    }

    // Flush buffered deltas to neighbor tiles
    if (hasUpDeltas) {
        Tile* upTile = ensureUpTile();
        VLIFE_METRICS_INC_BOUNDARY();
        if constexpr (UseAtomics) {
            upTile->atomicAddBoundaryDeltas(TILE_HEIGHT - 1, upNeighborDeltas);
        } else {
            upTile->nonAtomicAddBoundaryDeltas(TILE_HEIGHT - 1, upNeighborDeltas);
        }
        // Queue neighbor for next Phase 1 (skip if already in Phase 2)
        if (!upTile->willSelfQueueForPhase1() && upTile->tryQueueForPhase1()) {
            board->addToNextPhase1Queue(upTile);
        }

        // Handle up-left corner using the upTile we already have
        if (upLeftCornerDelta != 0) {
            Tile* upLeftTile = upTile->ensureLeftTile();
            VLIFE_METRICS_INC_BOUNDARY();
            if constexpr (UseAtomics) {
                upLeftTile->atomicApplyDelta(TILE_WIDTH - 1, TILE_HEIGHT - 1, upLeftCornerDelta);
            } else {
                upLeftTile->nonAtomicApplyDelta(TILE_WIDTH - 1, TILE_HEIGHT - 1, upLeftCornerDelta);
            }
            if (!upLeftTile->willSelfQueueForPhase1() && upLeftTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(upLeftTile);
            }
        }
        // Handle up-right corner using the upTile we already have
        if (upRightCornerDelta != 0) {
            Tile* upRightTile = upTile->ensureRightTile();
            VLIFE_METRICS_INC_BOUNDARY();
            if constexpr (UseAtomics) {
                upRightTile->atomicApplyDelta(0, TILE_HEIGHT - 1, upRightCornerDelta);
            } else {
                upRightTile->nonAtomicApplyDelta(0, TILE_HEIGHT - 1, upRightCornerDelta);
            }
            if (!upRightTile->willSelfQueueForPhase1() && upRightTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(upRightTile);
            }
        }
    }
    if (hasDownDeltas) {
        Tile* downTile = ensureDownTile();
        VLIFE_METRICS_INC_BOUNDARY();
        if constexpr (UseAtomics) {
            downTile->atomicAddBoundaryDeltas(0, downNeighborDeltas);
        } else {
            downTile->nonAtomicAddBoundaryDeltas(0, downNeighborDeltas);
        }
        // Queue neighbor for next Phase 1 (skip if already in Phase 2)
        if (!downTile->willSelfQueueForPhase1() && downTile->tryQueueForPhase1()) {
            board->addToNextPhase1Queue(downTile);
        }

        // Handle down-left corner using the downTile we already have
        if (downLeftCornerDelta != 0) {
            Tile* downLeftTile = downTile->ensureLeftTile();
            VLIFE_METRICS_INC_BOUNDARY();
            if constexpr (UseAtomics) {
                downLeftTile->atomicApplyDelta(TILE_WIDTH - 1, 0, downLeftCornerDelta);
            } else {
                downLeftTile->nonAtomicApplyDelta(TILE_WIDTH - 1, 0, downLeftCornerDelta);
            }
            if (!downLeftTile->willSelfQueueForPhase1() && downLeftTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(downLeftTile);
            }
        }
        // Handle down-right corner using the downTile we already have
        if (downRightCornerDelta != 0) {
            Tile* downRightTile = downTile->ensureRightTile();
            VLIFE_METRICS_INC_BOUNDARY();
            if constexpr (UseAtomics) {
                downRightTile->atomicApplyDelta(0, 0, downRightCornerDelta);
            } else {
                downRightTile->nonAtomicApplyDelta(0, 0, downRightCornerDelta);
            }
            if (!downRightTile->willSelfQueueForPhase1() && downRightTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(downRightTile);
            }
        }
    }
    if (hasLeftDeltas) {
        Tile* leftTile = ensureLeftTile();
        VLIFE_METRICS_INC_BOUNDARY();
        if constexpr (UseAtomics) {
            leftTile->atomicAddColumnDeltas(TILE_WIDTH - 1, leftNeighborDeltas);
        } else {
            leftTile->nonAtomicAddColumnDeltas(TILE_WIDTH - 1, leftNeighborDeltas);
        }
        // Queue neighbor for next Phase 1 (skip if already in Phase 2)
        if (!leftTile->willSelfQueueForPhase1() && leftTile->tryQueueForPhase1()) {
            board->addToNextPhase1Queue(leftTile);
        }
    }
    if (hasRightDeltas) {
        Tile* rightTile = ensureRightTile();
        VLIFE_METRICS_INC_BOUNDARY();
        if constexpr (UseAtomics) {
            rightTile->atomicAddColumnDeltas(0, rightNeighborDeltas);
        } else {
            rightTile->nonAtomicAddColumnDeltas(0, rightNeighborDeltas);
        }
        // Queue neighbor for next Phase 1 (skip if already in Phase 2)
        if (!rightTile->willSelfQueueForPhase1() && rightTile->tryQueueForPhase1()) {
            board->addToNextPhase1Queue(rightTile);
        }
    }
}

// Explicit template instantiations
template void Tile::runGenerationChanges<true>();
template void Tile::runGenerationChanges<false>();

// Apply a single delta to a cell's neighbor count
// Uses atomic for boundary cells (could race with neighbor tiles in full parallel mode)
inline void Tile::applyDelta(int x, int y, int8_t delta) {
    if (delta == 0) return;

    // Use atomic for boundary cells (could race with neighbor tile updates)
    // Boundary cells are those on the edge of the tile that neighbor tiles may also update
    if (isBoundaryCell(x, y)) {
        atomicApplyDelta(x, y, delta);
        return;
    }

    // Non-atomic path for interior cells (no race possible - only this tile updates them)
    uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
    uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

    // Extract the current neighbor count (bits 0-2)
    uint64_t mask = 0x7ULL << bitPos;
    uint64_t currentCount = (cells[cellIdx] & mask) >> bitPos;

    // Apply delta
    int newCount = currentCount + delta;

    // Update the neighbor count (mask to 3 bits to prevent corruption from negative values)
    cells[cellIdx] = (cells[cellIdx] & ~mask) | (static_cast<uint64_t>(newCount & 0x7) << bitPos);
}

// Apply vertical deltas to 4 consecutive cells in a row (x-1, x, x+1, x+2)
void Tile::applyVerticalDeltas(int baseX, int y, const int8_t* deltas) {
    // Apply deltas to 4 cells: (baseX-1, y), (baseX, y), (baseX+1, y), (baseX+2, y)
    int leftX = baseX - 1;
    int rightX = baseX + 2;

    // Check if this is a boundary row (y==0 or y==TILE_HEIGHT-1)
    // Boundary rows could be updated by neighbor tiles, so need atomic operations
    bool isBoundaryRow = (y == 0 || y == TILE_HEIGHT - 1);

    // Fast path: all 4 cells in same 64-bit word, not crossing tile boundaries,
    // AND not a boundary row (to avoid race with neighbor tiles)
    // Row layout: cells 0-15 in word 0, cells 16-31 in word 1
    // Interior cases (75% of cell pairs):
    //   Word 0: leftX >= 0 && rightX < 16  (baseX in {2,4,6,8,10,12})
    //   Word 1: leftX >= 16 && rightX < 32 (baseX in {18,20,22,24,26,28})
    bool inWord0 = (leftX >= 0 && rightX < 16);
    bool inWord1 = (leftX >= 16 && rightX < TILE_WIDTH);

    if (!isBoundaryRow && (inWord0 || inWord1)) {
        // Bulk operation: load once, update 4 nibbles, store once
        // Safe because interior rows can only be updated by this tile
        uint32_t cellIdx = (y * TILE_WIDTH + leftX) / 16;
        uint32_t baseBitPos = (leftX % 16) * 4;

        uint64_t word = cells[cellIdx];

        // Branchless unrolled version: always update all 4 nibbles
        // Adding 0 is a no-op, so we avoid branch mispredictions
        uint64_t m0 = 0x7ULL << baseBitPos;
        uint64_t m1 = 0x7ULL << (baseBitPos + 4);
        uint64_t m2 = 0x7ULL << (baseBitPos + 8);
        uint64_t m3 = 0x7ULL << (baseBitPos + 12);

        int c0 = static_cast<int>((word & m0) >> baseBitPos) + deltas[0];
        int c1 = static_cast<int>((word & m1) >> (baseBitPos + 4)) + deltas[1];
        int c2 = static_cast<int>((word & m2) >> (baseBitPos + 8)) + deltas[2];
        int c3 = static_cast<int>((word & m3) >> (baseBitPos + 12)) + deltas[3];

        // Clear all 4 nibbles and set new values (masked to 3 bits each)
        word = (word & ~(m0 | m1 | m2 | m3)) |
               (static_cast<uint64_t>(c0 & 0x7) << baseBitPos) |
               (static_cast<uint64_t>(c1 & 0x7) << (baseBitPos + 4)) |
               (static_cast<uint64_t>(c2 & 0x7) << (baseBitPos + 8)) |
               (static_cast<uint64_t>(c3 & 0x7) << (baseBitPos + 12));

        cells[cellIdx] = word;
    } else {
        // Slow path: handle boundaries with batched tile lookups
        // Classify boundary crossings once
        bool crossesLeft = (leftX < 0);
        bool crossesRight = (rightX >= TILE_WIDTH);

        // Handle left tile boundary first (single lookup, single queue check)
        if (crossesLeft && deltas[0] != 0) {
            Tile* leftTile = ensureLeftTile();
            VLIFE_METRICS_INC_BOUNDARY();
            leftTile->atomicApplyDelta(TILE_WIDTH - 1, y, deltas[0]);
            if (!leftTile->willSelfQueueForPhase1() && leftTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(leftTile);
            }
        }

        // Interior cells - use applyDelta for boundary row handling
        int startIdx = crossesLeft ? 1 : 0;
        int endIdx = crossesRight ? 3 : 4;
        for (int i = startIdx; i < endIdx; i++) {
            if (deltas[i] != 0) {
                applyDelta(leftX + i, y, deltas[i]);
            }
        }

        // Handle right tile boundary (single lookup, single queue check)
        if (crossesRight && deltas[3] != 0) {
            Tile* rightTile = ensureRightTile();
            VLIFE_METRICS_INC_BOUNDARY();
            rightTile->atomicApplyDelta(0, y, deltas[3]);
            if (!rightTile->willSelfQueueForPhase1() && rightTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(rightTile);
            }
        }
    }
}

// Atomically apply a single delta to a cell's neighbor count
// Uses compare-and-swap to safely update when multiple threads access the same tile
void Tile::atomicApplyDelta(int x, int y, int8_t delta) {
    if (delta == 0) return;

    VLIFE_METRICS_INC_ATOMIC();

    uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
    uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

    uint64_t mask = 0x7ULL << bitPos;

    // Compare-and-swap loop for thread-safe update
    uint64_t oldVal, newVal;
    do {
        oldVal = __atomic_load_n(&cells[cellIdx], __ATOMIC_RELAXED);

        // Extract current neighbor count (bits 0-2)
        int count = (oldVal & mask) >> bitPos;

        // Apply delta (mask to 3 bits to prevent corruption from negative values)
        count += delta;

        // Compute new value
        newVal = (oldVal & ~mask) | (static_cast<uint64_t>(count & 0x7) << bitPos);

    } while (!__atomic_compare_exchange_n(&cells[cellIdx], &oldVal, newVal,
                                           false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

// Atomically apply vertical deltas to 4 consecutive cells in a row
void Tile::atomicApplyVerticalDeltas(int baseX, int y, const int8_t* deltas) {
    // Apply deltas to 4 cells: (baseX-1, y), (baseX, y), (baseX+1, y), (baseX+2, y)
    int leftX = baseX - 1;

    // For cross-tile calls, we need to handle boundaries and use atomic operations
    // for cells that are in this tile, and call atomicApplyDelta on neighbor tiles
    for (int i = 0; i < 4; i++) {
        int x = leftX + i;
        int8_t delta = deltas[i];

        if (delta == 0) continue;

        if (x >= 0 && x < TILE_WIDTH) {
            // Cell is in this tile - use atomic update
            atomicApplyDelta(x, y, delta);
        } else if (x < 0) {
            // Left tile boundary - create tile if needed
            Tile* leftTile = ensureLeftTile();
            leftTile->atomicApplyDelta(TILE_WIDTH - 1, y, delta);
            // Queue neighbor for next Phase 1 (skip if already in Phase 2)
            if (!leftTile->willSelfQueueForPhase1() && leftTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(leftTile);
            }
        } else {
            // Right tile boundary - create tile if needed
            Tile* rightTile = ensureRightTile();
            rightTile->atomicApplyDelta(0, y, delta);
            // Queue neighbor for next Phase 1 (skip if already in Phase 2)
            if (!rightTile->willSelfQueueForPhase1() && rightTile->tryQueueForPhase1()) {
                board->addToNextPhase1Queue(rightTile);
            }
        }
    }
}

// Apply buffered boundary deltas from an int8_t array
// This applies each non-zero delta with atomicApplyDelta
void Tile::atomicAddBoundaryDeltas(int y, const int8_t* deltaArray) {
    for (int x = 0; x < TILE_WIDTH; x++) {
        int8_t delta = deltaArray[x];
        if (delta != 0) {
            atomicApplyDelta(x, y, delta);
        }
    }
}

// Non-atomic version of applyDelta for sequential execution
// Same logic as atomicApplyDelta but uses direct memory access instead of CAS loops
inline void Tile::nonAtomicApplyDelta(int x, int y, int8_t delta) {
    if (delta == 0) return;

    uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
    uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

    uint64_t mask = 0x7ULL << bitPos;

    // Direct read-modify-write (safe in sequential mode, no concurrent access)
    uint64_t oldVal = cells[cellIdx];
    int count = (oldVal & mask) >> bitPos;
    count += delta;
    cells[cellIdx] = (oldVal & ~mask) | (static_cast<uint64_t>(count & 0x7) << bitPos);
}

// Non-atomic version of atomicAddBoundaryDeltas for sequential execution
void Tile::nonAtomicAddBoundaryDeltas(int y, const int8_t* deltaArray) {
    for (int x = 0; x < TILE_WIDTH; x++) {
        int8_t delta = deltaArray[x];
        if (delta != 0) {
            nonAtomicApplyDelta(x, y, delta);
        }
    }
}

// Apply buffered column deltas from an int8_t array (atomic version)
// This applies each non-zero delta with atomicApplyDelta
void Tile::atomicAddColumnDeltas(int x, const int8_t* deltaArray) {
    for (int y = 0; y < TILE_HEIGHT; y++) {
        int8_t delta = deltaArray[y];
        if (delta != 0) {
            atomicApplyDelta(x, y, delta);
        }
    }
}

// Non-atomic version of atomicAddColumnDeltas for sequential execution
void Tile::nonAtomicAddColumnDeltas(int x, const int8_t* deltaArray) {
    for (int y = 0; y < TILE_HEIGHT; y++) {
        int8_t delta = deltaArray[y];
        if (delta != 0) {
            nonAtomicApplyDelta(x, y, delta);
        }
    }
}

