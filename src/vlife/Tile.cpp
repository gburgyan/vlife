//
// Created by George Burgyan on 3/1/25.
//

#include "Tile.h"
#include <cstring>
#include <mutex>

Tile::Tile(VLife *board, int32_t tileX, int32_t tileY) :
    board(board), tileX(tileX), tileY(tileY), left(nullptr), right(nullptr), up(nullptr), down(nullptr) {
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
}

void Tile::lockTile() { tileMutex.lock(); }

void Tile::unlockTile() { tileMutex.unlock(); }

bool Tile::getCell(uint32_t localX, uint32_t localY) const {
    // Calculate cell index and bit position
    uint32_t cellIdx = (localY * TILE_WIDTH + localX) / 16;
    uint32_t bitPos = ((localY * TILE_WIDTH + localX) % 16) * 4;

    // High bit of each nibble indicates alive cell
    return (cells[cellIdx] & (1ULL << bitPos)) != 0;
}

void Tile::setCell(uint32_t localX, uint32_t localY, bool alive) {
    // Calculate cell index and bit position
    uint32_t cellIdx = (localY * TILE_WIDTH + localX) / 16;
    uint32_t bitPos = ((localY * TILE_WIDTH + localX) % 16) * 4;

    // Set or clear the bit based on the alive state
    if (alive) {
        cells[cellIdx] |= (1ULL << bitPos);
    } else {
        cells[cellIdx] &= ~(1ULL << bitPos);
    }
}

void Tile::runGenerationPrepare() {
    // Chear changes
    for (int i = 0; i < TILE_CHANGE_64S; i++) {
        changes[i] = 0;
    }

    auto ruleLUT = board->ruleLUT;

    uint64_t changeBuff = 0;
    for (int i = 0; i < TILE_64S; i += 4) {
        // _mm_prefetch((const char*)&cells[i+4], _MM_HINT_T0);

        uint64_t slice = cells[i];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 62;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 60;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 58;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 56;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 54;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 52;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 50;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 48;

        slice = cells[i + 1];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 46;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 44;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 42;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 40;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 38;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 36;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 34;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 32;

        slice = cells[i + 2];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 30;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 28;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 26;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 24;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 22;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 20;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 18;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 16;

        slice = cells[i + 3];
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 56) & 0xFF)]) << 14;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 12;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 10;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 8;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 6;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 4;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 2;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]);

        changes[i >> 2] = changeBuff;
    }
}

void Tile::runGenerationChanges() {
    uint64_t verticalChangeAdd[TILE_64S_WIDTH];
    uint64_t verticalChangeSubtract[TILE_64S_WIDTH];
    uint64_t lineChangeAdd[TILE_64S_WIDTH];
    uint64_t lineChangeSubtract[TILE_64S_WIDTH];


    int topLeftChange = 0;
    int topRightChange = 0;
    int bottomLeftChange = 0;
    int bottomRightChange = 0;

    uint64_t *changesPtr = changes;
    uint64_t *cellsPtr = cells;
    uint64_t work;
    int changeWordsLeft = 0;

    for (int row = 0; row < TILE_HEIGHT; row++) {
        // zero out verticalChangeAdd, verticalChangeSubtract, lineChangeAdd, lineChangeSubtract
        uint64_t rowVerticalChangeAdd[TILE_64S_WIDTH];
        uint64_t rowVerticalChangeSubtract[TILE_64S_WIDTH];
        uint64_t rowLineChangeAdd[TILE_64S_WIDTH];
        uint64_t rowLineChangeSubtract[TILE_64S_WIDTH];

        int rowVerticalLeftChange = 0;
        int rowVerticalRightChange = 0;
        int rowLeftChange = 0;
        int rowRightChange = 0;

        for (int colPos = 0; colPos < TILE_64S; colPos++) {
            if (changeWordsLeft == 0) {
                // Each of the 64-bit cell blocks has 16 4-bit cells.
                work = *changesPtr++;
                changeWordsLeft = 4;
            }

            int changeWord = (work & 0xFFFF000000000000) >> 48;
            work <<= 16;
            changeWordsLeft--;

            if (changeWord == 0) {
                cellsPtr++;
                continue;
            }

            uint64_t cellSpan = *cellsPtr;

            if (changeWord & 0x8000) {
                if (cellSpan & 0x1000000000000000) {
                    // dying cell
                    rowLeftChange--;
                    rowVerticalChangeSubtract[colPos] += 0x2200000000000000;
                } else {
                    // new cell
                    rowLeftChange++;
                    rowVerticalChangeAdd[colPos] += 0x2200000000000000;
                }
                cellSpan ^= 0x1000000000000000;
            }

            for (int i = 1; i < 15; i++) {
                if (changeWord & (1 << i)) {
                    if (cellSpan & (1 << i)) {
                        // dying cell
                        rowVerticalChangeSubtract[colPos] += 0x0000000000000222 << (i * 4);
                    } else {
                        // new cell
                        rowVerticalChangeAdd[colPos] += 0x0000000000000222 << (i * 4);
                    }
                    cellSpan ^= 1 << i * 4;
                }
            }

            if (changeWord & 0x001) {
                if (cellSpan & 0x0000000000000001) {
                    // dying cell
                    rowRightChange--;
                    rowVerticalChangeSubtract[colPos] += 0x0000000000000022;
                } else {
                    // new cell
                    rowRightChange++;
                    rowVerticalChangeAdd[colPos] += 0x0000000000000022;
                }
                cellSpan ^= 0x0000000000000001;
            }

            *cellsPtr = cellSpan;

            cellsPtr++;
        }
    }
}
