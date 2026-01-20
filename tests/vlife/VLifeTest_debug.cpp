#include <gtest/gtest.h>
#include <memory>
#include <iostream>
#include "../../src/vlife/VLife.h"
#include "../../src/vlife/Tile.h"

// Forward declaration for friend test
class TileTest;

class VLifeDebugTest : public ::testing::Test {
protected:
    void SetUp() override {
        vlife = std::make_unique<VLife>();
    }

    std::unique_ptr<VLife> vlife;
    
    // Helper function to get neighbor count from cell representation
    uint32_t getNeighborCount(Tile* tile, uint32_t x, uint32_t y) {
        uint32_t cellIdx = (x + y * TILE_WIDTH) / 16;
        uint32_t bitPos = ((x + y * TILE_WIDTH) % 16) * 4;
        
        // Extract the neighbor count (bits 0-2)
        uint64_t mask = 0x7ULL << bitPos; // Mask for the neighbor count bits
        uint64_t result = (tile->cells[cellIdx] & mask) >> bitPos;
        std::cout << "getNeighborCount(" << x << ", " << y << "): cellIdx=" << cellIdx 
                  << ", bitPos=" << bitPos << ", mask=0x" << std::hex << mask 
                  << ", cell value=0x" << tile->cells[cellIdx] 
                  << ", result=" << std::dec << result << std::endl;
        return result;
    }
    
    // Helper function to check if a cell is alive
    bool isCellAlive(Tile* tile, uint32_t x, uint32_t y) {
        uint32_t cellIdx = (x + y * TILE_WIDTH) / 16;
        uint32_t bitPos = ((x + y * TILE_WIDTH) % 16) * 4;
        
        // Check the high bit (bit 3) of the nibble
        uint64_t mask = 0x8ULL << bitPos; // Mask for the alive bit
        bool result = (tile->cells[cellIdx] & mask) != 0;
        std::cout << "isCellAlive(" << x << ", " << y << "): cellIdx=" << cellIdx 
                  << ", bitPos=" << bitPos << ", mask=0x" << std::hex << mask 
                  << ", cell value=0x" << tile->cells[cellIdx] 
                  << ", result=" << std::boolalpha << result << std::endl;
        return result;
    }
};

// Test neighbor counting functionality with debug output
TEST_F(VLifeDebugTest, NeighborCountingDebug) {
    // Get the tile at (0, 0)
    Tile* tile = vlife->getTile(0, 0);
    
    std::cout << "Initial state:" << std::endl;
    // Cell at (5,5) with no neighbors should have neighbor count 0
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 0);
    
    std::cout << "\nSetting cell (6,5) to ALIVE:" << std::endl;
    // Set a neighboring cell alive
    vlife->setCell(6, 5, GameOfLife::CellState::ALIVE);
    isCellAlive(tile, 6, 5);
    
    // Cell at (5,5) should now have 1 neighbor
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 1);
    
    std::cout << "\nSetting cell (4,5) to ALIVE:" << std::endl;
    // Add more neighbors
    vlife->setCell(4, 5, GameOfLife::CellState::ALIVE);
    isCellAlive(tile, 4, 5);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 2);
    
    std::cout << "\nSetting cell (5,4) to ALIVE:" << std::endl;
    vlife->setCell(5, 4, GameOfLife::CellState::ALIVE);
    isCellAlive(tile, 5, 4);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 3);
    
    std::cout << "\nSetting cell (4,5) to DEAD:" << std::endl;
    // Remove a neighbor
    vlife->setCell(4, 5, GameOfLife::CellState::DEAD);
    isCellAlive(tile, 4, 5);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 2);
}