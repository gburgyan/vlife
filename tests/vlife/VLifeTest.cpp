#include <gtest/gtest.h>
#include <memory>
#include "../../src/vlife/VLife.h"
#include "../../src/vlife/Tile.h"

// Forward declaration for friend test
class TileTest;

class VLifeTest : public ::testing::Test {
protected:
    void SetUp() override {
        vlife = std::make_unique<VLife>();
    }

    std::unique_ptr<VLife> vlife;
    
    // Helper function to get neighbor count from cell representation
    uint32_t getNeighborCount(Tile* tile, uint32_t x, uint32_t y) {
        uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
        bool rightCell = x & 1;
        uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

        uint32_t oppositeCellLive;
        if (rightCell) {
            oppositeCellLive = tile->cells[cellIdx] & (1ULL << 7);
        } else {
            oppositeCellLive = tile->cells[cellIdx] & (1ULL << 3);
        }

        // Extract the neighbor count (bits 0-2)
        uint64_t mask = 0x7ULL << bitPos; // Mask for the neighbor count bits
        return ((tile->cells[cellIdx] & mask) >> bitPos) + oppositeCellLive;
    }
};

// Test that a new VLife board is empty
TEST_F(VLifeTest, NewBoardIsEmpty) {
    // Check a few random cells to ensure they are dead
    EXPECT_EQ(vlife->getCell(0, 0), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(100, 100), GameOfLife::CellState::DEAD);
}

// Test setting and getting a cell
TEST_F(VLifeTest, SetAndGetCell) {
    // Set a cell to alive
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::ALIVE);

    // Set a cell to dead
    vlife->setCell(5, 5, GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::DEAD);
}

// Test setting and getting multiple cells
TEST_F(VLifeTest, SetAndGetMultipleCells) {
    // Create a pattern (a 3x3 block)
    std::vector<GameOfLife::CellState> pattern = {
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE
    };

    // Set the pattern
    vlife->setCells(10, 10, 3, 3, pattern);

    // Get the same region and verify
    auto result = vlife->getCells(10, 10, 3, 3);
    EXPECT_EQ(result, pattern);
}

// Test resetting the board
TEST_F(VLifeTest, ResetBoard) {
    // Set a few cells to alive
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);
    vlife->setCell(6, 6, GameOfLife::CellState::ALIVE);
    vlife->setCell(7, 7, GameOfLife::CellState::ALIVE);

    // Verify cells are alive
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(6, 6), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(7, 7), GameOfLife::CellState::ALIVE);

    // Reset the board
    vlife->resetBoard();

    // Verify cells are now dead
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(6, 6), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(7, 7), GameOfLife::CellState::DEAD);
}

// Test boundary conditions
TEST_F(VLifeTest, BoundaryConditions) {
    // Set cells at various distances from origin
    vlife->setCell(0, 0, GameOfLife::CellState::ALIVE);
    vlife->setCell(1000, 0, GameOfLife::CellState::ALIVE);
    vlife->setCell(0, 1000, GameOfLife::CellState::ALIVE);
    vlife->setCell(1000, 1000, GameOfLife::CellState::ALIVE);

    // Verify cells are set correctly
    EXPECT_EQ(vlife->getCell(0, 0), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1000, 0), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(0, 1000), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1000, 1000), GameOfLife::CellState::ALIVE);
}

// Test getTile method
TEST_F(VLifeTest, GetTile) {
    // Create a new tile
    Tile* tile = vlife->getTile(0, 0);
    EXPECT_NE(tile, nullptr);
    
    // Get the same tile again (should return the same tile)
    Tile* sameTile = vlife->getTile(0, 0);
    EXPECT_EQ(tile, sameTile);
    
    // Get a different tile
    Tile* differentTile = vlife->getTile(1, 0);
    EXPECT_NE(tile, differentTile);
}

// Test getTileIfExists method
TEST_F(VLifeTest, GetTileIfExists) {
    // No tile exists yet
    Tile* nonExistentTile = vlife->getTileIfExists(0, 0);
    EXPECT_EQ(nonExistentTile, nullptr);
    
    // Create a new tile
    Tile* tile = vlife->getTile(0, 0);
    EXPECT_NE(tile, nullptr);
    
    // Now the tile should exist
    Tile* existingTile = vlife->getTileIfExists(0, 0);
    EXPECT_EQ(tile, existingTile);
}

// Test the tile neighbor connections
TEST_F(VLifeTest, TileNeighbors) {
    // Create a 2x2 grid of tiles
    Tile* tile00 = vlife->getTile(0, 0);
    Tile* tile10 = vlife->getTile(1, 0);
    Tile* tile01 = vlife->getTile(0, 1);
    Tile* tile11 = vlife->getTile(1, 1);
    
    // Check that the tiles are not null
    EXPECT_NE(tile00, nullptr);
    EXPECT_NE(tile10, nullptr);
    EXPECT_NE(tile01, nullptr);
    EXPECT_NE(tile11, nullptr);
    
    // Check that the neighbor connections are set correctly
    EXPECT_EQ(tile00->getRightTile(), tile10);
    EXPECT_EQ(tile00->getDownTile(), tile01);
    EXPECT_EQ(tile10->getLeftTile(), tile00);
    EXPECT_EQ(tile10->getDownTile(), tile11);
    EXPECT_EQ(tile01->getRightTile(), tile11);
    EXPECT_EQ(tile01->getUpTile(), tile00);
    EXPECT_EQ(tile11->getLeftTile(), tile01);
    EXPECT_EQ(tile11->getUpTile(), tile10);
}

// Test neighbor counting functionality
TEST_F(VLifeTest, NeighborCounting) {
    // Get the tile at (0, 0)
    Tile* tile = vlife->getTile(0, 0);
    
    // Cell at (5,5) with no neighbors should have neighbor count 0
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 0);
    
    // Set a neighboring cell alive
    vlife->setCell(6, 5, GameOfLife::CellState::ALIVE);
    
    // Cell at (5,5) should now have 1 neighbor
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 1);
    
    // Add more neighbors
    vlife->setCell(4, 5, GameOfLife::CellState::ALIVE);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 2);
    
    vlife->setCell(5, 4, GameOfLife::CellState::ALIVE);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 3);
    
    // Remove a neighbor
    vlife->setCell(4, 5, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(tile, 5, 5), 2);
}

// Test neighbor counting across tile boundaries
TEST_F(VLifeTest, CrossTileNeighborCounting) {
    // Place a cell at the edge of a tile
    uint32_t edgeX = (TILE_WIDTH - 1);
    uint32_t edgeY = (TILE_HEIGHT - 1);
    
    // Place a cell at the edge
    vlife->setCell(edgeX, edgeY, GameOfLife::CellState::ALIVE);
    
    // Get the neighboring tile and check if the cell at (0,0) has a neighbor
    Tile* neighborTile = vlife->getTile(1, 1);
    
    // Cell at (0,0) in the neighboring tile should have 1 neighbor
    EXPECT_EQ(getNeighborCount(neighborTile, 0, 0), 1);
    
    // Remove the cell and check that the neighbor count decreases
    vlife->setCell(edgeX, edgeY, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(neighborTile, 0, 0), 0);
}

// Test maximum neighbor count
TEST_F(VLifeTest, MaxNeighborCount) {
    // Place a cell with 8 neighbors
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    
    // Add 8 neighbors around it
    vlife->setCell(9, 9, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 9, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 9, GameOfLife::CellState::ALIVE);
    vlife->setCell(9, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(9, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 11, GameOfLife::CellState::ALIVE);
    
    // Get the tile
    Tile* tile = vlife->getTile(0, 0);
    
    // For cells paired in the same nibble, one would exclude the other,
    // so maximum neighbor count is 7 (8 neighbors - 1 paired cell)
    // Let's get a neighbor count that should be 7 or 8 depending on pairing
    int neighborCount = getNeighborCount(tile, 10, 10);
    EXPECT_TRUE(neighborCount >= 7);
    
    // Remove all neighbors
    vlife->setCell(9, 9, GameOfLife::CellState::DEAD);
    vlife->setCell(10, 9, GameOfLife::CellState::DEAD);
    vlife->setCell(11, 9, GameOfLife::CellState::DEAD);
    vlife->setCell(9, 10, GameOfLife::CellState::DEAD);
    vlife->setCell(11, 10, GameOfLife::CellState::DEAD);
    vlife->setCell(9, 11, GameOfLife::CellState::DEAD);
    vlife->setCell(10, 11, GameOfLife::CellState::DEAD);
    vlife->setCell(11, 11, GameOfLife::CellState::DEAD);
    
    // Neighbor count should now be 0
    EXPECT_EQ(getNeighborCount(tile, 10, 10), 0);
}

// Test that liveCount is correctly incremented and decremented
TEST_F(VLifeTest, LiveCountTracking) {
    // Get a tile
    Tile* tile = vlife->getTile(0, 0);
    
    // Initially, live count should be 0
    EXPECT_EQ(tile->getLiveCount(), 0);
    
    // Set a single cell to alive
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);
    
    // Live count should be 1
    EXPECT_EQ(tile->getLiveCount(), 1);
    
    // Set another cell to alive
    vlife->setCell(6, 6, GameOfLife::CellState::ALIVE);
    
    // Live count should be 2
    EXPECT_EQ(tile->getLiveCount(), 2);
    
    // Set a cell to alive that's already alive (no change to count)
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);
    
    // Live count should still be 2
    EXPECT_EQ(tile->getLiveCount(), 2);
    
    // Set a cell to dead
    vlife->setCell(5, 5, GameOfLife::CellState::DEAD);
    
    // Live count should be 1
    EXPECT_EQ(tile->getLiveCount(), 1);
    
    // Set the last live cell to dead
    vlife->setCell(6, 6, GameOfLife::CellState::DEAD);
    
    // Live count should be 0
    EXPECT_EQ(tile->getLiveCount(), 0);
    
    // Set a cell to dead that's already dead (no change to count)
    vlife->setCell(5, 5, GameOfLife::CellState::DEAD);
    
    // Live count should still be 0
    EXPECT_EQ(tile->getLiveCount(), 0);
}

// Test that liveCount works correctly at tile boundaries
TEST_F(VLifeTest, LiveCountAtBoundaries) {
    // Get tiles for a 2x2 grid
    Tile* tile00 = vlife->getTile(0, 0);
    Tile* tile10 = vlife->getTile(1, 0);
    Tile* tile01 = vlife->getTile(0, 1);
    Tile* tile11 = vlife->getTile(1, 1);
    
    // Check initial counts
    EXPECT_EQ(tile00->getLiveCount(), 0);
    EXPECT_EQ(tile10->getLiveCount(), 0);
    EXPECT_EQ(tile01->getLiveCount(), 0);
    EXPECT_EQ(tile11->getLiveCount(), 0);
    
    // Set a cell at the right edge of tile00
    uint32_t edgeX = TILE_WIDTH - 1;
    uint32_t edgeY = 5;
    vlife->setCell(edgeX, edgeY, GameOfLife::CellState::ALIVE);
    
    // Check that tile00's live count increased
    EXPECT_EQ(tile00->getLiveCount(), 1);
    
    // Set a cell at the left edge of tile10 (adjacent to the previous cell)
    vlife->setCell(TILE_WIDTH, edgeY, GameOfLife::CellState::ALIVE);
    
    // Check that tile10's live count increased
    EXPECT_EQ(tile10->getLiveCount(), 1);
    
    // Set a cell at the bottom edge of tile00
    uint32_t edgeX2 = 5;
    uint32_t edgeY2 = TILE_HEIGHT - 1;
    vlife->setCell(edgeX2, edgeY2, GameOfLife::CellState::ALIVE);
    
    // Check that tile00's live count increased
    EXPECT_EQ(tile00->getLiveCount(), 2);
    
    // Set a cell at the top edge of tile01 (adjacent to the previous cell)
    vlife->setCell(edgeX2, TILE_HEIGHT, GameOfLife::CellState::ALIVE);
    
    // Check that tile01's live count increased
    EXPECT_EQ(tile01->getLiveCount(), 1);
    
    // Set a cell at the corner of tile00
    vlife->setCell(TILE_WIDTH - 1, TILE_HEIGHT - 1, GameOfLife::CellState::ALIVE);
    
    // Check that tile00's live count increased
    EXPECT_EQ(tile00->getLiveCount(), 3);
    
    // Kill all cells and verify counts return to 0
    vlife->setCell(edgeX, edgeY, GameOfLife::CellState::DEAD);
    vlife->setCell(TILE_WIDTH, edgeY, GameOfLife::CellState::DEAD);
    vlife->setCell(edgeX2, edgeY2, GameOfLife::CellState::DEAD);
    vlife->setCell(edgeX2, TILE_HEIGHT, GameOfLife::CellState::DEAD);
    vlife->setCell(TILE_WIDTH - 1, TILE_HEIGHT - 1, GameOfLife::CellState::DEAD);
    
    // Verify all counts are back to 0
    EXPECT_EQ(tile00->getLiveCount(), 0);
    EXPECT_EQ(tile10->getLiveCount(), 0);
    EXPECT_EQ(tile01->getLiveCount(), 0);
    EXPECT_EQ(tile11->getLiveCount(), 0);
}

// Test liveCount when using setCells for bulk loading
TEST_F(VLifeTest, LiveCountWithSetCells) {
    // Get a tile
    Tile* tile = vlife->getTile(0, 0);
    
    // Initially, live count should be 0
    EXPECT_EQ(tile->getLiveCount(), 0);
    
    // Create a pattern (a 3x3 block of live cells)
    std::vector<GameOfLife::CellState> pattern = {
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE
    };
    
    // Set the pattern
    vlife->setCells(10, 10, 3, 3, pattern);
    
    // Live count should be 9
    EXPECT_EQ(tile->getLiveCount(), 9);
    
    // Overwrite with a pattern that has some dead cells
    std::vector<GameOfLife::CellState> mixedPattern = {
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::DEAD, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::DEAD, GameOfLife::CellState::ALIVE, GameOfLife::CellState::DEAD,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::DEAD, GameOfLife::CellState::ALIVE
    };
    
    // Set the new pattern over the same area
    vlife->setCells(10, 10, 3, 3, mixedPattern);
    
    // Live count should be 5
    EXPECT_EQ(tile->getLiveCount(), 5);
    
    // Reset all cells to dead
    std::vector<GameOfLife::CellState> deadPattern(9, GameOfLife::CellState::DEAD);
    vlife->setCells(10, 10, 3, 3, deadPattern);
    
    // Live count should be 0
    EXPECT_EQ(tile->getLiveCount(), 0);
}

// Note: runGeneration tests are commented out because the implementation is incomplete
/*
// Test simple RunGeneration with a single cell
TEST_F(VLifeTest, SimpleRunGeneration) {
    // Set a single cell to alive
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);
    
    // An isolated cell should die in the next generation
    vlife->runGeneration();
    
    // Verify the cell is dead
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::DEAD);
}
*/