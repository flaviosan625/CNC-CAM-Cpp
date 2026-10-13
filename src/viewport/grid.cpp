// Porte de src/viewport/grid.py: vertices de grade, eixos e marcador de origem.
#include "viewport/grid.hpp"

#include <cmath>
#include <utility>

namespace cnc::vp {
namespace {

void add_line(std::vector<float>& vertices, double x1, double y1, double x2, double y2,
              double z, const float color[3]) {
    vertices.insert(vertices.end(), {static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z),
                                     color[0], color[1], color[2],
                                     static_cast<float>(x2), static_cast<float>(y2), static_cast<float>(z),
                                     color[0], color[1], color[2]});
}

}  // namespace

double nice_step(double span, int target_lines) {
    if (span <= 0.0) return 10.0;
    const double raw = span / static_cast<double>(std::max(target_lines, 1));
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double scaled = raw / power;
    const double factor = scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : 5.0;
    return factor * power;
}

std::vector<float> build_workpiece_grid_vertices(double width, double height, double z) {
    if (width <= 0.0 || height <= 0.0) return {};

    const double step = nice_step(std::min(width, height));
    constexpr int major_every = 5;
    constexpr float minor_color[] = {0.48f, 0.53f, 0.59f};
    constexpr float major_color[] = {0.76f, 0.80f, 0.85f};
    constexpr float edge_color[] = {0.94f, 0.96f, 0.98f};
    std::vector<float> vertices;

    const int x_count = static_cast<int>(std::floor(width / step + 1e-9));
    for (int index = 1; index <= x_count; ++index) {
        const double x = std::min(width, index * step);
        if (x >= width - 1e-9) break;
        add_line(vertices, x, 0.0, x, height, z,
                 index % major_every == 0 ? major_color : minor_color);
    }

    const int y_count = static_cast<int>(std::floor(height / step + 1e-9));
    for (int index = 1; index <= y_count; ++index) {
        const double y = std::min(height, index * step);
        if (y >= height - 1e-9) break;
        add_line(vertices, 0.0, y, width, y, z,
                 index % major_every == 0 ? major_color : minor_color);
    }

    add_line(vertices, 0.0, 0.0, width, 0.0, z, edge_color);
    add_line(vertices, width, 0.0, width, height, z, edge_color);
    add_line(vertices, width, height, 0.0, height, z, edge_color);
    add_line(vertices, 0.0, height, 0.0, 0.0, z, edge_color);
    return vertices;
}

std::vector<float> build_grid_vertices(double size, double minor_step, double major_step) {
    std::vector<float> vertices;
    constexpr float minor_color[] = {0.25f, 0.25f, 0.28f};
    constexpr float major_color[] = {0.35f, 0.35f, 0.40f};
    const double half = size;

    for (double pos = minor_step; pos <= half; pos += minor_step) {
        if (std::abs(std::fmod(pos, major_step)) > 0.01) {
            for (double sign : {1.0, -1.0}) {
                add_line(vertices, sign * pos, -half, sign * pos, half, 0.0, minor_color);
                add_line(vertices, -half, sign * pos, half, sign * pos, 0.0, minor_color);
            }
        }
    }

    for (double pos = major_step; pos <= half; pos += major_step) {
        for (double sign : {1.0, -1.0}) {
            add_line(vertices, sign * pos, -half, sign * pos, half, 0.0, major_color);
            add_line(vertices, -half, sign * pos, half, sign * pos, 0.0, major_color);
        }
    }
    return vertices;
}

std::vector<float> build_axes_vertices(double length) {
    return {
        0, 0, 0, 0.9f, 0.2f, 0.2f, static_cast<float>(length), 0, 0, 0.9f, 0.2f, 0.2f,
        0, 0, 0, 0.2f, 0.8f, 0.2f, 0, static_cast<float>(length), 0, 0.2f, 0.8f, 0.2f,
        0, 0, 0, 0.3f, 0.4f, 0.9f, 0, 0, static_cast<float>(length), 0.3f, 0.4f, 0.9f,
    };
}

std::vector<float> build_origin_marker(double size) {
    constexpr float white[] = {1.0f, 1.0f, 1.0f};
    std::vector<float> vertices;
    add_line(vertices, -size, -size, size, size, 0.0, white);
    add_line(vertices, size, -size, -size, size, 0.0, white);
    return vertices;
}

}  // namespace cnc::vp
