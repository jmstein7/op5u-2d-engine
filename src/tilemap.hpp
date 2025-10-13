// src/tilemap.hpp
#pragma once
#include <vector>

struct Tilemap {
    int width, height;
    int tileSize;
    std::vector<int> data;

    Tilemap(int w, int h, int size);
    void fillRandom(int numTiles);
};
