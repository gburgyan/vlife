//
// Created by George Burgyan on 3/1/25.
//

#include "VLife.h"
#include "Tile.h"
#include <algorithm>
#include <cassert>

// ThreadPool implementation
ThreadPool::ThreadPool(size_t numThreads) {
    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stop = true;
    }
    taskAvailable.notify_all();
    for (auto& worker : workers) {
        worker.join();
    }
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            taskAvailable.wait(lock, [this] { return stop || !taskQueue.empty(); });

            if (stop && taskQueue.empty()) {
                return;
            }

            task = std::move(taskQueue.front());
            taskQueue.pop();
        }

        task();

        if (--pendingTasks == 0) {
            tasksDone.notify_all();
        }
    }
}

void ThreadPool::runBatch(const std::vector<std::function<void()>>& tasks) {
    if (tasks.empty()) return;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingTasks = tasks.size();
        for (const auto& task : tasks) {
            taskQueue.push(task);
        }
    }
    taskAvailable.notify_all();

    // Wait for all tasks to complete
    std::unique_lock<std::mutex> lock(queueMutex);
    tasksDone.wait(lock, [this] { return pendingTasks == 0; });
}

VLife::VLife() {
    resetBoard();
    populateRuleLUT();
    populateUpdateLUT();
}

VLife::~VLife() {
    // Clear all tiles
    tiles.clear();
}

void VLife::resetBoard() {
    // Clear all existing tiles
    tiles.clear();
    spatialOrder.clear();
    spatialOrderDirty = true;
}

Tile *VLife::getTile(int32_t tileX, int32_t tileY) {
    TileCoord coord{tileX, tileY};

    // Fast path: shared lock for read (multiple readers allowed)
    {
        std::shared_lock<std::shared_mutex> readLock(tilesMutex);
        auto it = tiles.find(coord);
        if (it != tiles.end()) {
            return it->second.get();
        }
    }

    // Slow path: exclusive lock for creation
    std::unique_lock<std::shared_mutex> writeLock(tilesMutex);

    // Re-check after acquiring exclusive lock (another thread may have created it)
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }

    // Create a new tile
    auto newTile = std::make_unique<Tile>(this, tileX, tileY);
    Tile *tilePtr = newTile.get();
    tiles[coord] = std::move(newTile);
    spatialOrderDirty = true;

    // Connect to neighboring tiles if they exist (already under exclusive lock)
    // We link all 8 neighbors (orthogonal and diagonal) here.
    // The 4-color parallel scheme ensures same-color tiles don't share neighbors.

    // Check left (-1, 0)
    auto leftIt = tiles.find(TileCoord{tileX - 1, tileY});
    if (leftIt != tiles.end()) {
        tilePtr->setNeighbor(leftIt->second.get(), -1, 0);
    }

    // Check right (1, 0)
    auto rightIt = tiles.find(TileCoord{tileX + 1, tileY});
    if (rightIt != tiles.end()) {
        tilePtr->setNeighbor(rightIt->second.get(), 1, 0);
    }

    // Check up (0, -1)
    auto upIt = tiles.find(TileCoord{tileX, tileY - 1});
    if (upIt != tiles.end()) {
        tilePtr->setNeighbor(upIt->second.get(), 0, -1);
    }

    // Check down (0, 1)
    auto downIt = tiles.find(TileCoord{tileX, tileY + 1});
    if (downIt != tiles.end()) {
        tilePtr->setNeighbor(downIt->second.get(), 0, 1);
    }

    // Check diagonal neighbors
    auto upLeftIt = tiles.find(TileCoord{tileX - 1, tileY - 1});
    if (upLeftIt != tiles.end()) {
        tilePtr->setNeighbor(upLeftIt->second.get(), -1, -1);
    }
    auto upRightIt = tiles.find(TileCoord{tileX + 1, tileY - 1});
    if (upRightIt != tiles.end()) {
        tilePtr->setNeighbor(upRightIt->second.get(), 1, -1);
    }
    auto downLeftIt = tiles.find(TileCoord{tileX - 1, tileY + 1});
    if (downLeftIt != tiles.end()) {
        tilePtr->setNeighbor(downLeftIt->second.get(), -1, 1);
    }
    auto downRightIt = tiles.find(TileCoord{tileX + 1, tileY + 1});
    if (downRightIt != tiles.end()) {
        tilePtr->setNeighbor(downRightIt->second.get(), 1, 1);
    }

    return tilePtr;
}

// Helper method to get a tile only if it already exists
Tile *VLife::getTileIfExists(int32_t tileX, int32_t tileY) {
    std::shared_lock<std::shared_mutex> readLock(tilesMutex);

    TileCoord coord{tileX, tileY};
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
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
    // First pass: prepare all tiles for the generation (in spatial order)
    // Tile-level skip optimization: skip tiles with no activity
    for (Tile* tile : spatialOrder) {
        // Only process tiles that have potential activity
        // A tile with activityMask == 0 has no live cells and no neighbor counts,
        // so it cannot produce any changes
        if (tile->hasActivity()) {
            tile->runGenerationPrepare();
        }
    }

    // Second pass: apply the changes (in spatial order)
    for (Tile* tile : spatialOrder) {
        tile->runGenerationChanges();
    }
}

void VLife::runGeneration() {
    // Rebuild spatial order if needed
    if (spatialOrderDirty) {
        rebuildSpatialOrder();
    }

    // Fall back to sequential for small tile counts or when parallel is disabled
    // Higher threshold due to synchronization overhead
    if (!parallelEnabled || spatialOrder.size() < 64) {
        runGenerationSequential();
        evictDeadTiles();
        return;
    }

    // Lazy initialize thread pool and cache thread count
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;  // Fallback if detection fails

    if (!threadPool) {
        threadPool = std::make_unique<ThreadPool>(numThreads);
    }

    // PHASE 1: Fully parallel (no dependencies between tiles)
    // Each tile only writes to its own changes[] array
    // Batch tiles together to reduce task overhead
    std::vector<std::function<void()>> tasks;

    // Collect active tiles
    std::vector<Tile*> activeTiles;
    activeTiles.reserve(spatialOrder.size());
    for (Tile* tile : spatialOrder) {
        if (tile->hasActivity()) {
            activeTiles.push_back(tile);
        }
    }

    // Create batched tasks for Phase 1
    size_t batchSize = std::max(size_t(1), activeTiles.size() / numThreads);
    tasks.reserve(numThreads + 1);

    for (size_t i = 0; i < activeTiles.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, activeTiles.size());
        tasks.push_back([&activeTiles, i, end] {
            for (size_t j = i; j < end; j++) {
                activeTiles[j]->runGenerationPrepare();
            }
        });
    }
    threadPool->runBatch(tasks);
    tasks.clear();

    // PHASE 2: 4-color parallel (each color group is independent)
    // Tiles of the same color are at least 2 steps apart in both X and Y,
    // meaning they share NO neighbors (orthogonal or diagonal).
    // Process each color sequentially, but tiles within a color in parallel.
    for (int color = 0; color < 4; color++) {
        auto& group = colorGroups[color];
        if (group.empty()) continue;

        // Batch tiles within the color group
        batchSize = std::max(size_t(1), group.size() / numThreads);

        for (size_t i = 0; i < group.size(); i += batchSize) {
            size_t end = std::min(i + batchSize, group.size());
            tasks.push_back([&group, i, end] {
                for (size_t j = i; j < end; j++) {
                    group[j]->runGenerationChanges();
                }
            });
        }
        threadPool->runBatch(tasks);
        tasks.clear();
    }

    // Evict dead tiles to prevent memory growth
    evictDeadTiles();
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

    Tile *tile = it->second.get();

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

    // Remove the tile from the map
    tiles.erase(it);
    spatialOrderDirty = true;
}

void VLife::rebuildSpatialOrder() {
    spatialOrder.clear();
    spatialOrder.reserve(tiles.size());

    for (auto& [coord, tile] : tiles) {
        spatialOrder.push_back(tile.get());
    }

    // Sort tiles by row-major order (y first, then x) for cache-friendly iteration
    std::sort(spatialOrder.begin(), spatialOrder.end(),
        [](const Tile* a, const Tile* b) {
            int64_t aKey = (static_cast<int64_t>(a->getTileY()) << 32) | (a->getTileX() & 0xFFFFFFFF);
            int64_t bKey = (static_cast<int64_t>(b->getTileY()) << 32) | (b->getTileX() & 0xFFFFFFFF);
            return aKey < bKey;
        });

    spatialOrderDirty = false;

    // Rebuild color groups for parallel processing
    rebuildColorGroups();
}

void VLife::rebuildColorGroups() {
    for (int i = 0; i < 4; i++) {
        colorGroups[i].clear();
        colorGroups[i].reserve(spatialOrder.size() / 4 + 1);
    }

    for (Tile* tile : spatialOrder) {
        // Color index based on (tileX % 2) + (tileY % 2) * 2
        int colorIdx = (tile->getTileX() & 1) + ((tile->getTileY() & 1) << 1);
        colorGroups[colorIdx].push_back(tile);
    }
}

void VLife::evictDeadTiles() {
    // Collect dead tiles using cooldown-based eviction to prevent flapping
    // Tiles must remain inactive for TILE_COOLDOWN_GENERATIONS before being evicted
    std::vector<TileCoord> deadTiles;

    for (auto& [coord, tile] : tiles) {
        if (tile->getLiveCount() == 0) {
            if (!tile->hasActivity()) {
                // Tile is completely inactive - decrement cooldown
                if (tile->decrementCooldown()) {
                    // Cooldown expired, mark for eviction
                    deadTiles.push_back(coord);
                }
            } else {
                // Tile has neighbor counts but no live cells - reset cooldown
                tile->resetCooldown();
            }
        } else {
            // Tile has live cells - reset cooldown
            tile->resetCooldown();
        }
    }

    // Remove dead tiles
    for (const auto& coord : deadTiles) {
        removeTile(coord.x, coord.y);
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
