// Porte de src/sim/heightmap.py: alturas residuais do material.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "cam/tools.hpp"

namespace cnc::sim {

struct HeightmapConfig {
    double width = 300.0;
    double height = 200.0;
    double thickness = 18.0;
    double resolution = 0.5;
    std::optional<double> surface_z;
    double origin_x = 0.0;
    double origin_y = 0.0;

    int grid_width() const;
    int grid_height() const;
    static HeightmapConfig for_stock(double width, double height, double thickness,
                                     double surface_z, int max_samples = 180000);
};

struct Heightmap {
    int rows = 0;
    int columns = 0;
    std::vector<float> values;

    float& at(int row, int column) { return values[static_cast<size_t>(row) * columns + column]; }
    float at(int row, int column) const { return values[static_cast<size_t>(row) * columns + column]; }
};

struct MeshData {
    std::vector<float> vertices;       // XYZRGB, seis floats por vertice
    std::vector<std::uint32_t> indices;
};

Heightmap create_heightmap(const HeightmapConfig& config);

void apply_flat_endmill(Heightmap& heightmap, double x, double y, double z, double diameter,
                        double resolution, double origin_x = 0.0, double origin_y = 0.0,
                        double bottom_z = -HUGE_VAL);
void remove_tool_segment(Heightmap& heightmap, const std::array<double, 3>& start,
                         const std::array<double, 3>& end, const cam::Tool& tool,
                         const HeightmapConfig& config, double bottom_z);
void apply_vbit(Heightmap& heightmap, double x, double y, double z, const cam::Tool& tool,
                const HeightmapConfig& config, double bottom_z = -HUGE_VAL);

MeshData heightmap_mesh(const Heightmap& heightmap, const HeightmapConfig& config,
                        const std::array<float, 3>& color);

}  // namespace cnc::sim
