//
// Created by George Burgyan on 3/1/25.
//

#include "Tile.h"
#include <mutex>
#include <immintrin.h>

void Tile::lockTile() {
    tileMutex.lock();
}

void Tile::unlockTile() {
    tileMutex.unlock();
}

void Tile::runGenerationPrepare() {
    // Chear changes
    for (int i = 0; i < TILE_CHANGE_64S; i++) {
        changes[i] = 0;
    }

    auto ruleLUT = board->ruleLUT;

    uint64_t changeBuff = 0;
    for (int i = 0; i < TILE_64S; i += 4) {
        _mm_prefetch((const char*)&cells[i+4], _MM_HINT_T0);

        uint64_t slice = cells[i];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 62;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 60;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 58;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 56;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 54;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 52;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 50;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 48;

        slice = cells[i+1];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 46;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 44;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 42;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 40;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 38;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 36;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 34;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 32;

        slice = cells[i+2];
        changeBuff |= static_cast<uint64_t>(ruleLUT[(slice >> 56)]) << 30;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 28;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 26;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 24;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 22;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 20;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 18;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]) << 16;

        slice = cells[i+3];
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 56) & 0xFF)]) << 14;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 48) & 0xFF)]) << 12;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 40) & 0xFF)]) << 10;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 32) & 0xFF)]) << 8;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 24) & 0xFF)]) << 6;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 16) & 0xFF)]) << 4;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice >> 8) & 0xFF)]) << 2;
        changeBuff |= static_cast<uint64_t>(ruleLUT[((slice) & 0xFF)]);

        changes[i>>2] = changeBuff;
    }
}

void Tile::runGenerationChanges() {
    uint64_t verticalChangeAdd[TILE_64S_WIDTH];
    uint64_t verticalChangeSubtract[TILE_64S_WIDTH];
    uint64_t lineChangeAdd[TILE_64S_WIDTH];
    uint64_t lineChangeSubtract[TILE_64S_WIDTH];

    uint64_t topChangeLine[TILE_64S_WIDTH];
    uint64_t bottomChangeLine[TILE_64S_WIDTH];
    uint64_t leftChangeLine[TILE_64S_HEIGHT];
    uint64_t rightChangeLine[TILE_64S_HEIGHT];

    int topLeftChange = 0;
    int topRightChange = 0;
    int bottomLeftChange = 0;
    int bottomRightChange = 0;

    uint64_t* changesPtr = changes;
    uint64_t* cellsPtr = cells;
    uint64_t work;
    int changeWordsLeft = 0;

    for (int row = 0; row < TILE_HEIGHT; row++) {
        for (int colPos = 0; colPos < TILE_64S; colPos++) {
            if (changeWordsLeft == 0) {
                // Each of the 64-bit cell blocks has 16 4-bit cells.
                work = *changesPtr++;
                changeWordsLeft = 4;
            }

            int changeWord = work & 0xFFFF000000000000;
            work <<= 16;
            changeWordsLeft--;

            if (changeWord == 0) {
                cellsPtr++;
                continue;
            }

            uint64_t cellSpan = *cellsPtr;

            for (int j = 0; j < 8; j++) {
                int changeBits = changeWord & 0xC000;
                changeWord <<= 2;
                if (changeBits == 0) {
                    continue;
                }
                int cells = (cellSpan >> ((7-j)*8)) & 0x11;
                bool leftAlive = cells & 0x10;
                bool rightAlive = cells & 0x01;
                switch (changeBits) {
                    case 0x3:
                        // Both cells are changing


                }
            }

            cellsPtr++;
        }
    }

}

