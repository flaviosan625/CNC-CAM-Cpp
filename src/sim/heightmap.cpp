// Porte de src/sim/heightmap.py: alturas residuais do material.
#include "sim/heightmap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cnc::sim {
namespace {

struct Region {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

Region local_region(const Heightmap& heightmap, double x, double y, double radius,
                    const HeightmapConfig& config) {
    const int cx = static_cast<int>(std::nearbyint((x - config.origin_x) / config.resolution));
    const int cy = static_cast<int>(std::nearbyint((y - config.origin_y) / config.resolution));
    const int r_px = std::max(1, static_cast<int>(std::ceil(radius / config.resolution)));
    return {std::max(0, cx - r_px), std::min(heightmap.columns, cx + r_px + 1),
            std::max(0, cy - r_px), std::min(heightmap.rows, cy + r_px + 1)};
}

template <typename Fn>
void for_region(Heightmap& heightmap, double x, double y, double radius,
                const HeightmapConfig& config, Fn&& fn) {
    const Region region = local_region(heightmap, x, y, radius, config);
    if (region.x0 >= region.x1 || region.y0 >= region.y1) return;
    const int cx = static_cast<int>(std::nearbyint((x - config.origin_x) / config.resolution));
    const int cy = static_cast<int>(std::nearbyint((y - config.origin_y) / config.resolution));
    for (int row = region.y0; row < region.y1; ++row) {
        for (int column = region.x0; column < region.x1; ++column) {
            const double dx = (column - cx) * config.resolution;
            const double dy = (row - cy) * config.resolution;
            fn(row, column, std::sqrt(dx * dx + dy * dy));
        }
    }
}

void remove_ball_nose(Heightmap& heightmap, double x, double y, double z, double radius,
                      const HeightmapConfig& config, double bottom_z) {
    for_region(heightmap, x, y, radius, config, [&](int row, int column, double distance) {
        if (distance > radius) return;
        const double surface = z + radius - std::sqrt(std::max(radius * radius - distance * distance, 0.0));
        heightmap.at(row, column) = static_cast<float>(std::min<double>(heightmap.at(row, column),
                                                                         std::max(surface, bottom_z)));
    });
}

void remove_drill(Heightmap& heightmap, double x, double y, double z, const cam::Tool& tool,
                  const HeightmapConfig& config, double bottom_z) {
    const double radius = tool.diameter / 2.0;
    const double tangent = std::tan(118.0 * 3.14159265358979323846 / 360.0);
    for_region(heightmap, x, y, radius, config, [&](int row, int column, double distance) {
        if (distance > radius) return;
        const double surface = z + distance / tangent;
        heightmap.at(row, column) = static_cast<float>(std::min<double>(heightmap.at(row, column),
                                                                         std::max(surface, bottom_z)));
    });
}

}  // namespace

int HeightmapConfig::grid_width() const {
    return std::max(2, static_cast<int>(std::ceil(width / resolution)) + 1);
}

int HeightmapConfig::grid_height() const {
    return std::max(2, static_cast<int>(std::ceil(height / resolution)) + 1);
}

HeightmapConfig HeightmapConfig::for_stock(double width, double height, double thickness,
                                           double surface_z, int max_samples) {
    if (width <= 0 || height <= 0 || thickness <= 0)
        throw std::invalid_argument("As dimensoes do material devem ser positivas.");
    const double resolution = std::max(0.25, std::sqrt(width * height / max_samples));
    return {width, height, thickness, resolution, surface_z, 0.0, 0.0};
}

Heightmap create_heightmap(const HeightmapConfig& config) {
    const int rows = config.grid_height();
    const int columns = config.grid_width();
    const float initial = static_cast<float>(config.surface_z.value_or(config.thickness));
    return {rows, columns, std::vector<float>(static_cast<size_t>(rows) * columns, initial)};
}

void apply_flat_endmill(Heightmap& heightmap, double x, double y, double z, double diameter,
                        double resolution, double origin_x, double origin_y, double bottom_z) {
    if (diameter <= 0 || resolution <= 0)
        throw std::invalid_argument("O diametro e a resolucao devem ser positivos.");
    const int cx = static_cast<int>(std::nearbyint((x - origin_x) / resolution));
    const int cy = static_cast<int>(std::nearbyint((y - origin_y) / resolution));
    const double radius = diameter / 2.0;
    const int radius_px = std::max(1, static_cast<int>(std::ceil(radius / resolution)));
    const int x0 = std::max(0, cx - radius_px), x1 = std::min(heightmap.columns, cx + radius_px + 1);
    const int y0 = std::max(0, cy - radius_px), y1 = std::min(heightmap.rows, cy + radius_px + 1);
    if (x0 >= x1 || y0 >= y1) return;
    for (int row = y0; row < y1; ++row) {
        for (int column = x0; column < x1; ++column) {
            const double dx = (column - cx) * resolution;
            const double dy = (row - cy) * resolution;
            if (std::sqrt(dx * dx + dy * dy) <= radius + 1e-9)
                heightmap.at(row, column) = static_cast<float>(std::min<double>(heightmap.at(row, column),
                                                                                   std::max(z, bottom_z)));
        }
    }
}

void apply_vbit(Heightmap& heightmap, double x, double y, double z, const cam::Tool& tool,
                const HeightmapConfig& config, double bottom_z) {
    const double radius = tool.diameter / 2.0;
    const double half_angle = std::max(tool.v_angle, 1.0) * 3.14159265358979323846 / 360.0;
    for_region(heightmap, x, y, radius, config, [&](int row, int column, double distance) {
        if (distance > radius) return;
        const double surface = z + std::max(0.0, distance - tool.tip_diameter / 2.0) /
                                     std::max(std::tan(half_angle), 1e-6);
        heightmap.at(row, column) = static_cast<float>(std::min<double>(heightmap.at(row, column),
                                                                         std::max(surface, bottom_z)));
    });
}

void remove_tool_segment(Heightmap& heightmap, const std::array<double, 3>& start,
                         const std::array<double, 3>& end, const cam::Tool& tool,
                         const HeightmapConfig& config, double bottom_z) {
    const double distance_xy = std::hypot(end[0] - start[0], end[1] - start[1]);
    const double spacing = std::max(config.resolution * 0.5, 0.1);
    const int samples = std::max(1, static_cast<int>(std::ceil(distance_xy / spacing)));
    for (int index = 0; index <= samples; ++index) {
        const double fraction = static_cast<double>(index) / samples;
        const double x = start[0] + (end[0] - start[0]) * fraction;
        const double y = start[1] + (end[1] - start[1]) * fraction;
        const double z = start[2] + (end[2] - start[2]) * fraction;
        if (tool.tool_type == cam::ToolType::VBit || tool.tool_type == cam::ToolType::Engraving)
            apply_vbit(heightmap, x, y, z, tool, config, bottom_z);
        else if (tool.tool_type == cam::ToolType::Drill)
            remove_drill(heightmap, x, y, z, tool, config, bottom_z);
        else if (tool.tool_type == cam::ToolType::BallNose || tool.tool_type == cam::ToolType::TaperedBall)
            remove_ball_nose(heightmap, x, y, z, tool.diameter / 2.0, config, bottom_z);
        else
            apply_flat_endmill(heightmap, x, y, z, tool.diameter, config.resolution,
                               config.origin_x, config.origin_y, bottom_z);
    }
}

MeshData heightmap_mesh(const Heightmap& heightmap, const HeightmapConfig& config,
                        const std::array<float, 3>& color) {
    MeshData out;
    const size_t top_vertex_count = static_cast<size_t>(heightmap.rows) * heightmap.columns;
    out.vertices.resize(top_vertex_count * 6);
    for (int row = 0; row < heightmap.rows; ++row) {
        for (int column = 0; column < heightmap.columns; ++column) {
            const size_t base = (static_cast<size_t>(row) * heightmap.columns + column) * 6;
            const double x = std::min(config.origin_x + column * config.resolution,
                                      config.origin_x + config.width);
            const double y = std::min(config.origin_y + row * config.resolution,
                                      config.origin_y + config.height);
            out.vertices[base] = static_cast<float>(x);
            out.vertices[base + 1] = static_cast<float>(y);
            out.vertices[base + 2] = heightmap.at(row, column);
            // Gentle directional variation gives the stock a readable wood grain
            // without making the simulated cut harder to see.
            const float grain = static_cast<float>(0.97 + 0.025 * std::sin(y * 0.42) +
                                                   0.012 * std::sin(y * 1.73 + x * 0.018));
            out.vertices[base + 3] = std::clamp(color[0] * grain, 0.0f, 1.0f);
            out.vertices[base + 4] = std::clamp(color[1] * grain, 0.0f, 1.0f);
            out.vertices[base + 5] = std::clamp(color[2] * grain, 0.0f, 1.0f);
        }
    }
    const int cells = (heightmap.rows - 1) * (heightmap.columns - 1);
    const size_t perimeter_segments = static_cast<size_t>(2 * (heightmap.rows - 1) +
                                                          2 * (heightmap.columns - 1));
    out.indices.reserve(static_cast<size_t>(cells) * 6 + perimeter_segments * 6 + 6);
    for (int row = 0; row < heightmap.rows - 1; ++row) {
        for (int column = 0; column < heightmap.columns - 1; ++column) {
            const auto tl = static_cast<std::uint32_t>(row * heightmap.columns + column);
            const auto tr = tl + 1;
            const auto bl = tl + static_cast<std::uint32_t>(heightmap.columns);
            const auto br = bl + 1;
            out.indices.insert(out.indices.end(), {tl, tr, bl, tr, br, bl});
        }
    }

    const float bottom_z = static_cast<float>(config.surface_z.value_or(config.thickness) - config.thickness);
    const float side_shade = 0.78f;
    const auto append_vertex = [&out, &color, side_shade](double x, double y, double z, bool side) {
        const float shade = side ? side_shade : 0.72f;
        const auto index = static_cast<std::uint32_t>(out.vertices.size() / 6);
        out.vertices.insert(out.vertices.end(), {static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(z), std::clamp(color[0] * shade, 0.0f, 1.0f),
            std::clamp(color[1] * shade, 0.0f, 1.0f), std::clamp(color[2] * shade, 0.0f, 1.0f)});
        return index;
    };
    // Close the stock perimeter down to its bottom face so the workpiece reads
    // as a solid board, including while its top is being machined.
    const int last_row = heightmap.rows - 1;
    const int last_column = heightmap.columns - 1;
    const auto add_wall_quad = [&out, &append_vertex, &heightmap, bottom_z](int row0, int column0,
                                                                          int row1, int column1) {
        const size_t top0 = static_cast<size_t>(row0) * heightmap.columns + column0;
        const size_t top1 = static_cast<size_t>(row1) * heightmap.columns + column1;
        const size_t base0 = top0 * 6;
        const size_t base1 = top1 * 6;
        const auto a = append_vertex(out.vertices[base0], out.vertices[base0 + 1],
                                     heightmap.at(row0, column0), true);
        const auto b = append_vertex(out.vertices[base1], out.vertices[base1 + 1],
                                     heightmap.at(row1, column1), true);
        const auto c = append_vertex(out.vertices[base1], out.vertices[base1 + 1], bottom_z, true);
        const auto d = append_vertex(out.vertices[base0], out.vertices[base0 + 1], bottom_z, true);
        out.indices.insert(out.indices.end(), {a, b, d, b, c, d});
    };
    for (int column = 0; column < last_column; ++column) {
        add_wall_quad(0, column + 1, 0, column);
        add_wall_quad(last_row, column, last_row, column + 1);
    }
    for (int row = 0; row < last_row; ++row) {
        add_wall_quad(row, 0, row + 1, 0);
        add_wall_quad(row + 1, last_column, row, last_column);
    }
    const auto b0 = append_vertex(config.origin_x, config.origin_y, bottom_z, false);
    const auto b1 = append_vertex(config.origin_x + config.width, config.origin_y, bottom_z, false);
    const auto b2 = append_vertex(config.origin_x + config.width, config.origin_y + config.height, bottom_z, false);
    const auto b3 = append_vertex(config.origin_x, config.origin_y + config.height, bottom_z, false);
    out.indices.insert(out.indices.end(), {b0, b2, b1, b0, b3, b2});
    return out;
}

}  // namespace cnc::sim
