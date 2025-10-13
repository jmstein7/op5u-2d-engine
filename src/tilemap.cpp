// src/tilemap.cpp
#include "tilemap.hpp"
#include <cstdlib>

Tilemap::Tilemap(int w, int h, int size)
: width(w), height(h), tileSize(size), data(w*h, 0) {}

void Tilemap::fillRandom(int numTiles) {
    for (int i = 0; i < width * height; ++i)
        data[i] = rand() % numTiles;
}
