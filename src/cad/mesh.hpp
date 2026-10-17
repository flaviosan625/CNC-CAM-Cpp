#pragma once

#include <vector>

namespace cnc::cad {

struct Mesh {
    std::vector<float> vertices; // XYZRGB (coordenadas X, Y, Z e cor R, G, B em 0.0f..1.0f)
    std::vector<unsigned int> indices; // Índices triangulares
};

} // namespace cnc::cad

