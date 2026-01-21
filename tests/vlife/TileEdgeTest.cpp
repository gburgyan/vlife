#include <gtest/gtest.h>
#include <memory>
#include "../../src/vlife/VLife.h"
#include "../../src/vlife/Tile.h"

// Test suite for tile boundary edge cases in VLife
// Focuses on scenarios where tile contents and neighbor information
// spill over into adjacent tiles

class TileEdgeTest : public ::testing::Test {
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

    // Helper: count live cells in region
    int countLiveCells(int startX, int startY, int width, int height) {
        int count = 0;
        for (int y = startY; y < startY + height; y++) {
            for (int x = startX; x < startX + width; x++) {
                if (vlife->getCell(x, y) == GameOfLife::CellState::ALIVE) {
                    count++;
                }
            }
        }
        return count;
    }

    // Helper: setup standard glider at offset
    void setupGlider(int offsetX, int offsetY) {
        //  .X.
        //  ..X
        //  XXX
        vlife->setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 2, offsetY + 1, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 0, offsetY + 2, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 1, offsetY + 2, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 2, offsetY + 2, GameOfLife::CellState::ALIVE);
    }

    // Helper: setup 2x2 block at offset
    void setupBlock(int offsetX, int offsetY) {
        vlife->setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 1, offsetY + 1, GameOfLife::CellState::ALIVE);
    }

    // Helper: setup horizontal blinker at offset
    void setupHorizontalBlinker(int offsetX, int offsetY) {
        vlife->setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 1, offsetY, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX + 2, offsetY, GameOfLife::CellState::ALIVE);
    }

    // Helper: setup vertical blinker at offset
    void setupVerticalBlinker(int offsetX, int offsetY) {
        vlife->setCell(offsetX, offsetY, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX, offsetY + 1, GameOfLife::CellState::ALIVE);
        vlife->setCell(offsetX, offsetY + 2, GameOfLife::CellState::ALIVE);
    }
};

// =============================================================================
// Category 1: Neighbor Count Propagation at Edges (8 tests)
// =============================================================================

// Test that setting a cell at x=0 updates neighbor count in the left tile's x=31
TEST_F(TileEdgeTest, SetCellLeftEdge_NeighborCountPropagation) {
    // Ensure left tile exists
    Tile* leftTile = vlife->getTile(-1, 0);
    Tile* centerTile = vlife->getTile(0, 0);

    // Set a cell at x=0 (left edge of tile 0,0)
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);

    // Cell at x=31, y=15 in left tile should have 1 neighbor
    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 1);

    // Diagonal neighbors in left tile should also be updated
    EXPECT_EQ(getNeighborCount(leftTile, 31, 14), 1);
    EXPECT_EQ(getNeighborCount(leftTile, 31, 16), 1);

    // Kill the cell
    vlife->setCell(0, 15, GameOfLife::CellState::DEAD);

    // All neighbor counts should return to 0
    EXPECT_EQ(getNeighborCount(leftTile, 31, 14), 0);
    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 0);
    EXPECT_EQ(getNeighborCount(leftTile, 31, 16), 0);
}

// Test that setting a cell at x=31 updates neighbor count in the right tile's x=0
TEST_F(TileEdgeTest, SetCellRightEdge_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* rightTile = vlife->getTile(1, 0);

    // Set a cell at x=31 (right edge of tile 0,0)
    vlife->setCell(TILE_WIDTH - 1, 15, GameOfLife::CellState::ALIVE);

    // Cell at x=0, y=15 in right tile should have 1 neighbor
    EXPECT_EQ(getNeighborCount(rightTile, 0, 15), 1);

    // Diagonal neighbors in right tile should also be updated
    EXPECT_EQ(getNeighborCount(rightTile, 0, 14), 1);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 16), 1);

    // Kill the cell
    vlife->setCell(TILE_WIDTH - 1, 15, GameOfLife::CellState::DEAD);

    // All neighbor counts should return to 0
    EXPECT_EQ(getNeighborCount(rightTile, 0, 14), 0);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 15), 0);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 16), 0);
}

// Test that setting a cell at y=0 updates neighbor count in the up tile's y=31
TEST_F(TileEdgeTest, SetCellTopEdge_NeighborCountPropagation) {
    Tile* upTile = vlife->getTile(0, -1);
    Tile* centerTile = vlife->getTile(0, 0);

    // Set a cell at y=0 (top edge of tile 0,0)
    vlife->setCell(15, 0, GameOfLife::CellState::ALIVE);

    // Cell at x=15, y=31 in up tile should have 1 neighbor
    EXPECT_EQ(getNeighborCount(upTile, 15, TILE_HEIGHT - 1), 1);

    // Diagonal neighbors in up tile should also be updated
    EXPECT_EQ(getNeighborCount(upTile, 14, TILE_HEIGHT - 1), 1);
    EXPECT_EQ(getNeighborCount(upTile, 16, TILE_HEIGHT - 1), 1);

    // Kill the cell
    vlife->setCell(15, 0, GameOfLife::CellState::DEAD);

    // All neighbor counts should return to 0
    EXPECT_EQ(getNeighborCount(upTile, 14, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(upTile, 15, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(upTile, 16, TILE_HEIGHT - 1), 0);
}

// Test that setting a cell at y=31 updates neighbor count in the down tile's y=0
TEST_F(TileEdgeTest, SetCellBottomEdge_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* downTile = vlife->getTile(0, 1);

    // Set a cell at y=31 (bottom edge of tile 0,0)
    vlife->setCell(15, TILE_HEIGHT - 1, GameOfLife::CellState::ALIVE);

    // Cell at x=15, y=0 in down tile should have 1 neighbor
    EXPECT_EQ(getNeighborCount(downTile, 15, 0), 1);

    // Diagonal neighbors in down tile should also be updated
    EXPECT_EQ(getNeighborCount(downTile, 14, 0), 1);
    EXPECT_EQ(getNeighborCount(downTile, 16, 0), 1);

    // Kill the cell
    vlife->setCell(15, TILE_HEIGHT - 1, GameOfLife::CellState::DEAD);

    // All neighbor counts should return to 0
    EXPECT_EQ(getNeighborCount(downTile, 14, 0), 0);
    EXPECT_EQ(getNeighborCount(downTile, 15, 0), 0);
    EXPECT_EQ(getNeighborCount(downTile, 16, 0), 0);
}

// Test top-left corner affects 3 adjacent tiles
TEST_F(TileEdgeTest, SetCellTopLeftCorner_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);
    Tile* upTile = vlife->getTile(0, -1);
    Tile* diagTile = vlife->getTile(-1, -1);

    // Set a cell at (0, 0) - top-left corner
    vlife->setCell(0, 0, GameOfLife::CellState::ALIVE);

    // Check diagonal tile (corner neighbor)
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 1);

    // Check left tile (horizontal neighbor)
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, 0), 1);
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, 1), 1);

    // Check up tile (vertical neighbor)
    EXPECT_EQ(getNeighborCount(upTile, 0, TILE_HEIGHT - 1), 1);
    EXPECT_EQ(getNeighborCount(upTile, 1, TILE_HEIGHT - 1), 1);

    // Kill the cell and verify cleanup
    vlife->setCell(0, 0, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, 0), 0);
    EXPECT_EQ(getNeighborCount(upTile, 0, TILE_HEIGHT - 1), 0);
}

// Test top-right corner affects 3 adjacent tiles
TEST_F(TileEdgeTest, SetCellTopRightCorner_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* rightTile = vlife->getTile(1, 0);
    Tile* upTile = vlife->getTile(0, -1);
    Tile* diagTile = vlife->getTile(1, -1);

    // Set a cell at (31, 0) - top-right corner
    vlife->setCell(TILE_WIDTH - 1, 0, GameOfLife::CellState::ALIVE);

    // Check diagonal tile (corner neighbor)
    EXPECT_EQ(getNeighborCount(diagTile, 0, TILE_HEIGHT - 1), 1);

    // Check right tile (horizontal neighbor)
    EXPECT_EQ(getNeighborCount(rightTile, 0, 0), 1);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 1), 1);

    // Check up tile (vertical neighbor)
    EXPECT_EQ(getNeighborCount(upTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 1);
    EXPECT_EQ(getNeighborCount(upTile, TILE_WIDTH - 2, TILE_HEIGHT - 1), 1);

    // Kill the cell and verify cleanup
    vlife->setCell(TILE_WIDTH - 1, 0, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(diagTile, 0, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 0), 0);
    EXPECT_EQ(getNeighborCount(upTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 0);
}

// Test bottom-left corner affects 3 adjacent tiles
TEST_F(TileEdgeTest, SetCellBottomLeftCorner_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);
    Tile* downTile = vlife->getTile(0, 1);
    Tile* diagTile = vlife->getTile(-1, 1);

    // Set a cell at (0, 31) - bottom-left corner
    vlife->setCell(0, TILE_HEIGHT - 1, GameOfLife::CellState::ALIVE);

    // Check diagonal tile (corner neighbor)
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, 0), 1);

    // Check left tile (horizontal neighbor)
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 1);
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, TILE_HEIGHT - 2), 1);

    // Check down tile (vertical neighbor)
    EXPECT_EQ(getNeighborCount(downTile, 0, 0), 1);
    EXPECT_EQ(getNeighborCount(downTile, 1, 0), 1);

    // Kill the cell and verify cleanup
    vlife->setCell(0, TILE_HEIGHT - 1, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, 0), 0);
    EXPECT_EQ(getNeighborCount(leftTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(downTile, 0, 0), 0);
}

// Test bottom-right corner affects 3 adjacent tiles
TEST_F(TileEdgeTest, SetCellBottomRightCorner_NeighborCountPropagation) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* rightTile = vlife->getTile(1, 0);
    Tile* downTile = vlife->getTile(0, 1);
    Tile* diagTile = vlife->getTile(1, 1);

    // Set a cell at (31, 31) - bottom-right corner
    vlife->setCell(TILE_WIDTH - 1, TILE_HEIGHT - 1, GameOfLife::CellState::ALIVE);

    // Check diagonal tile (corner neighbor)
    EXPECT_EQ(getNeighborCount(diagTile, 0, 0), 1);

    // Check right tile (horizontal neighbor)
    EXPECT_EQ(getNeighborCount(rightTile, 0, TILE_HEIGHT - 1), 1);
    EXPECT_EQ(getNeighborCount(rightTile, 0, TILE_HEIGHT - 2), 1);

    // Check down tile (vertical neighbor)
    EXPECT_EQ(getNeighborCount(downTile, TILE_WIDTH - 1, 0), 1);
    EXPECT_EQ(getNeighborCount(downTile, TILE_WIDTH - 2, 0), 1);

    // Kill the cell and verify cleanup
    vlife->setCell(TILE_WIDTH - 1, TILE_HEIGHT - 1, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(diagTile, 0, 0), 0);
    EXPECT_EQ(getNeighborCount(rightTile, 0, TILE_HEIGHT - 1), 0);
    EXPECT_EQ(getNeighborCount(downTile, TILE_WIDTH - 1, 0), 0);
}

// =============================================================================
// Category 2: Cell Pair Nibble Boundary Issues (3 tests)
// =============================================================================

// Verify paired cell neighbor count at x=0/x=1 boundary
TEST_F(TileEdgeTest, CellPairAtLeftEdge_NeighborCountCorrectness) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);

    // Set cell at x=0 (even position, paired with x=1)
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);

    // x=1 should now see x=0 as a neighbor via the paired cell mechanism
    EXPECT_EQ(getNeighborCount(centerTile, 1, 15), 1);

    // Cell at x=-1 (which is x=31 in left tile) should also see x=0 as neighbor
    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 1);

    // Now set cell at x=1 as well
    vlife->setCell(1, 15, GameOfLife::CellState::ALIVE);

    // x=0 should see x=1 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, 0, 15), 1);

    // x=2 should see x=1 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, 2, 15), 1);

    // Clean up and verify
    vlife->setCell(0, 15, GameOfLife::CellState::DEAD);
    vlife->setCell(1, 15, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(centerTile, 1, 15), 0);
    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 0);
}

// Verify paired cell neighbor count at x=30/x=31 boundary
TEST_F(TileEdgeTest, CellPairAtRightEdge_NeighborCountCorrectness) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* rightTile = vlife->getTile(1, 0);

    // Set cell at x=30 (even position, paired with x=31)
    vlife->setCell(TILE_WIDTH - 2, 15, GameOfLife::CellState::ALIVE);

    // x=31 should see x=30 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, TILE_WIDTH - 1, 15), 1);

    // x=29 should see x=30 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, TILE_WIDTH - 3, 15), 1);

    // Now set cell at x=31 as well
    vlife->setCell(TILE_WIDTH - 1, 15, GameOfLife::CellState::ALIVE);

    // x=30 should see x=31 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, TILE_WIDTH - 2, 15), 1);

    // Cell at x=32 (which is x=0 in right tile) should see x=31 as neighbor
    EXPECT_EQ(getNeighborCount(rightTile, 0, 15), 1);

    // Clean up
    vlife->setCell(TILE_WIDTH - 2, 15, GameOfLife::CellState::DEAD);
    vlife->setCell(TILE_WIDTH - 1, 15, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(centerTile, TILE_WIDTH - 1, 15), 0);
    EXPECT_EQ(getNeighborCount(rightTile, 0, 15), 0);
}

// Test both cells of pair alive at left boundary (complex interaction)
TEST_F(TileEdgeTest, BothCellsOfPairAliveAtLeftBoundary) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);

    // Set both x=0 and x=1 alive (they form a pair)
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);
    vlife->setCell(1, 15, GameOfLife::CellState::ALIVE);

    // Also set a cell at x=-1 (x=31 in left tile) to have cross-tile neighbor
    vlife->setCell(TILE_WIDTH - 1 + (-1) * TILE_WIDTH, 15, GameOfLife::CellState::ALIVE);

    // Verify neighbor counts:
    // x=0 should see x=1 and x=-1 as neighbors = 2
    EXPECT_EQ(getNeighborCount(centerTile, 0, 15), 2);

    // x=1 should see x=0 and x=2 is dead, so = 1
    EXPECT_EQ(getNeighborCount(centerTile, 1, 15), 1);

    // x=-1 (x=31 in left tile) should see x=0 as neighbor = 1 (x=-2 is dead)
    EXPECT_GE(getNeighborCount(leftTile, 31, 15), 1);

    // Clean up
    vlife->setCell(0, 15, GameOfLife::CellState::DEAD);
    vlife->setCell(1, 15, GameOfLife::CellState::DEAD);
    vlife->setCell(TILE_WIDTH - 1 + (-1) * TILE_WIDTH, 15, GameOfLife::CellState::DEAD);
}

// =============================================================================
// Category 3: Generation Changes at Boundaries (4 tests)
// =============================================================================

// Horizontal blinker at x=0,1,2 (starting at left edge)
TEST_F(TileEdgeTest, BlinkerAtLeftEdge_CrossesTileBoundary) {
    // Create horizontal blinker at x=0,1,2
    setupHorizontalBlinker(0, 15);

    // Verify initial state
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(2, 15), GameOfLife::CellState::ALIVE);

    // Run one generation - should become vertical centered at x=1
    vlife->runGeneration();

    // Verify vertical orientation
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(1, 14), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1, 16), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(2, 15), GameOfLife::CellState::DEAD);

    // Run another generation - should return to horizontal
    vlife->runGeneration();

    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(2, 15), GameOfLife::CellState::ALIVE);
}

// Blinker spanning tiles at x=30,31,32 (crosses from tile 0 to tile 1)
TEST_F(TileEdgeTest, BlinkerSpanningTileBoundary_Horizontal) {
    // Create horizontal blinker spanning boundary
    // x=30,31 are in tile 0, x=32 is in tile 1
    setupHorizontalBlinker(TILE_WIDTH - 2, 15);

    // Verify initial state
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 2, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, 15), GameOfLife::CellState::ALIVE);

    // Run one generation - should become vertical centered at x=31
    vlife->runGeneration();

    // Verify vertical orientation centered at x=31
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 2, 15), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 14), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 16), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, 15), GameOfLife::CellState::DEAD);

    // Run another generation - should return to horizontal
    vlife->runGeneration();

    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 2, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 15), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, 15), GameOfLife::CellState::ALIVE);
}

// Blinker spanning tiles vertically at y=30,31,32
TEST_F(TileEdgeTest, BlinkerSpanningTileBoundary_Vertical) {
    // Create vertical blinker spanning boundary
    // y=30,31 are in tile (0,0), y=32 is in tile (0,1)
    setupVerticalBlinker(15, TILE_HEIGHT - 2);

    // Verify initial state
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 2), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT), GameOfLife::CellState::ALIVE);

    // Run one generation - should become horizontal centered at y=31
    vlife->runGeneration();

    // Verify horizontal orientation centered at y=31
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 2), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(14, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(16, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT), GameOfLife::CellState::DEAD);

    // Run another generation - should return to vertical
    vlife->runGeneration();

    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 2), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(15, TILE_HEIGHT), GameOfLife::CellState::ALIVE);
}

// Blinker at tile corner (tests all 4 adjacent tiles)
TEST_F(TileEdgeTest, BlinkerAtCorner) {
    // Place horizontal blinker at corner, spanning from x=31 to x=33
    // x=31 in tile(0,0), x=32,33 in tile(1,0)
    // y=31 (bottom of tiles)
    setupHorizontalBlinker(TILE_WIDTH - 1, TILE_HEIGHT - 1);

    // Verify initial state
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH + 1, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);

    // Run one generation
    vlife->runGeneration();

    // Should become vertical centered at x=32
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, TILE_HEIGHT - 1), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, TILE_HEIGHT - 2), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, TILE_HEIGHT), GameOfLife::CellState::ALIVE);  // This is in tile (1,1)
    EXPECT_EQ(vlife->getCell(TILE_WIDTH + 1, TILE_HEIGHT - 1), GameOfLife::CellState::DEAD);

    // Run another generation - should return to horizontal
    vlife->runGeneration();

    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH + 1, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
}

// =============================================================================
// Category 4: Cell Birth/Death at Edges (4 tests)
// =============================================================================

// Cell born at x=0 with 3 neighbors
TEST_F(TileEdgeTest, CellBirthAtLeftEdge) {
    // Ensure both tiles exist BEFORE setting cells (required for cross-tile propagation)
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);

    // Create 3 neighbors around (0, 15) to cause birth
    // Neighbors at (-1,14), (-1,15), (-1,16) which are in the left tile
    vlife->setCell(-1, 14, GameOfLife::CellState::ALIVE);  // x=31 in tile -1
    vlife->setCell(-1, 15, GameOfLife::CellState::ALIVE);
    vlife->setCell(-1, 16, GameOfLife::CellState::ALIVE);

    // Cell at (0, 15) should be dead initially
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::DEAD);

    // Verify it has 3 neighbors
    EXPECT_EQ(getNeighborCount(centerTile, 0, 15), 3);

    // Run generation - cell should be born
    vlife->runGeneration();

    // Cell at (0, 15) should now be alive
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::ALIVE);
}

// Cell born at top-left corner (0, 0)
TEST_F(TileEdgeTest, CellBirthAtTopLeftCorner) {
    // Create 3 neighbors around (0, 0)
    // These will be at (-1,-1), (-1,0), (0,-1) - all in adjacent tiles
    vlife->setCell(-1, -1, GameOfLife::CellState::ALIVE);  // diagonal tile
    vlife->setCell(-1, 0, GameOfLife::CellState::ALIVE);   // left tile
    vlife->setCell(0, -1, GameOfLife::CellState::ALIVE);   // up tile

    // Cell at (0, 0) should be dead initially
    EXPECT_EQ(vlife->getCell(0, 0), GameOfLife::CellState::DEAD);

    // Verify it has 3 neighbors
    Tile* centerTile = vlife->getTile(0, 0);
    EXPECT_EQ(getNeighborCount(centerTile, 0, 0), 3);

    // Run generation - cell should be born
    vlife->runGeneration();

    // Cell at (0, 0) should now be alive
    EXPECT_EQ(vlife->getCell(0, 0), GameOfLife::CellState::ALIVE);
}

// Cell at left edge dies from underpopulation
TEST_F(TileEdgeTest, CellDeathAtLeftEdge_Underpopulation) {
    // Place a cell at left edge with only 1 neighbor
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);
    vlife->setCell(1, 15, GameOfLife::CellState::ALIVE);  // 1 neighbor

    // Verify initial state
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::ALIVE);

    // Run generation - both cells should die (each has only 1 neighbor)
    vlife->runGeneration();

    // Cell should be dead
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::DEAD);
    EXPECT_EQ(vlife->getCell(1, 15), GameOfLife::CellState::DEAD);
}

// Cell at left edge dies from overpopulation
TEST_F(TileEdgeTest, CellDeathAtLeftEdge_Overpopulation) {
    // Create a cross pattern around (0, 15) giving it 4 neighbors
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);  // center cell
    vlife->setCell(-1, 15, GameOfLife::CellState::ALIVE); // left neighbor
    vlife->setCell(1, 15, GameOfLife::CellState::ALIVE);  // right neighbor
    vlife->setCell(0, 14, GameOfLife::CellState::ALIVE);  // up neighbor
    vlife->setCell(0, 16, GameOfLife::CellState::ALIVE);  // down neighbor

    // Verify the center cell has 4 neighbors
    Tile* centerTile = vlife->getTile(0, 0);
    EXPECT_EQ(getNeighborCount(centerTile, 0, 15), 4);

    // Run generation - center cell should die from overpopulation
    vlife->runGeneration();

    // Center cell at (0, 15) should now be dead
    EXPECT_EQ(vlife->getCell(0, 15), GameOfLife::CellState::DEAD);
}

// =============================================================================
// Category 5: Systematic Edge Position Verification (4 tests)
// =============================================================================

// Test all 32 positions along left edge (x=0)
TEST_F(TileEdgeTest, AllLeftEdgePositions_NeighborCounts) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);

    for (uint32_t y = 0; y < TILE_HEIGHT; y++) {
        // Set a cell at left edge
        vlife->setCell(0, y, GameOfLife::CellState::ALIVE);

        // Verify neighbor count is propagated to left tile
        // Cell at x=31 in left tile should see this cell
        EXPECT_EQ(getNeighborCount(leftTile, 31, y), 1)
            << "Failed for y=" << y;

        // Clean up for next iteration
        vlife->setCell(0, y, GameOfLife::CellState::DEAD);
        EXPECT_EQ(getNeighborCount(leftTile, 31, y), 0)
            << "Cleanup failed for y=" << y;
    }
}

// Test all 32 positions along right edge (x=31)
TEST_F(TileEdgeTest, AllRightEdgePositions_NeighborCounts) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* rightTile = vlife->getTile(1, 0);

    for (uint32_t y = 0; y < TILE_HEIGHT; y++) {
        // Set a cell at right edge
        vlife->setCell(TILE_WIDTH - 1, y, GameOfLife::CellState::ALIVE);

        // Verify neighbor count is propagated to right tile
        EXPECT_EQ(getNeighborCount(rightTile, 0, y), 1)
            << "Failed for y=" << y;

        // Clean up
        vlife->setCell(TILE_WIDTH - 1, y, GameOfLife::CellState::DEAD);
        EXPECT_EQ(getNeighborCount(rightTile, 0, y), 0)
            << "Cleanup failed for y=" << y;
    }
}

// Test all 32 positions along top edge (y=0)
TEST_F(TileEdgeTest, AllTopEdgePositions_NeighborCounts) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* upTile = vlife->getTile(0, -1);

    for (uint32_t x = 0; x < TILE_WIDTH; x++) {
        // Set a cell at top edge
        vlife->setCell(x, 0, GameOfLife::CellState::ALIVE);

        // Verify neighbor count is propagated to up tile
        EXPECT_EQ(getNeighborCount(upTile, x, TILE_HEIGHT - 1), 1)
            << "Failed for x=" << x;

        // Clean up
        vlife->setCell(x, 0, GameOfLife::CellState::DEAD);
        EXPECT_EQ(getNeighborCount(upTile, x, TILE_HEIGHT - 1), 0)
            << "Cleanup failed for x=" << x;
    }
}

// Test all 32 positions along bottom edge (y=31)
TEST_F(TileEdgeTest, AllBottomEdgePositions_NeighborCounts) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* downTile = vlife->getTile(0, 1);

    for (uint32_t x = 0; x < TILE_WIDTH; x++) {
        // Set a cell at bottom edge
        vlife->setCell(x, TILE_HEIGHT - 1, GameOfLife::CellState::ALIVE);

        // Verify neighbor count is propagated to down tile
        EXPECT_EQ(getNeighborCount(downTile, x, 0), 1)
            << "Failed for x=" << x;

        // Clean up
        vlife->setCell(x, TILE_HEIGHT - 1, GameOfLife::CellState::DEAD);
        EXPECT_EQ(getNeighborCount(downTile, x, 0), 0)
            << "Cleanup failed for x=" << x;
    }
}

// =============================================================================
// Category 6: Multi-Tile Stress Tests (4 tests)
// =============================================================================

// Glider crossing left boundary (into negative tile coordinates)
TEST_F(TileEdgeTest, GliderCrossingLeftBoundary) {
    // Place glider near left edge, moving down-right initially but configured
    // to move toward the left boundary
    // Standard glider moves down-right, so we place a left-moving variant
    // Left-moving glider:
    //  .X.
    //  X..
    //  XXX
    int startX = 4;  // Will cross into negative tile after some generations
    int startY = 10;

    // Set up left-moving glider
    vlife->setCell(startX + 1, startY, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX, startY + 1, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX, startY + 2, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX + 1, startY + 2, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX + 2, startY + 2, GameOfLife::CellState::ALIVE);

    // Verify initial state
    EXPECT_EQ(countLiveCells(-TILE_WIDTH, 0, TILE_WIDTH * 2, TILE_HEIGHT), 5);

    // Run 30 generations - glider should cross into tile at tileX=-1
    for (int gen = 0; gen < 30; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(-TILE_WIDTH, 0, TILE_WIDTH * 2, TILE_HEIGHT);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1);
        if (liveCells != 5) break;
    }
}

// Glider crossing top boundary (into negative Y tile)
TEST_F(TileEdgeTest, GliderCrossingTopBoundary) {
    // Place an upward-moving glider near top edge
    // Up-moving glider (reflected):
    //  XXX
    //  X..
    //  .X.
    int startX = 15;
    int startY = 4;  // Will cross into negative Y tile

    vlife->setCell(startX, startY, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX + 1, startY, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX + 2, startY, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX, startY + 1, GameOfLife::CellState::ALIVE);
    vlife->setCell(startX + 1, startY + 2, GameOfLife::CellState::ALIVE);

    // Verify initial state
    EXPECT_EQ(countLiveCells(0, -TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT * 2), 5);

    // Run 30 generations
    for (int gen = 0; gen < 30; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(0, -TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT * 2);
        EXPECT_EQ(liveCells, 5) << "Generation " << (gen + 1);
        if (liveCells != 5) break;
    }
}

// Stable blocks at all 4 corners of a tile simultaneously
TEST_F(TileEdgeTest, BlocksAtAllFourCorners) {
    // Place 2x2 blocks at all 4 corners of tile (0,0)
    // Top-left: (0,0) to (1,1)
    setupBlock(0, 0);
    // Top-right: (30,0) to (31,1)
    setupBlock(TILE_WIDTH - 2, 0);
    // Bottom-left: (0,30) to (1,31)
    setupBlock(0, TILE_HEIGHT - 2);
    // Bottom-right: (30,30) to (31,31)
    setupBlock(TILE_WIDTH - 2, TILE_HEIGHT - 2);

    // Verify initial count: 4 blocks * 4 cells = 16 live cells
    EXPECT_EQ(countLiveCells(0, 0, TILE_WIDTH, TILE_HEIGHT), 16);

    // Run 20 generations - all blocks should remain stable
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(0, 0, TILE_WIDTH, TILE_HEIGHT);
        EXPECT_EQ(liveCells, 16) << "Generation " << (gen + 1);
        if (liveCells != 16) break;
    }

    // Verify specific corner positions are still alive
    EXPECT_EQ(vlife->getCell(0, 0), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, 0), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(0, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
    EXPECT_EQ(vlife->getCell(TILE_WIDTH - 1, TILE_HEIGHT - 1), GameOfLife::CellState::ALIVE);
}

// Long horizontal line spanning 3 tiles
TEST_F(TileEdgeTest, LongLineSpanningThreeTiles) {
    // Create a long horizontal line of 80 cells (spanning ~2.5 tiles)
    // Line from x=10 to x=89 at y=16
    int lineY = 16;
    int lineStart = 10;
    int lineLength = 80;

    for (int x = lineStart; x < lineStart + lineLength; x++) {
        vlife->setCell(x, lineY, GameOfLife::CellState::ALIVE);
    }

    // Verify initial count
    int expectedCells = lineLength;
    EXPECT_EQ(countLiveCells(0, 0, TILE_WIDTH * 3, TILE_HEIGHT), expectedCells);

    // Run a few generations and verify cell count changes appropriately
    // A line evolves chaotically but should maintain roughly similar cell counts
    int prevCells = expectedCells;
    for (int gen = 0; gen < 5; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(-TILE_WIDTH, -TILE_HEIGHT, TILE_WIDTH * 5, TILE_HEIGHT * 3);
        // Line should evolve into something (not all die, not explode unreasonably)
        EXPECT_GT(liveCells, 0) << "Generation " << (gen + 1);
        EXPECT_LT(liveCells, lineLength * 10) << "Generation " << (gen + 1);
    }
}

// =============================================================================
// Category 7: Parallel Processing Safety (2 tests)
// =============================================================================

// Multiple gliders at tile boundaries (maximum parallel contention)
TEST_F(TileEdgeTest, ManyGlidersAtBoundaries_ParallelSafety) {
    // Place gliders at boundary positions that will maximize parallel contention
    // Multiple tiles are processed in parallel and share neighbor tiles

    // Gliders crossing from (0,0) to (1,0)
    setupGlider(TILE_WIDTH - 4, 5);
    // Gliders crossing from (2,0) to (1,0) - shares tile (1,0) with above
    setupGlider(TILE_WIDTH * 2 - 4, 5);
    // Gliders crossing from (0,0) to (0,1)
    setupGlider(5, TILE_HEIGHT - 4);
    // Gliders crossing from (0,2) to (0,1) - shares tile (0,1) with above
    setupGlider(5, TILE_HEIGHT * 2 - 4);

    // Total: 4 gliders * 5 cells = 20 cells
    EXPECT_EQ(countLiveCells(0, 0, TILE_WIDTH * 4, TILE_HEIGHT * 4), 20);

    // Enable parallel processing
    vlife->setParallelEnabled(true);

    // Run 40 generations
    for (int gen = 0; gen < 40; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(-TILE_WIDTH, -TILE_HEIGHT, TILE_WIDTH * 6, TILE_HEIGHT * 6);
        EXPECT_EQ(liveCells, 20) << "Generation " << (gen + 1);
        if (liveCells != 20) break;
    }
}

// Verify parallel and sequential produce identical results for boundary patterns
TEST_F(TileEdgeTest, ParallelDeterminism_BoundaryPatterns) {
    // Setup helper to create identical boards
    auto setupComplexBoundaryPattern = [](VLife& board) {
        // Gliders at various boundary positions
        auto setupGliderInBoard = [&board](int x, int y) {
            board.setCell(x + 1, y + 0, GameOfLife::CellState::ALIVE);
            board.setCell(x + 2, y + 1, GameOfLife::CellState::ALIVE);
            board.setCell(x + 0, y + 2, GameOfLife::CellState::ALIVE);
            board.setCell(x + 1, y + 2, GameOfLife::CellState::ALIVE);
            board.setCell(x + 2, y + 2, GameOfLife::CellState::ALIVE);
        };

        // Multiple gliders at boundaries
        setupGliderInBoard(TILE_WIDTH - 4, 5);
        setupGliderInBoard(TILE_WIDTH * 2 - 4, 5);
        setupGliderInBoard(5, TILE_HEIGHT - 4);
        setupGliderInBoard(TILE_WIDTH - 4, TILE_HEIGHT - 4);
    };

    // Create two boards with identical patterns
    auto parallelBoard = std::make_unique<VLife>();
    auto sequentialBoard = std::make_unique<VLife>();

    setupComplexBoundaryPattern(*parallelBoard);
    setupComplexBoundaryPattern(*sequentialBoard);

    parallelBoard->setParallelEnabled(true);
    sequentialBoard->setParallelEnabled(false);

    // Run 50 generations and compare results
    for (int gen = 0; gen < 50; gen++) {
        parallelBoard->runGeneration();
        sequentialBoard->runGeneration();

        // Compare cell states across all relevant tiles
        bool mismatch = false;
        for (int tileY = -1; tileY <= 3 && !mismatch; tileY++) {
            for (int tileX = -1; tileX <= 3 && !mismatch; tileX++) {
                for (int y = 0; y < TILE_HEIGHT && !mismatch; y++) {
                    for (int x = 0; x < TILE_WIDTH && !mismatch; x++) {
                        int worldX = tileX * TILE_WIDTH + x;
                        int worldY = tileY * TILE_HEIGHT + y;
                        auto pCell = parallelBoard->getCell(worldX, worldY);
                        auto sCell = sequentialBoard->getCell(worldX, worldY);
                        if (pCell != sCell) {
                            mismatch = true;
                            EXPECT_EQ(pCell, sCell)
                                << "Mismatch at (" << worldX << "," << worldY
                                << ") in generation " << (gen + 1);
                        }
                    }
                }
            }
        }
        if (mismatch) break;
    }
}

// =============================================================================
// Additional Edge Case Tests
// =============================================================================

// Test that cell pairs at tile corners work correctly
TEST_F(TileEdgeTest, CellPairAtTileCorner_TopLeft) {
    // x=0 and x=1 form a pair; test at y=0 (top-left corner)
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);
    Tile* upTile = vlife->getTile(0, -1);

    // Set both cells of the pair at the corner
    vlife->setCell(0, 0, GameOfLife::CellState::ALIVE);
    vlife->setCell(1, 0, GameOfLife::CellState::ALIVE);

    // x=0 should see x=1 as neighbor (via paired cell)
    EXPECT_EQ(getNeighborCount(centerTile, 0, 0), 1);

    // x=1 should see x=0 as neighbor
    EXPECT_EQ(getNeighborCount(centerTile, 1, 0), 1);

    // Cell in left tile at (31, 0) should see x=0 as neighbor
    EXPECT_GE(getNeighborCount(leftTile, 31, 0), 1);

    // Cell in up tile at (0, 31) and (1, 31) should see their neighbors
    EXPECT_GE(getNeighborCount(upTile, 0, TILE_HEIGHT - 1), 1);
    EXPECT_GE(getNeighborCount(upTile, 1, TILE_HEIGHT - 1), 1);
}

// Test that diagonal neighbors across tile corners are counted
TEST_F(TileEdgeTest, DiagonalNeighborAcrossCorner) {
    // Set cell at (0,0) in tile (0,0) - this is the corner
    vlife->setCell(0, 0, GameOfLife::CellState::ALIVE);

    // The diagonal tile at (-1,-1) should have its corner cell (31,31) see this as neighbor
    Tile* diagTile = vlife->getTile(-1, -1);
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 1);

    // Kill the cell
    vlife->setCell(0, 0, GameOfLife::CellState::DEAD);
    EXPECT_EQ(getNeighborCount(diagTile, TILE_WIDTH - 1, TILE_HEIGHT - 1), 0);
}

// Test multiple cells along an edge and verify cumulative neighbor counts
TEST_F(TileEdgeTest, MultipleCellsAlongEdge_CumulativeNeighborCount) {
    Tile* centerTile = vlife->getTile(0, 0);
    Tile* leftTile = vlife->getTile(-1, 0);

    // Set 3 consecutive cells at left edge: (0,14), (0,15), (0,16)
    vlife->setCell(0, 14, GameOfLife::CellState::ALIVE);
    vlife->setCell(0, 15, GameOfLife::CellState::ALIVE);
    vlife->setCell(0, 16, GameOfLife::CellState::ALIVE);

    // Cell at (31, 15) in left tile should see all 3 as neighbors
    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 3);

    // Cell at (31, 14) should see 2 neighbors: (0,14) and (0,15)
    EXPECT_EQ(getNeighborCount(leftTile, 31, 14), 2);

    // Cell at (31, 16) should see 2 neighbors: (0,15) and (0,16)
    EXPECT_EQ(getNeighborCount(leftTile, 31, 16), 2);

    // Clean up
    vlife->setCell(0, 14, GameOfLife::CellState::DEAD);
    vlife->setCell(0, 15, GameOfLife::CellState::DEAD);
    vlife->setCell(0, 16, GameOfLife::CellState::DEAD);

    EXPECT_EQ(getNeighborCount(leftTile, 31, 15), 0);
}

// Test that a beehive (stable pattern) works at tile boundary
TEST_F(TileEdgeTest, BeehiveAtTileBoundary) {
    // Beehive pattern:
    //  .XX.
    //  X..X
    //  .XX.
    // Place it at tile boundary (centered at x=31)
    int baseX = TILE_WIDTH - 2;
    int baseY = 15;

    vlife->setCell(baseX + 1, baseY, GameOfLife::CellState::ALIVE);
    vlife->setCell(baseX + 2, baseY, GameOfLife::CellState::ALIVE);
    vlife->setCell(baseX, baseY + 1, GameOfLife::CellState::ALIVE);
    vlife->setCell(baseX + 3, baseY + 1, GameOfLife::CellState::ALIVE);
    vlife->setCell(baseX + 1, baseY + 2, GameOfLife::CellState::ALIVE);
    vlife->setCell(baseX + 2, baseY + 2, GameOfLife::CellState::ALIVE);

    // Verify initial count: 6 cells
    EXPECT_EQ(countLiveCells(0, 0, TILE_WIDTH * 2, TILE_HEIGHT), 6);

    // Run 20 generations - beehive should remain stable
    for (int gen = 0; gen < 20; gen++) {
        vlife->runGeneration();
        int liveCells = countLiveCells(0, 0, TILE_WIDTH * 2, TILE_HEIGHT);
        EXPECT_EQ(liveCells, 6) << "Generation " << (gen + 1);
        if (liveCells != 6) break;
    }
}

