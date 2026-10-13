// Porte de src/viewport/grid.py: vertices de grade, eixos e marcador de origem.
#pragma once

#include <vector>

namespace cnc::vp {

double nice_step(double span, int target_lines = 20);
std::vector<float> build_workpiece_grid_vertices(double width, double height, double z = 0.0);
std::vector<float> build_grid_vertices(double size = 500.0, double minor_step = 10.0,
                                       double major_step = 50.0);
std::vector<float> build_axes_vertices(double length = 500.0);
std::vector<float> build_origin_marker(double size = 5.0);

}  // namespace cnc::vp
