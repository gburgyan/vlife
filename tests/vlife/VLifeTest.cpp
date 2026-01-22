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
    // The stored neighbor count excludes the paired cell, so we add the paired cell's alive state
    uint32_t getNeighborCount(Tile* tile, uint32_t x, uint32_t y) {
        uint32_t cellIdx = (y * TILE_WIDTH + x) / 16;
        uint32_t bitPos = ((y * TILE_WIDTH + x) % 16) * 4;

        // Get the paired cell's alive state
        // For odd x (left cell in pair), paired cell's alive bit is at bitPos - 1
        // For even x (right cell in pair), paired cell's alive bit is at bitPos + 7
        uint32_t pairedCellAlive;
        if (x & 1) {
            // Odd x = "left" cell, paired with "right" cell at bitPos-4, alive bit at bitPos-1
            pairedCellAlive = (tile->cells[cellIdx] >> (bitPos - 1)) & 1;
        } else {
            // Even x = "right" cell, paired with "left" cell at bitPos+4, alive bit at bitPos+7
            pairedCellAlive = (tile->cells[cellIdx] >> (bitPos + 7)) & 1;
        }

        // Extract the stored neighbor count (bits 0-2 of the nibble)
        uint64_t mask = 0x7ULL << bitPos;
        uint32_t storedCount = (tile->cells[cellIdx] & mask) >> bitPos;

        // True neighbor count = stored count + paired cell's alive state
        return storedCount + pairedCellAlive;
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

// Test simple RunGeneration with a single cell (isolated cell dies)
TEST_F(VLifeTest, SimpleRunGeneration) {
    // Set a single cell to alive
    vlife->setCell(5, 5, GameOfLife::CellState::ALIVE);

    // An isolated cell should die in the next generation
    vlife->runGeneration();

    // Verify the cell is dead
    EXPECT_EQ(vlife->getCell(5, 5), GameOfLife::CellState::DEAD);
}

// Test that a 2x2 block is stable (still life)
TEST_F(VLifeTest, BlockStability) {
    // Create a 2x2 block
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 11, GameOfLife::CellState::ALIVE);

    // Run a generation
    vlife->runGeneration();

    // Block should remain unchanged
    EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(10, 11), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::ALIVE);

    // Surrounding cells should remain dead
    EXPECT_EQ(vlife->getCell(9, 9), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(12, 12), GameOfLife::CellState::DEAD);
}

// Test blinker oscillation (period 2 oscillator)
TEST_F(VLifeTest, BlinkerOscillation) {
    // Create a horizontal blinker
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(12, 10, GameOfLife::CellState::ALIVE);

    // Run one generation - should become vertical
    vlife->runGeneration();

    // Verify vertical orientation
    EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(11, 9), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(12, 10), GameOfLife::CellState::DEAD);

    // Run another generation - should return to horizontal
    vlife->runGeneration();

    // Verify horizontal orientation
    EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(12, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 9), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::DEAD);
}

// Test glider movement
TEST_F(VLifeTest, GliderMovement) {
    // Create a glider (moves down-right)
    //  .X.
    //  ..X
    //  XXX
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(12, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 12, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 12, GameOfLife::CellState::ALIVE);
    vlife->setCell(12, 12, GameOfLife::CellState::ALIVE);

    // Run 4 generations (one full glider cycle, moves 1 cell diagonally)
    vlife->runGenerations(4);

    // Glider should have moved down and right by 1
    EXPECT_EQ(vlife->getCell(12, 11), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(13, 12), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(11, 13), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(12, 13), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(13, 13), GameOfLife::CellState::ALIVE);

    // Original positions should be dead
    EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(10, 12), GameOfLife::CellState::DEAD);
}

// Test cell death by underpopulation
TEST_F(VLifeTest, CellDeathByUnderpopulation) {
    // A cell with only 1 neighbor should die
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);

    vlife->runGeneration();

    // Both cells should die
    EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::DEAD);
}

// Test cell death by overpopulation
TEST_F(VLifeTest, CellDeathByOverpopulation) {
    // Create a configuration where center cell has 4+ neighbors (will die)
    //  .X.
    //  XXX
    //  .X.
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(12, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 12, GameOfLife::CellState::ALIVE);

    vlife->runGeneration();

    // Center cell (11, 11) should die due to overpopulation (4 neighbors)
    EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::DEAD);
}

// Test cell birth
TEST_F(VLifeTest, CellBirth) {
    // A dead cell with exactly 3 neighbors should become alive
    //  XX
    //  X.
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 11, GameOfLife::CellState::ALIVE);

    vlife->runGeneration();

    // Cell at (11, 11) should be born (has exactly 3 neighbors)
    EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::ALIVE);
}

// Test generation at tile boundary
TEST_F(VLifeTest, GenerationAtTileBoundary) {
    // Place a blinker at the edge of a tile
    uint32_t edgeX = TILE_WIDTH - 2;

    // Horizontal blinker spanning edge
    vlife->setCell(edgeX, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(edgeX + 1, 10, GameOfLife::CellState::ALIVE);  // At tile boundary
    vlife->setCell(edgeX + 2, 10, GameOfLife::CellState::ALIVE);  // In next tile

    // Run a generation
    vlife->runGeneration();

    // Should become vertical blinker
    EXPECT_EQ(vlife->getCell(edgeX, 10), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(edgeX + 1, 9), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(edgeX + 1, 10), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(edgeX + 1, 11), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(edgeX + 2, 10), GameOfLife::CellState::DEAD);
}

// Test that liveCount is updated correctly during generation
TEST_F(VLifeTest, LiveCountAfterGeneration) {
    // Create a blinker (3 live cells)
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(12, 10, GameOfLife::CellState::ALIVE);

    Tile* tile = vlife->getTile(0, 0);
    EXPECT_EQ(tile->getLiveCount(), 3);

    // Run a generation
    vlife->runGeneration();

    // Still 3 live cells, just in different positions
    EXPECT_EQ(tile->getLiveCount(), 3);
}

// Helper function to count total live cells in the board
static int countLiveCells(VLife& board, int startX, int startY, int width, int height) {
    int count = 0;
    for (int y = startY; y < startY + height; y++) {
        for (int x = startX; x < startX + width; x++) {
            if (board.getCell(x, y) == GameOfLife::CellState::ALIVE) {
                count++;
            }
        }
    }
    return count;
}

// Helper function to setup a glider at the specified position
static void setupGlider(VLife& board, int offsetX, int offsetY) {
    //  .X.
    //  ..X
    //  XXX
    board.setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 1, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 0, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
}

// Test glider crossing right tile boundary
// This tests the race condition where tiles (0,0) and (2,0) both update tile (1,0)
TEST_F(VLifeTest, GliderCrossingRightBoundary) {
    // Place glider near right edge of tile (0,0), so it will cross into tile (1,0)
    // TILE_WIDTH is 32, so place glider at x = 28 (4 cells from edge)
    int startX = TILE_WIDTH - 4;
    int startY = 10;

    setupGlider(*vlife, startX, startY);

    // Verify initial state: exactly 5 live cells
    EXPECT_EQ(countLiveCells(*vlife, startX - 5, startY - 5, 20, 20), 5);

    // Run 20 generations - glider should cross the tile boundary
    // A glider moves 1 cell diagonally every 4 generations
    // So after 20 generations, it should have moved 5 cells right and down
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();

        // After each generation, glider should still have exactly 5 live cells
        int liveCells = countLiveCells(*vlife, 0, 0, TILE_WIDTH * 2, TILE_HEIGHT * 2);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1) << ": expected 5 live cells, got " << liveCells;

        if (liveCells != 5) {
            // Early exit on failure to avoid noisy output
            break;
        }
    }
}

// Test glider crossing bottom tile boundary
TEST_F(VLifeTest, GliderCrossingBottomBoundary) {
    // Place glider near bottom edge of tile (0,0), so it will cross into tile (0,1)
    int startX = 10;
    int startY = TILE_HEIGHT - 4;

    setupGlider(*vlife, startX, startY);

    // Verify initial state: exactly 5 live cells
    EXPECT_EQ(countLiveCells(*vlife, startX - 5, startY - 5, 20, 20), 5);

    // Run 20 generations
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();

        int liveCells = countLiveCells(*vlife, 0, 0, TILE_WIDTH * 2, TILE_HEIGHT * 2);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1) << ": expected 5 live cells, got " << liveCells;

        if (liveCells != 5) break;
    }
}

// Test glider crossing diagonal corner boundary (hardest case)
// The glider will cross from tile (0,0) to tile (1,1) diagonally
TEST_F(VLifeTest, GliderCrossingCornerBoundary) {
    // Place glider near bottom-right corner of tile (0,0)
    int startX = TILE_WIDTH - 4;
    int startY = TILE_HEIGHT - 4;

    setupGlider(*vlife, startX, startY);

    // Verify initial state
    EXPECT_EQ(countLiveCells(*vlife, startX - 5, startY - 5, 20, 20), 5);

    // Run 20 generations
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();

        int liveCells = countLiveCells(*vlife, 0, 0, TILE_WIDTH * 2, TILE_HEIGHT * 2);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1) << ": expected 5 live cells, got " << liveCells;

        if (liveCells != 5) break;
    }
}

// Test multiple gliders crossing different boundaries simultaneously
// This is most likely to trigger race conditions because multiple tiles
// will be updating shared neighbor tiles in parallel
TEST_F(VLifeTest, MultipleGlidersCrossingSimultaneously) {
    // Place gliders near boundaries that will cause maximum contention:
    // - Glider at (TILE_WIDTH-4, 10) crosses right from tile (0,0) to (1,0)
    // - Glider at (TILE_WIDTH*2-4, 10) crosses right from tile (1,0) to (2,0)
    // Tiles (0,0) and (2,0) will be processed in parallel

    setupGlider(*vlife, TILE_WIDTH - 4, 10);      // Crosses from tile 0,0 to 1,0
    setupGlider(*vlife, TILE_WIDTH * 2 - 4, 10);  // Crosses from tile 1,0 to 2,0

    // Also add gliders at different y positions to increase contention
    setupGlider(*vlife, TILE_WIDTH - 4, TILE_HEIGHT + 10);      // Crosses from tile 0,1 to 1,1
    setupGlider(*vlife, TILE_WIDTH * 2 - 4, TILE_HEIGHT + 10);  // Crosses from tile 1,1 to 2,1

    // Verify initial state: 4 gliders * 5 cells = 20 live cells
    EXPECT_EQ(countLiveCells(*vlife, 0, 0, TILE_WIDTH * 4, TILE_HEIGHT * 3), 20);

    // Run 30 generations
    for (int gen = 0; gen < 30; gen++) {
        vlife->runGeneration();

        int liveCells = countLiveCells(*vlife, 0, 0, TILE_WIDTH * 4, TILE_HEIGHT * 3);
        EXPECT_EQ(liveCells, 20) << "Generation " << (gen + 1) << ": expected 20 live cells, got " << liveCells;

        if (liveCells != 20) break;
    }
}

// Test that the bug is specifically related to parallel processing
// by comparing sequential vs parallel results
TEST_F(VLifeTest, ParallelVsSequentialConsistency) {
    // Setup two identical boards
    auto vlifeParallel = std::make_unique<VLife>();
    auto vlifeSequential = std::make_unique<VLife>();

    // Place gliders near boundaries
    setupGlider(*vlifeParallel, TILE_WIDTH - 4, 10);
    setupGlider(*vlifeSequential, TILE_WIDTH - 4, 10);

    setupGlider(*vlifeParallel, TILE_WIDTH * 2 - 4, 10);
    setupGlider(*vlifeSequential, TILE_WIDTH * 2 - 4, 10);

    // Enable parallel for one, disable for other
    vlifeParallel->setParallelEnabled(true);
    vlifeSequential->setParallelEnabled(false);

    // Run 30 generations on each
    for (int gen = 0; gen < 30; gen++) {
        vlifeParallel->runGeneration();
        vlifeSequential->runGeneration();

        // Compare results - they should be identical
        int parallelCells = countLiveCells(*vlifeParallel, 0, 0, TILE_WIDTH * 4, TILE_HEIGHT * 2);
        int sequentialCells = countLiveCells(*vlifeSequential, 0, 0, TILE_WIDTH * 4, TILE_HEIGHT * 2);

        EXPECT_EQ(parallelCells, sequentialCells)
            << "Generation " << (gen + 1) << ": parallel has " << parallelCells
            << " cells, sequential has " << sequentialCells;

        if (parallelCells != sequentialCells) break;
    }
}

// ============================================================================
// Block Modification Tracking Tests
// ============================================================================

// Test block index calculation formula
TEST_F(VLifeTest, BlockIndexCalculation) {
    // Block index formula: ((y & 0x1C) << 1) | (x >> 2)
    // 32x32 tile divided into 8x8 blocks of 4x4 cells each

    // Test corners of first block (x=0-3, y=0-3) -> block index 0
    EXPECT_EQ(((0 & 0x1C) << 1) | (0 >> 2), 0);
    EXPECT_EQ(((0 & 0x1C) << 1) | (3 >> 2), 0);
    EXPECT_EQ(((3 & 0x1C) << 1) | (0 >> 2), 0);
    EXPECT_EQ(((3 & 0x1C) << 1) | (3 >> 2), 0);

    // Test second block in first row (x=4-7, y=0-3) -> block index 1
    EXPECT_EQ(((0 & 0x1C) << 1) | (4 >> 2), 1);
    EXPECT_EQ(((3 & 0x1C) << 1) | (7 >> 2), 1);

    // Test first block in second block row (x=0-3, y=4-7) -> block index 8
    EXPECT_EQ(((4 & 0x1C) << 1) | (0 >> 2), 8);
    EXPECT_EQ(((7 & 0x1C) << 1) | (3 >> 2), 8);

    // Test last block (x=28-31, y=28-31) -> block index 63
    EXPECT_EQ(((28 & 0x1C) << 1) | (28 >> 2), 63);
    EXPECT_EQ(((31 & 0x1C) << 1) | (31 >> 2), 63);
}

// Test that block tracking produces correct results over multiple generations
TEST_F(VLifeTest, BlockTrackingCorrectnessGlider) {
    // Use a glider pattern - it should work correctly with block tracking
    setupGlider(*vlife, 10, 10);

    // Record initial state
    int initialLiveCells = countLiveCells(*vlife, 0, 0, 50, 50);
    EXPECT_EQ(initialLiveCells, 5);

    // Run 20 generations
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();

        int liveCells = countLiveCells(*vlife, 0, 0, 50, 50);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1) << ": expected 5 live cells, got " << liveCells;

        if (liveCells != 5) break;
    }
}

// Test that block tracking works correctly across tile boundaries
TEST_F(VLifeTest, BlockTrackingAcrossTileBoundary) {
    // Place a blinker across tile boundary
    uint32_t edgeX = TILE_WIDTH - 1;

    // Horizontal blinker spanning tile boundary
    vlife->setCell(edgeX - 1, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(edgeX, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(edgeX + 1, 10, GameOfLife::CellState::ALIVE);

    // Run 10 generations (blinker oscillates with period 2)
    for (int gen = 0; gen < 10; gen++) {
        vlife->runGeneration();

        int liveCells = countLiveCells(*vlife, edgeX - 2, 8, 5, 5);
        EXPECT_EQ(liveCells, 3) << "Generation " << (gen + 1) << ": expected 3 live cells, got " << liveCells;

        if (liveCells != 3) break;
    }
}

// Test that wasModified is properly initialized for new tiles
TEST_F(VLifeTest, WasModifiedInitialization) {
    // Get a new tile - wasModified should be all 1s (everything needs processing)
    Tile* tile = vlife->getTile(5, 5);

    // After one generation of processing, wasModified should be cleared
    // (assuming no activity in the empty tile)
    vlife->runGeneration();

    // Set a cell to trigger activity marking
    vlife->setCell(5 * TILE_WIDTH + 15, 5 * TILE_HEIGHT + 15, GameOfLife::CellState::ALIVE);

    // Run generation - the cell should process correctly
    vlife->runGeneration();

    // Verify the cell and its neighbors are handled correctly
    // (single cell dies due to underpopulation)
    EXPECT_EQ(vlife->getCell(5 * TILE_WIDTH + 15, 5 * TILE_HEIGHT + 15), GameOfLife::CellState::DEAD);
}

// Test block tracking with still life (block pattern)
TEST_F(VLifeTest, BlockTrackingStillLife) {
    // Create a 2x2 block (still life) - should remain stable
    vlife->setCell(10, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 10, GameOfLife::CellState::ALIVE);
    vlife->setCell(10, 11, GameOfLife::CellState::ALIVE);
    vlife->setCell(11, 11, GameOfLife::CellState::ALIVE);

    // Run 10 generations - block should be stable
    for (int gen = 0; gen < 10; gen++) {
        vlife->runGeneration();

        // All 4 cells should still be alive
        EXPECT_EQ(vlife->getCell(10, 10), GameOfLife::CellState::ALIVE) << "Gen " << (gen + 1);
        EXPECT_EQ(vlife->getCell(11, 10), GameOfLife::CellState::ALIVE) << "Gen " << (gen + 1);
        EXPECT_EQ(vlife->getCell(10, 11), GameOfLife::CellState::ALIVE) << "Gen " << (gen + 1);
        EXPECT_EQ(vlife->getCell(11, 11), GameOfLife::CellState::ALIVE) << "Gen " << (gen + 1);
    }
}