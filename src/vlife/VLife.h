//
// Created by George Burgyan on 3/1/25.
//

#pragma once

#include <GameOfLife.h>

#define TILE_WIDTH_2 5
#define TILE_HEIGHT_2 5
#define TILE_WIDTH 1 << TILE_WIDTH_2
#define TILE_HEIGHT 1 << TILE_HEIGHT_2

class VLife : public GameOfLife {

public:
    VLife();

    void resetBoard() override;

    [[nodiscard]] CellState getCell(uint32_t x, uint32_t y) const override;
    void setCell(uint32_t x, uint32_t y, CellState state) override;

    [[nodiscard]] std::vector<CellState> getCells(uint32_t startX, uint32_t startY, uint32_t width, uint32_t height) const override;

    void setCells(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height, const std::vector<CellState>& cells) override;

    void runGeneration() override;
    void runGenerations(uint32_t count) override;

private:
    void populateRuleLUT();

public:
    std::byte ruleLUT[256];
};