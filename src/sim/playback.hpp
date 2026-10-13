// Porte de src/sim/playback.py: playback temporal de percursos CNC.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "cam/model.hpp"
#include "cam/tools.hpp"
#include "sim/heightmap.hpp"

namespace cnc::sim {

inline constexpr double kRapidRateMmMin = 8000.0;

struct SimulationOperation {
    cam::Toolpath toolpath;
    cam::Tool tool;
};

class ToolpathSimulation {
public:
    ToolpathSimulation(std::vector<SimulationOperation> operations, HeightmapConfig config,
                       double material_bottom_z);

    bool finished() const { return segment_index_ >= segments_.size(); }
    double progress() const;
    double remaining_seconds() const;
    void reset();
    bool advance(double seconds);
    bool step();
    std::vector<float> path_vertices() const;
    MeshData stock_mesh(const std::array<float, 3>& color) const;

    std::vector<SimulationOperation> operations;
    HeightmapConfig config;
    double material_bottom_z = 0.0;
    Heightmap heightmap;
    std::array<double, 3> cursor{};
    size_t current_operation = 0;
    double total_seconds = 0.0;
    size_t segment_index() const { return segment_index_; }
    double segment_fraction() const { return segment_fraction_; }
    double elapsed_seconds() const { return elapsed_seconds_; }

private:
    struct Segment {
        cam::Move start;
        cam::Move end;
        size_t operation = 0;
        double duration = 0.0;
    };

    std::array<double, 3> initial_position() const;
    std::vector<Segment> make_segments() const;
    void update_segment(const Segment& segment, double start_fraction, double end_fraction);
    void finish_segment(const Segment& segment);

    std::vector<Segment> segments_;
    std::vector<size_t> draw_segment_indices_;
    size_t segment_index_ = 0;
    double segment_fraction_ = 0.0;
    double elapsed_seconds_ = 0.0;
};

}  // namespace cnc::sim
