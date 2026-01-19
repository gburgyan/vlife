//
// Created by George Burgyan on 3/1/25.
//

#include "Tile.h"
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

Tile::Tile(VLife *board, int32_t tileX, int32_t tileY) :
    board(board), tileX(tileX), tileY(tileY), left(nullptr), right(nullptr), up(nullptr), down(nullptr),
    upLeft(nullptr), upRight(nullptr), downLeft(nullptr), downRight(nullptr), liveCount(0), activityMask(0) {
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
    } else if (dx == -1 && dy == -1) {
        upLeft = tile;
        if (tile) tile->downRight = this;
    } else if (dx == 1 && dy == -1) {
        upRight = tile;
        if (tile) tile->downLeft = this;
    } else if (dx == -1 && dy == 1) {
        downLeft = tile;
        if (tile) tile->upRight = this;
    } else if (dx == 1 && dy == 1) {
        downRight = tile;
        if (tile) tile->upLeft = this;
    }
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
            } else if (tileOffsetX == 1 && tileOffsetY == 0) {
                adjTile = right;
                if (!adjTile) {
                    adjTile = board->getTile(tileX + 1, tileY);
                    right = adjTile;
                }
            } else if (tileOffsetX == 0 && tileOffsetY == -1) {
                adjTile = up;
                if (!adjTile) {
                    adjTile = board->getTile(tileX, tileY - 1);
                    up = adjTile;
                }
            } else if (tileOffsetX == 0 && tileOffsetY == 1) {
                adjTile = down;
                if (!adjTile) {
                    adjTile = board->getTile(tileX, tileY + 1);
                    down = adjTile;
                }
            } else if (tileOffsetX == -1 && tileOffsetY == -1) {
                adjTile = upLeft;
                if (!adjTile) {
                    adjTile = board->getTile(tileX - 1, tileY - 1);
                    upLeft = adjTile;
                }
            } else if (tileOffsetX == 1 && tileOffsetY == -1) {
                adjTile = upRight;
                if (!adjTile) {
                    adjTile = board->getTile(tileX + 1, tileY - 1);
                    upRight = adjTile;
                }
            } else if (tileOffsetX == -1 && tileOffsetY == 1) {
                adjTile = downLeft;
                if (!adjTile) {
                    adjTile = board->getTile(tileX - 1, tileY + 1);
                    downLeft = adjTile;
                }
            } else if (tileOffsetX == 1 && tileOffsetY == 1) {
                adjTile = downRight;
                if (!adjTile) {
                    adjTile = board->getTile(tileX + 1, tileY + 1);
                    downRight = adjTile;
                }
            }

            adjTile->updateNeighborCount(adjLocalX, adjLocalY, alive);
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
}

void Tile::runGenerationPrepare() {
    // Clear changes array
    std::memset(changes, 0, sizeof(changes));

    // Rebuild activity mask while processing
    // This allows us to skip dead regions efficiently
    activityMask = 0;

    auto ruleLUT = board->ruleLUT;

#ifdef USE_NEON_SIMD
    // Optimized ARM64 implementation with improved prefetch and word-level skipping
    // Uses scalar LUT lookups (cache-efficient) with direct result accumulation
    const uint8_t* __restrict ruleLUT_u8 = reinterpret_cast<const uint8_t*>(ruleLUT);

    for (int i = 0; i < TILE_64S; i += 4) {
        // Prefetch 2 iterations ahead for better memory pipeline utilization
        if (i + 8 < TILE_64S) {
            PREFETCH(&cells[i + 8]);
        }

        // Quick check: skip if all 4 words are zero
        uint64_t combined = cells[i] | cells[i + 1] | cells[i + 2] | cells[i + 3];
        if (__builtin_expect(combined == 0, 0)) {
            continue;
        }

        // Update activity mask for non-zero words
        if (cells[i] != 0) activityMask |= (1ULL << i);
        if (cells[i + 1] != 0) activityMask |= (1ULL << (i + 1));
        if (cells[i + 2] != 0) activityMask |= (1ULL << (i + 2));
        if (cells[i + 3] != 0) activityMask |= (1ULL << (i + 3));

        uint64_t changeBuff = 0;

        // Process word 0 - only if non-zero
        uint64_t slice = cells[i];
        if (slice != 0) {
            // Pack 8 LUT results directly into 16 bits (bits 63:48)
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

        changes[i >> 2] = changeBuff;
    }

#else
    // Scalar fallback for non-ARM platforms
    for (int i = 0; i < TILE_64S; i += 4) {
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

        // Update activity mask for non-zero words
        if (cells[i] != 0) activityMask |= (1ULL << i);
        if (cells[i + 1] != 0) activityMask |= (1ULL << (i + 1));
        if (cells[i + 2] != 0) activityMask |= (1ULL << (i + 2));
        if (cells[i + 3] != 0) activityMask |= (1ULL << (i + 3));

        // Process word 0
        uint64_t changeBuff = 0;
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

        changes[i >> 2] = changeBuff;
    }
#endif
}

void Tile::runGenerationChanges() {
    // Process the changes array to find cells that need to toggle.
    // Uses LUT-based optimization to handle cell pairs together.
    //
    // For each cell pair that has changes:
    //   1. Toggle the alive bits
    //   2. Update liveCount
    //   3. Look up deltas in deltaLUT based on change states
    //   4. Apply deltas to all affected neighbors

    const PackedDeltas* deltaLUT = board->deltaLUT;

    for (int changeIdx = 0; changeIdx < TILE_CHANGE_64S; changeIdx++) {
        uint64_t changeBits = changes[changeIdx];

        if (changeBits == 0) {
            continue;
        }

        // Process 64 change bits (32 cell pairs, 2 bits per pair)
        for (int bitPair = 0; bitPair < 32; bitPair++) {
            // Extract 2 change bits for this cell pair (bit 1 = left changed, bit 0 = right changed)
            int shiftAmount = 62 - (bitPair * 2);
            int pairChangeBits = (changeBits >> shiftAmount) & 0x3;

            if (pairChangeBits == 0) {
                continue;
            }

            // Calculate which cell word and bit position
            int wordsFromStart = bitPair / 8;  // 8 cell pairs per 64-bit word
            int pairInWord = bitPair % 8;

            int currentCellIdx = (changeIdx * 4) + wordsFromStart;
            int leftCellBitPos = (7 - pairInWord) * 8 + 7;   // Left cell alive bit
            int rightCellBitPos = (7 - pairInWord) * 8 + 3;  // Right cell alive bit

            // Calculate base cell index (right cell, even x)
            int baseCellInTile = currentCellIdx * 16 + (7 - pairInWord) * 2;
            int baseX = baseCellInTile % TILE_WIDTH;  // Right cell x (even)
            int localY = baseCellInTile / TILE_WIDTH;

            // Determine states for LUT index:
            // bits [3:2] = left cell: 00=unchanged, 01=became alive, 10=died
            // bits [1:0] = right cell: 00=unchanged, 01=became alive, 10=died
            int leftState = 0;
            int rightState = 0;

            // Process left cell (odd x = baseX + 1)
            if (pairChangeBits & 0x2) {
                bool wasAlive = (cells[currentCellIdx] & (1ULL << leftCellBitPos)) != 0;
                bool becameAlive = !wasAlive;

                // Toggle the alive bit
                cells[currentCellIdx] ^= (1ULL << leftCellBitPos);

                // Update live count
                if (becameAlive) {
                    liveCount++;
                    leftState = 1;  // became alive
                } else {
                    liveCount--;
                    leftState = 2;  // died
                }
            }

            // Process right cell (even x = baseX)
            if (pairChangeBits & 0x1) {
                bool wasAlive = (cells[currentCellIdx] & (1ULL << rightCellBitPos)) != 0;
                bool becameAlive = !wasAlive;

                // Toggle the alive bit
                cells[currentCellIdx] ^= (1ULL << rightCellBitPos);

                // Update live count
                if (becameAlive) {
                    liveCount++;
                    rightState = 1;  // became alive
                } else {
                    liveCount--;
                    rightState = 2;  // died
                }
            }

            // Build LUT index and apply deltas
            int lutIndex = (leftState << 2) | rightState;
            applyDeltasForCellPair(baseX, localY, deltaLUT[lutIndex]);
        }
    }
}

// Apply a single delta to a cell's neighbor count
inline void Tile::applyDelta(int x, int y, int8_t delta) {
    if (delta == 0) return;

    uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
    uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

    // Extract the current neighbor count (bits 0-2)
    uint64_t mask = 0x7ULL << bitPos;
    uint64_t currentCount = (cells[cellIdx] & mask) >> bitPos;

    // Apply delta
    int newCount = currentCount + delta;

    // Update the neighbor count (mask to 3 bits to prevent corruption from negative values)
    cells[cellIdx] = (cells[cellIdx] & ~mask) | (static_cast<uint64_t>(newCount & 0x7) << bitPos);

    // Mark this word as active (neighbor count change means potential birth)
    activityMask |= (1ULL << cellIdx);
}

// Apply vertical deltas to 4 consecutive cells in a row (x-1, x, x+1, x+2)
void Tile::applyVerticalDeltas(int baseX, int y, const int8_t* deltas) {
    // Apply deltas to 4 cells: (baseX-1, y), (baseX, y), (baseX+1, y), (baseX+2, y)
    int leftX = baseX - 1;
    int rightX = baseX + 2;

    // Fast path: all 4 cells in same 64-bit word, not crossing tile boundaries
    // Row layout: cells 0-15 in word 0, cells 16-31 in word 1
    // Interior cases (75% of cell pairs):
    //   Word 0: leftX >= 0 && rightX < 16  (baseX in {2,4,6,8,10,12})
    //   Word 1: leftX >= 16 && rightX < 32 (baseX in {18,20,22,24,26,28})
    bool inWord0 = (leftX >= 0 && rightX < 16);
    bool inWord1 = (leftX >= 16 && rightX < TILE_WIDTH);

    if (inWord0 || inWord1) {
        // Bulk operation: load once, update 4 nibbles, store once
        uint32_t cellIdx = (y * TILE_WIDTH + leftX) / 16;
        uint32_t baseBitPos = (leftX % 16) * 4;

        uint64_t word = cells[cellIdx];

        // Extract, add, clamp, and repack 4 nibbles
        for (int i = 0; i < 4; i++) {
            int8_t delta = deltas[i];
            if (delta == 0) continue;

            uint32_t bitPos = baseBitPos + i * 4;
            uint64_t mask = 0x7ULL << bitPos;
            int count = (word & mask) >> bitPos;

            // Apply delta (mask to 3 bits to prevent corruption from negative values)
            count += delta;

            word = (word & ~mask) | (static_cast<uint64_t>(count & 0x7) << bitPos);
        }

        cells[cellIdx] = word;
        activityMask |= (1ULL << cellIdx);
    } else {
        // Slow path: handle boundaries individually
        for (int i = 0; i < 4; i++) {
            int x = leftX + i;
            int8_t delta = deltas[i];

            if (delta == 0) continue;

            // Check bounds and handle accordingly
            if (x >= 0 && x < TILE_WIDTH) {
                applyDelta(x, y, delta);
            } else if (x < 0) {
                // Left tile boundary - create tile if needed, use atomic for safety
                Tile* leftTile = ensureNeighborTile(-1, 0);
                if (leftTile) {
                    leftTile->atomicApplyDelta(TILE_WIDTH - 1, y, delta);
                }
            } else {
                // Right tile boundary - create tile if needed, use atomic for safety
                Tile* rightTile = ensureNeighborTile(1, 0);
                if (rightTile) {
                    rightTile->atomicApplyDelta(0, y, delta);
                }
            }
        }
    }
}

// Atomically apply a single delta to a cell's neighbor count
// Uses compare-and-swap to safely update when multiple threads access the same tile
void Tile::atomicApplyDelta(int x, int y, int8_t delta) {
    if (delta == 0) return;

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

    // Atomically update activity mask
    __atomic_fetch_or(&activityMask, 1ULL << cellIdx, __ATOMIC_RELAXED);
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
            Tile* leftTile = ensureNeighborTile(-1, 0);
            if (leftTile) {
                leftTile->atomicApplyDelta(TILE_WIDTH - 1, y, delta);
            }
        } else {
            // Right tile boundary - create tile if needed
            Tile* rightTile = ensureNeighborTile(1, 0);
            if (rightTile) {
                rightTile->atomicApplyDelta(0, y, delta);
            }
        }
    }
}

// Helper to ensure a neighbor tile exists and return it
// Creates the tile on demand if it doesn't exist
Tile* Tile::ensureNeighborTile(int dx, int dy) {
    Tile** neighbor;
    if (dx == -1 && dy == 0) neighbor = &left;
    else if (dx == 1 && dy == 0) neighbor = &right;
    else if (dx == 0 && dy == -1) neighbor = &up;
    else if (dx == 0 && dy == 1) neighbor = &down;
    else if (dx == -1 && dy == -1) neighbor = &upLeft;
    else if (dx == 1 && dy == -1) neighbor = &upRight;
    else if (dx == -1 && dy == 1) neighbor = &downLeft;
    else if (dx == 1 && dy == 1) neighbor = &downRight;
    else return nullptr;

    if (*neighbor == nullptr) {
        *neighbor = board->getTile(tileX + dx, tileY + dy);
    }
    return *neighbor;
}

// Apply deltas for a cell pair using LUT
void Tile::applyDeltasForCellPair(int baseX, int localY, const PackedDeltas& deltas) {
    // baseX is the x coordinate of the right cell (even x)
    // The left cell is at baseX+1 (odd x)

    // Apply same-row horizontal neighbors: x-1 and x+2
    // sameRow[0] is for x-1 (only affected by right cell)
    // sameRow[1] is for x+2 (only affected by left cell)

    int leftNeighborX = baseX - 1;
    int rightNeighborX = baseX + 2;

    // Handle x-1 neighbor (same row)
    if (deltas.sameRow[0] != 0) {
        if (leftNeighborX >= 0) {
            applyDelta(leftNeighborX, localY, deltas.sameRow[0]);
        } else {
            // Cross-tile: create tile if needed and use atomic for thread safety
            Tile* leftTile = ensureNeighborTile(-1, 0);
            if (leftTile) {
                leftTile->atomicApplyDelta(TILE_WIDTH - 1, localY, deltas.sameRow[0]);
            }
        }
    }

    // Handle x+2 neighbor (same row)
    if (deltas.sameRow[1] != 0) {
        if (rightNeighborX < TILE_WIDTH) {
            applyDelta(rightNeighborX, localY, deltas.sameRow[1]);
        } else {
            // Cross-tile: create tile if needed and use atomic for thread safety
            Tile* rightTile = ensureNeighborTile(1, 0);
            if (rightTile) {
                rightTile->atomicApplyDelta(0, localY, deltas.sameRow[1]);
            }
        }
    }

    // Apply vertical deltas for row above (y-1)
    if (localY > 0) {
        applyVerticalDeltas(baseX, localY - 1, deltas.verticalRow);
    } else {
        // Cross-tile: create tile if needed and use atomic for thread safety
        Tile* upTile = ensureNeighborTile(0, -1);
        if (upTile) {
            upTile->atomicApplyVerticalDeltas(baseX, TILE_HEIGHT - 1, deltas.verticalRow);
        }
    }

    // Apply vertical deltas for row below (y+1)
    if (localY < TILE_HEIGHT - 1) {
        applyVerticalDeltas(baseX, localY + 1, deltas.verticalRow);
    } else {
        // Cross-tile: create tile if needed and use atomic for thread safety
        Tile* downTile = ensureNeighborTile(0, 1);
        if (downTile) {
            downTile->atomicApplyVerticalDeltas(baseX, 0, deltas.verticalRow);
        }
    }
}

void Tile::rebuildActivityMask() {
    // Rebuild the activity mask by scanning all 64-bit words
    // A word is active if it contains any non-zero content
    activityMask = 0;
    for (int i = 0; i < TILE_64S; i++) {
        if (cells[i] != 0) {
            activityMask |= (1ULL << i);
        }
    }
}
