//
// Tile pool allocator using TBB's memory_pool
//

#pragma once

#define TBB_PREVIEW_MEMORY_POOL 1
#include <tbb/memory_pool.h>
#include <cstdint>

class Tile;
class VLife;

// Wrapper around TBB's memory_pool for Tile allocation
// Provides thread-safe allocation with object construction/reinitialization
class TilePool {
public:
    explicit TilePool(VLife* board);
    ~TilePool();

    // Allocate and construct a Tile at (tileX, tileY)
    // Thread-safe (TBB memory_pool is thread-safe)
    Tile* allocate(int32_t tileX, int32_t tileY);

    // Destruct and return memory to pool
    void deallocate(Tile* tile);

    // Recycle all memory (for board reset)
    void recycle();

private:
    VLife* board;
    tbb::memory_pool<std::allocator<char>> pool;
};
