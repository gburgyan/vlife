//
// Tile pool allocator implementation
//

#include "TilePool.h"
#include "Tile.h"
#include <new>

TilePool::TilePool(VLife* board) : board(board) {}

TilePool::~TilePool() = default;  // pool destructor frees all memory

Tile* TilePool::allocate(int32_t tileX, int32_t tileY) {
    void* mem = pool.malloc(sizeof(Tile));
    if (!mem) throw std::bad_alloc();
    return new (mem) Tile(board, tileX, tileY);  // placement new
}

void TilePool::deallocate(Tile* tile) {
    tile->~Tile();         // explicit destructor
    pool.free(tile);       // return to pool
}

void TilePool::recycle() {
    pool.recycle();        // free all memory in pool
}
