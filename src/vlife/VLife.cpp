//
// Created by George Burgyan on 3/1/25.
//

#include "VLife.h"
#include "Tile.h"
#include "VLifeMetrics.h"
#include <algorithm>
#include <cassert>
#include <thread>
#include <tbb/parallel_for_each.h>

VLife::VLife() {
    resetBoard();
    populateRuleLUT();
    populateUpdateLUT();
}

VLife::~VLife() {
    // Deallocate all tiles back to the pool
    for (auto& [coord, tile] : tiles) {
        tilePool.deallocate(tile);
    }
    tiles.clear();
}

void VLife::resetBoard() {
    // Deallocate all tiles back to the pool
    for (auto& [coord, tile] : tiles) {
        tilePool.deallocate(tile);
    }
    tiles.clear();
    tilePool.recycle();

    // Reset linked list pointers
    listHead = nullptr;
    listTail = nullptr;
}

void VLife::addTileToListHead(Tile* tile) {
    // Called during tile creation (already under tilesMutex)
    tile->setListPrev(nullptr);
    tile->setListNext(listHead);
    if (listHead) listHead->setListPrev(tile);
    listHead = tile;
    if (!listTail) listTail = tile;
}

void VLife::removeTileFromList(Tile* tile) {
    Tile* prev = tile->getListPrev();
    Tile* next = tile->getListNext();
    if (prev) prev->setListNext(next);
    else listHead = next;
    if (next) next->setListPrev(prev);
    else listTail = prev;
    tile->setListPrev(nullptr);
    tile->setListNext(nullptr);
}

void VLife::moveToHead(Tile* tile) {
    // Quick check before acquiring lock (common case: already at head)
    if (tile == listHead) return;

    std::lock_guard<std::mutex> lock(listMutex);
    if (tile == listHead) return;  // Re-check after lock

    // Unlink from current position
    Tile* prev = tile->getListPrev();
    Tile* next = tile->getListNext();
    if (prev) prev->setListNext(next);
    if (next) next->setListPrev(prev);
    else listTail = prev;

    // Link at head
    tile->setListPrev(nullptr);
    tile->setListNext(listHead);
    if (listHead) listHead->setListPrev(tile);
    listHead = tile;
}

void VLife::moveToTail(Tile* tile) {
    // Quick check before acquiring lock
    if (tile == listTail) return;

    std::lock_guard<std::mutex> lock(listMutex);
    if (tile == listTail) return;  // Re-check after lock

    // Unlink from current position
    Tile* prev = tile->getListPrev();
    Tile* next = tile->getListNext();
    if (prev) prev->setListNext(next);
    else listHead = next;
    if (next) next->setListPrev(prev);

    // Link at tail
    tile->setListNext(nullptr);
    tile->setListPrev(listTail);
    if (listTail) listTail->setListNext(tile);
    listTail = tile;
    if (!listHead) listHead = tile;
}

Tile *VLife::getTile(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};

    // Fast path: shared lock for read (multiple readers allowed)
    {
        std::shared_lock<std::shared_mutex> readLock(tilesMutex);
        auto it = tiles.find(coord);
        if (it != tiles.end()) {
            return it->second;
        }
    }

    // Slow path: exclusive lock for creation
    std::unique_lock<std::shared_mutex> writeLock(tilesMutex);

    // Re-check after acquiring exclusive lock (another thread may have created it)
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second;
    }

    // Create a new tile using the pool
    Tile *tilePtr = tilePool.allocate(tileX, tileY);
    tiles[coord] = tilePtr;

    // Add to linked list (already under exclusive lock, so no separate lock needed)
    addTileToListHead(tilePtr);

    // Connect to neighboring tiles if they exist (already under exclusive lock)
    // We link all 8 neighbors (orthogonal and diagonal) here.

    // Check left (-1, 0)
    auto leftIt = tiles.find(TileCoord{tileX - 1, tileY});
    if (leftIt != tiles.end()) {
        tilePtr->setNeighbor(leftIt->second, -1, 0);
    }

    // Check right (1, 0)
    auto rightIt = tiles.find(TileCoord{tileX + 1, tileY});
    if (rightIt != tiles.end()) {
        tilePtr->setNeighbor(rightIt->second, 1, 0);
    }

    // Check up (0, -1)
    auto upIt = tiles.find(TileCoord{tileX, tileY - 1});
    if (upIt != tiles.end()) {
        tilePtr->setNeighbor(upIt->second, 0, -1);
    }

    // Check down (0, 1)
    auto downIt = tiles.find(TileCoord{tileX, tileY + 1});
    if (downIt != tiles.end()) {
        tilePtr->setNeighbor(downIt->second, 0, 1);
    }

    // Check diagonal neighbors
    auto upLeftIt = tiles.find(TileCoord{tileX - 1, tileY - 1});
    if (upLeftIt != tiles.end()) {
        tilePtr->setNeighbor(upLeftIt->second, -1, -1);
    }
    auto upRightIt = tiles.find(TileCoord{tileX + 1, tileY - 1});
    if (upRightIt != tiles.end()) {
        tilePtr->setNeighbor(upRightIt->second, 1, -1);
    }
    auto downLeftIt = tiles.find(TileCoord{tileX - 1, tileY + 1});
    if (downLeftIt != tiles.end()) {
        tilePtr->setNeighbor(downLeftIt->second, -1, 1);
    }
    auto downRightIt = tiles.find(TileCoord{tileX + 1, tileY + 1});
    if (downRightIt != tiles.end()) {
        tilePtr->setNeighbor(downRightIt->second, 1, 1);
    }

    return tilePtr;
}

// Helper method to get a tile only if it already exists
Tile *VLife::getTileIfExists(int32_t tileX, int32_t tileY) {
    std::shared_lock<std::shared_mutex> readLock(tilesMutex);

    TileCoord coord{tileX, tileY};
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second;
    }
    return nullptr;
}

GameOfLife::CellState VLife::getCell(uint32_t x, uint32_t y) const {
    // Calculate which tile this cell belongs to
    int32_t tileX = static_cast<int32_t>(x) >> TILE_WIDTH_2;
    int32_t tileY = static_cast<int32_t>(y) >> TILE_HEIGHT_2;

    // Calculate the position within the tile
    uint32_t localX = x & (TILE_WIDTH - 1);
    uint32_t localY = y & (TILE_HEIGHT - 1);

    // Try to find the tile
    TileCoord coord{tileX, tileY};
    auto it = tiles.find(coord);
    if (it == tiles.end()) {
        // Tile doesn't exist, cell is dead
        return CellState::DEAD;
    }

    // Use Tile's getCell method to check if the cell is alive
    bool isAlive = it->second->getCell(localX, localY);

    return isAlive ? CellState::ALIVE : CellState::DEAD;
}

void VLife::setCell(uint32_t x, uint32_t y, CellState state) {
    // Calculate which tile this cell belongs to
    int32_t tileX = static_cast<int32_t>(x) >> TILE_WIDTH_2;
    int32_t tileY = static_cast<int32_t>(y) >> TILE_HEIGHT_2;

    // Get or create the tile
    Tile *tile = getTile(tileX, tileY);

    // Calculate the position within the tile
    uint32_t localX = x & (TILE_WIDTH - 1);
    uint32_t localY = y & (TILE_HEIGHT - 1);

    // Use Tile's setCell method to set the cell state
    tile->setCell(localX, localY, state == CellState::ALIVE);
}

std::vector<GameOfLife::CellState> VLife::getCells(uint32_t startX, uint32_t startY, uint32_t width,
                                                   uint32_t height) const {
    std::vector<CellState> result;
    result.reserve(width * height);

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            result.push_back(getCell(startX + x, startY + y));
        }
    }

    return result;
}

void VLife::setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height,
                     const std::vector<CellState> &cells) {
    // Validate input: cells vector must match the specified dimensions
    assert(cells.size() == width * height && "setCells: cells vector size doesn't match width*height");
    if (cells.size() != width * height) {
        return;
    }

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            setCell(offsetX + x, offsetY + y, cells[y * width + x]);
        }
    }
}

void VLife::runGenerationSequential() {
#ifdef VLIFE_METRICS_ENABLED
    auto metricsTimerStart = std::chrono::high_resolution_clock::now();
    size_t activeTilesCount = 0;
#endif

    // Clear the queue from previous generation (keeps capacity for amortized allocation)
    changedTilesSequential.clear();

    // Phase 1: Traverse linked list from head, process tiles with needsProcessing flag
    // Tiles are moved to head during Phase 2 when they receive neighbor updates
    Tile* tile = listHead;
    while (tile && tile->getNeedsProcessing()) {
        // Mark as active BEFORE clearing flag for next-gen transition detection
        tile->setWasActiveLastGeneration(true);
        tile->clearNeedsProcessing();
        Tile* next = tile->getListNext();  // Save before potential list modification

        if (tile->needsPhase1Processing()) {
            tile->runGenerationPrepare();
            if (tile->hasChanges()) {
                changedTilesSequential.push_back(tile);
            }
#ifdef VLIFE_METRICS_ENABLED
            activeTilesCount++;
#endif
        }
        tile = next;
    }

#ifdef VLIFE_METRICS_ENABLED
    auto phase1End = std::chrono::high_resolution_clock::now();
    uint64_t phase1Time = std::chrono::duration_cast<std::chrono::microseconds>(phase1End - metricsTimerStart).count();
    if (metricsCollector) {
        VLIFE_METRICS_MERGE_TL(*metricsCollector);
        VLIFE_METRICS_END_PHASE1(*metricsCollector, activeTilesCount, phase1Time);
    }
    auto phase2Start = std::chrono::high_resolution_clock::now();
#endif

    // Second pass: apply the changes only to tiles that have changes
    // Use non-atomic version since sequential execution has no concurrent access
    for (Tile* t : changedTilesSequential) {
        t->runGenerationChanges<false>();
    }

#ifdef VLIFE_METRICS_ENABLED
    auto phase2End = std::chrono::high_resolution_clock::now();
    uint64_t phase2Time = std::chrono::duration_cast<std::chrono::microseconds>(phase2End - phase2Start).count();
    if (metricsCollector) {
        VLIFE_METRICS_MERGE_TL(*metricsCollector);
        VLIFE_METRICS_END_PHASE2(*metricsCollector, tiles.size(), phase2Time);
    }
#endif

    // Move newly-inactive tiles to tail (active->inactive transitions)
    // These tiles had wasActiveLastGeneration=true but weren't re-queued in Phase 2
    // Batch move with single lock acquisition to reduce contention
    {
        std::vector<Tile*> tilesToMoveTail;
        Tile* tile = listHead;
        while (tile) {
            Tile* next = tile->getListNext();
            if (tile->getWasActiveLastGeneration() && !tile->getNeedsProcessing()) {
                tile->setWasActiveLastGeneration(false);
                tilesToMoveTail.push_back(tile);
            }
            tile = next;
        }
        // Batch move to reduce lock contention
        if (!tilesToMoveTail.empty()) {
            std::lock_guard<std::mutex> lock(listMutex);
            for (Tile* t : tilesToMoveTail) {
                // Inline unlink and link-at-tail to avoid repeated lock acquisition
                if (t == listTail) continue;
                Tile* prev = t->getListPrev();
                Tile* next = t->getListNext();
                if (prev) prev->setListNext(next);
                else listHead = next;
                if (next) next->setListPrev(prev);

                t->setListNext(nullptr);
                t->setListPrev(listTail);
                if (listTail) listTail->setListNext(t);
                listTail = t;
            }
        }
    }
}

void VLife::runGeneration() {
#ifdef VLIFE_METRICS_ENABLED
    auto genStartTime = std::chrono::high_resolution_clock::now();
    size_t initialTileCount = tiles.size();
    if (metricsCollector) {
        VLIFE_METRICS_BEGIN_GEN(*metricsCollector, generationNumber, initialTileCount);
    }
#endif

    // Fall back to sequential for small tile counts or when parallel is disabled
    if (!parallelEnabled || tiles.size() < 10) {
        runGenerationSequential();

#ifdef VLIFE_METRICS_ENABLED
        size_t tilesCreated = tiles.size() > initialTileCount ? tiles.size() - initialTileCount : 0;
        size_t preEvictCount = tiles.size();
#endif

        evictDeadTiles();
        generationNumber++;

#ifdef VLIFE_METRICS_ENABLED
        size_t tilesEvicted = preEvictCount > tiles.size() ? preEvictCount - tiles.size() : 0;
        auto genEndTime = std::chrono::high_resolution_clock::now();
        uint64_t totalTime = std::chrono::duration_cast<std::chrono::microseconds>(genEndTime - genStartTime).count();
        if (metricsCollector) {
            int32_t minX, maxX, minY, maxY;
            getBoardExtent(minX, maxX, minY, maxY);
            VLIFE_METRICS_END_GEN(*metricsCollector, getTotalLiveCells(), tiles.size(),
                                   minX, maxX, minY, maxY, totalTime, tilesCreated, tilesEvicted);
        }
#endif
        return;
    }

#ifdef VLIFE_METRICS_ENABLED
    auto phase1Start = std::chrono::high_resolution_clock::now();
    std::atomic<size_t> activeTileCount{0};
#endif

    // Clear the queue from previous generation (keeps capacity for amortized allocation)
    tilesWithChanges.clear();

    // Build vector of tiles to process from linked list
    // Tiles are moved to head during Phase 2 when they receive neighbor updates
    std::vector<Tile*> tilesToProcess;
    tilesToProcess.reserve(tiles.size());
    {
        Tile* tile = listHead;
        while (tile && tile->getNeedsProcessing()) {
            // Mark as active BEFORE clearing flag for next-gen transition detection
            tile->setWasActiveLastGeneration(true);
            tile->clearNeedsProcessing();
            if (tile->needsPhase1Processing()) {
                tilesToProcess.push_back(tile);
            }
            tile = tile->getListNext();
        }
    }

    // PHASE 1: Parallel preparation on tiles that need processing
    tbb::parallel_for_each(tilesToProcess.begin(), tilesToProcess.end(),
        [this
#ifdef VLIFE_METRICS_ENABLED
            , &activeTileCount
#endif
        ](Tile* tile) {
            tile->runGenerationPrepare();
            if (tile->hasChanges()) {
                tilesWithChanges.push_back(tile);
            }
#ifdef VLIFE_METRICS_ENABLED
            activeTileCount.fetch_add(1, std::memory_order_relaxed);
#endif
        }
    );

#ifdef VLIFE_METRICS_ENABLED
    auto phase1End = std::chrono::high_resolution_clock::now();
    uint64_t phase1Time = std::chrono::duration_cast<std::chrono::microseconds>(phase1End - phase1Start).count();
    if (metricsCollector) {
        VLIFE_METRICS_END_PHASE1(*metricsCollector, activeTileCount.load(), phase1Time);
    }
    auto phase2Start = std::chrono::high_resolution_clock::now();
#endif

    // PHASE 2: Only process tiles that have changes (collected during Phase 1)
    tbb::parallel_for_each(tilesWithChanges.begin(), tilesWithChanges.end(),
        [](Tile* tile) {
            tile->runGenerationChanges<true>();
        }
    );

#ifdef VLIFE_METRICS_ENABLED
    auto phase2End = std::chrono::high_resolution_clock::now();
    uint64_t phase2Time = std::chrono::duration_cast<std::chrono::microseconds>(phase2End - phase2Start).count();
    size_t tilesCreated = tiles.size() > initialTileCount ? tiles.size() - initialTileCount : 0;
    size_t preEvictCount = tiles.size();
    if (metricsCollector) {
        VLIFE_METRICS_END_PHASE2(*metricsCollector, tiles.size(), phase2Time);
    }
#endif

    // Move newly-inactive tiles to tail (active->inactive transitions)
    // These tiles had wasActiveLastGeneration=true but weren't re-queued in Phase 2
    // Batch move with single lock acquisition to reduce contention
    {
        std::vector<Tile*> tilesToMoveTail;
        Tile* tile = listHead;
        while (tile) {
            Tile* next = tile->getListNext();
            if (tile->getWasActiveLastGeneration() && !tile->getNeedsProcessing()) {
                tile->setWasActiveLastGeneration(false);
                tilesToMoveTail.push_back(tile);
            }
            tile = next;
        }
        // Batch move to reduce lock contention
        if (!tilesToMoveTail.empty()) {
            std::lock_guard<std::mutex> lock(listMutex);
            for (Tile* t : tilesToMoveTail) {
                // Inline unlink and link-at-tail to avoid repeated lock acquisition
                if (t == listTail) continue;
                Tile* prev = t->getListPrev();
                Tile* next = t->getListNext();
                if (prev) prev->setListNext(next);
                else listHead = next;
                if (next) next->setListPrev(prev);

                t->setListNext(nullptr);
                t->setListPrev(listTail);
                if (listTail) listTail->setListNext(t);
                listTail = t;
            }
        }
    }

    // Evict dead tiles to prevent memory growth
    evictDeadTiles();
    generationNumber++;

#ifdef VLIFE_METRICS_ENABLED
    size_t tilesEvicted = preEvictCount > tiles.size() ? preEvictCount - tiles.size() : 0;
    auto genEndTime = std::chrono::high_resolution_clock::now();
    uint64_t totalTime = std::chrono::duration_cast<std::chrono::microseconds>(genEndTime - genStartTime).count();
    if (metricsCollector) {
        int32_t minX, maxX, minY, maxY;
        getBoardExtent(minX, maxX, minY, maxY);
        VLIFE_METRICS_END_GEN(*metricsCollector, getTotalLiveCells(), tiles.size(),
                               minX, maxX, minY, maxY, totalTime, tilesCreated, tilesEvicted);
    }
#endif
}

void VLife::runGenerations(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        runGeneration();
    }
}

void VLife::removeTile(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};

    // Find the tile
    auto it = tiles.find(coord);
    if (it == tiles.end()) {
        return; // Tile doesn't exist
    }

    Tile *tile = it->second;

    // Unlink from neighbors
    if (tile->left) {
        tile->left->right = nullptr;
    }
    if (tile->right) {
        tile->right->left = nullptr;
    }
    if (tile->up) {
        tile->up->down = nullptr;
    }
    if (tile->down) {
        tile->down->up = nullptr;
    }
    if (tile->upLeft) tile->upLeft->downRight = nullptr;
    if (tile->upRight) tile->upRight->downLeft = nullptr;
    if (tile->downLeft) tile->downLeft->upRight = nullptr;
    if (tile->downRight) tile->downRight->upLeft = nullptr;

#ifdef DEBUG_VLIFE
    // Verify all cells are dead
    for (int i = 0; i < TILE_64S; i++) {
        if (tile->cells[i] != 0) {
            // Some cells are still alive, don't remove the tile
            return;
        }
    }
#endif

    // Remove from linked list before deallocating
    removeTileFromList(tile);

    // Deallocate tile and remove from map
    tilePool.deallocate(tile);
    tiles.erase(it);
}

void VLife::evictDeadTiles() {
    // Only check periodically (power of 2 interval for fast modulo via bitmask)
    if ((generationNumber & (EVICTION_CHECK_INTERVAL - 1)) != 0) {
        return;
    }

    uint8_t currentGen = static_cast<uint8_t>(generationNumber);

    // Scan from tail backwards - inactive tiles cluster at tail due to state-transition-based movement
    // Stop when hitting active tiles for O(inactive) complexity instead of O(all_tiles)
    Tile* tile = listTail;
    while (tile) {
        Tile* prev = tile->getListPrev();

        // Stop at active region (tiles with needsProcessing or recently active)
        // Active tiles are near the head, so hitting one means we've scanned all inactive tiles
        if (tile->getNeedsProcessing() || tile->getWasActiveLastGeneration()) {
            break;
        }

        if (tile->isSafeToEvict()) {
            // Use modular arithmetic for age calculation (handles wrap-around)
            uint8_t age = currentGen - tile->getLastModifiedGeneration();
            if (age >= EVICTION_AGE_THRESHOLD) {
                // Remove from linked list
                removeTileFromList(tile);

                // Unlink from neighbors (inline from removeTile)
                if (tile->left) tile->left->right = nullptr;
                if (tile->right) tile->right->left = nullptr;
                if (tile->up) tile->up->down = nullptr;
                if (tile->down) tile->down->up = nullptr;
                if (tile->upLeft) tile->upLeft->downRight = nullptr;
                if (tile->upRight) tile->upRight->downLeft = nullptr;
                if (tile->downLeft) tile->downLeft->upRight = nullptr;
                if (tile->downRight) tile->downRight->upLeft = nullptr;

                TileCoord coord{tile->getTileX(), tile->getTileY()};
                tilePool.deallocate(tile);
                tiles.erase(coord);
            }
        }
        tile = prev;
    }
}

void VLife::populateRuleLUT() {
    // The rule lut is a 256-byte array, where each byte represents the rule for a specific cell configuration.
    // The first 4 bits represent the left cell, and the last 4 bits represent the right cell.
    // The bits are arranged as follows:
    //   0bANNN
    //
    //   A = alive (1) or dead (0)
    //   N = number of alive neighbors (0-7)
    //
    // Since two cells are represented in a single byte, the liveness of one cell can be included in the neighbor
    // of the other cell *implicitly*. A cell can have a total of eight neighbors, and normally the three bits
    // representing the number of alive neighbors would not be enough to represent the state of the cell. This
    // is fixed by using the left cell's state as a neighbor of the right cell and vice versa. This means that
    // the left cell's state is included in the right cell's neighbor count and the right cell's state is included
    // in the left cell's neighbor count.
    //
    // The result of the lut is two bits, reflecting the change in state of the left and right cells after
    // running this generation.

    int minBorn = 3; // 0bLLLLRRRR
    int maxBorn = 3;
    int minSurvive = 2;
    int maxSurvive = 3;

    for (int i = 0; i < 256; i++) {
        int leftAlive = (i >> 7) & 1;       // Bit 7: left cell alive
        int leftNeighbors = (i >> 4) & 0x7; // Bits 6-4: left neighbor count
        int rightAlive = (i >> 3) & 1;      // Bit 3: right cell alive
        int rightNeighbors = i & 0x7;       // Bits 2-0: right neighbor count

        leftNeighbors += rightAlive;
        rightNeighbors += leftAlive;

        bool leftNewAlive;
        bool leftChanged;
        bool rightNewAlive;
        bool rightChanged;

        if (leftAlive) {
            if (leftNeighbors < minSurvive || leftNeighbors > maxSurvive) {
                leftNewAlive = false;
                leftChanged = true;
            } else {
                leftNewAlive = true;
                leftChanged = false;
            }
        } else {
            if (leftNeighbors >= minBorn && leftNeighbors <= maxBorn) {
                leftNewAlive = true;
                leftChanged = true;
            } else {
                leftNewAlive = false;
                leftChanged = false;
            }
        }

        if (rightAlive) {
            if (rightNeighbors < minSurvive || rightNeighbors > maxSurvive) {
                rightNewAlive = false;
                rightChanged = true;
            } else {
                rightNewAlive = true;
                rightChanged = false;
            }
        } else {
            if (rightNeighbors >= minBorn && rightNeighbors <= maxBorn) {
                rightNewAlive = true;
                rightChanged = true;
            } else {
                rightNewAlive = false;
                rightChanged = false;
            }
        }

        int result = leftChanged << 1 | rightChanged;
        ruleLUT[i] = static_cast<std::byte>(result);
    }
}

void VLife::populateUpdateLUT() {
    // Populate the deltaLUT for neighbor count updates.
    // Index encoding (4 bits):
    //   bits [3:2] = left cell state:  00=unchanged, 01=became alive, 10=died
    //   bits [1:0] = right cell state: 00=unchanged, 01=became alive, 10=died
    //
    // For a cell pair at positions (2k, y) [right] and (2k+1, y) [left]:
    //   - sameRow[0] (x-1): only affected by right cell
    //   - sameRow[1] (x+2): only affected by left cell
    //   - verticalRow[0] (x-1): only affected by right cell
    //   - verticalRow[1] (x):   affected by both cells (shared neighbor)
    //   - verticalRow[2] (x+1): affected by both cells (shared neighbor)
    //   - verticalRow[3] (x+2): only affected by left cell

    // Initialize all entries to zero
    for (int i = 0; i < 16; i++) {
        deltaLUT[i].sameRow[0] = 0;
        deltaLUT[i].sameRow[1] = 0;
        deltaLUT[i].verticalRow[0] = 0;
        deltaLUT[i].verticalRow[1] = 0;
        deltaLUT[i].verticalRow[2] = 0;
        deltaLUT[i].verticalRow[3] = 0;
    }

    // Helper: convert state code to delta: 0=unchanged(0), 1=became alive(+1), 2=died(-1)
    auto stateToDelta = [](int state) -> int8_t {
        if (state == 1) return 1;   // became alive
        if (state == 2) return -1;  // died
        return 0;                   // unchanged
    };

    for (int leftState = 0; leftState < 3; leftState++) {
        for (int rightState = 0; rightState < 3; rightState++) {
            int idx = (leftState << 2) | rightState;
            int8_t leftDelta = stateToDelta(leftState);
            int8_t rightDelta = stateToDelta(rightState);

            // sameRow[0] (x-1): only affected by right cell
            deltaLUT[idx].sameRow[0] = rightDelta;
            // sameRow[1] (x+2): only affected by left cell
            deltaLUT[idx].sameRow[1] = leftDelta;

            // verticalRow for rows above and below
            // [0] (x-1): only right cell
            deltaLUT[idx].verticalRow[0] = rightDelta;
            // [1] (x): both cells (shared)
            deltaLUT[idx].verticalRow[1] = static_cast<int8_t>(leftDelta + rightDelta);
            // [2] (x+1): both cells (shared)
            deltaLUT[idx].verticalRow[2] = static_cast<int8_t>(leftDelta + rightDelta);
            // [3] (x+2): only left cell
            deltaLUT[idx].verticalRow[3] = leftDelta;
        }
    }
}

uint64_t VLife::getTotalLiveCells() const {
    uint64_t total = 0;
    for (const auto& [coord, tile] : tiles) {
        total += tile->getLiveCount();
    }
    return total;
}

void VLife::getBoardExtent(int32_t& minX, int32_t& maxX, int32_t& minY, int32_t& maxY) const {
    if (tiles.empty()) {
        minX = maxX = minY = maxY = 0;
        return;
    }

    minX = INT32_MAX;
    maxX = INT32_MIN;
    minY = INT32_MAX;
    maxY = INT32_MIN;

    for (const auto& [coord, tile] : tiles) {
        if (coord.x < minX) minX = coord.x;
        if (coord.x > maxX) maxX = coord.x;
        if (coord.y < minY) minY = coord.y;
        if (coord.y > maxY) maxY = coord.y;
    }
}
