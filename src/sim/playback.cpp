// Porte de src/sim/playback.py: playback temporal de percursos CNC.
#include "sim/playback.hpp"

#include <algorithm>
#include <stdexcept>

namespace cnc::sim {
namespace {

std::array<double, 3> interpolate(const cam::Move& start, const cam::Move& end, double fraction) {
    return {start.x + (end.x - start.x) * fraction,
            start.y + (end.y - start.y) * fraction,
            start.z + (end.z - start.z) * fraction};
}

}  // namespace

ToolpathSimulation::ToolpathSimulation(std::vector<SimulationOperation> operations_, HeightmapConfig config_,
                                       double material_bottom_z_)
    : operations(std::move(operations_)), config(std::move(config_)), material_bottom_z(material_bottom_z_),
      segments_(make_segments()) {
    if (operations.empty()) throw std::invalid_argument("A simulacao precisa de pelo menos um percurso.");
    for (size_t index = 0; index < segments_.size(); ++index) {
        if (segments_[index].end.kind != cam::MoveKind::Dwell) {
            draw_segment_indices_.push_back(index);
        }
        total_seconds += segments_[index].duration;
    }
    heightmap = create_heightmap(config);
    cursor = initial_position();
    current_operation = 0;
}

std::array<double, 3> ToolpathSimulation::initial_position() const {
    for (const SimulationOperation& operation : operations) {
        if (!operation.toolpath.moves.empty()) {
            const cam::Move& move = operation.toolpath.moves.front();
            return {move.x, move.y, move.z};
        }
    }
    return {0.0, 0.0, config.surface_z.value_or(0.0)};
}

std::vector<ToolpathSimulation::Segment> ToolpathSimulation::make_segments() const {
    std::vector<Segment> out;
    for (size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
        const auto& operation = operations[operation_index];
        const cam::Move* previous = nullptr;
        for (const cam::Move& move : operation.toolpath.moves) {
            if (previous) {
                out.push_back({*previous, move, operation_index,
                               operation.toolpath.move_seconds(*previous, move, kRapidRateMmMin)});
            }
            previous = &move;
        }
    }
    return out;
}

double ToolpathSimulation::progress() const {
    if (total_seconds <= 0) return finished() ? 1.0 : 0.0;
    return std::min(1.0, elapsed_seconds_ / total_seconds);
}

double ToolpathSimulation::remaining_seconds() const {
    return std::max(0.0, total_seconds - elapsed_seconds_);
}

void ToolpathSimulation::reset() {
    heightmap = create_heightmap(config);
    segment_index_ = 0;
    segment_fraction_ = 0.0;
    elapsed_seconds_ = 0.0;
    cursor = initial_position();
    current_operation = 0;
}

bool ToolpathSimulation::advance(double seconds) {
    double remaining = std::max(0.0, seconds);
    while (!finished()) {
        const Segment& segment = segments_[segment_index_];
        if (segment.duration <= 0.0) {
            finish_segment(segment);
            continue;
        }
        const double available = segment.duration * (1.0 - segment_fraction_);
        const double consumed = std::min(remaining, available);
        const double old_fraction = segment_fraction_;
        segment_fraction_ += consumed / segment.duration;
        elapsed_seconds_ += consumed;
        update_segment(segment, old_fraction, segment_fraction_);
        remaining -= consumed;
        if (segment_fraction_ >= 1.0 - 1e-9) finish_segment(segment);
        if (remaining <= 1e-12) break;
    }
    return finished();
}

bool ToolpathSimulation::step() {
    if (finished()) return true;
    const Segment& segment = segments_[segment_index_];
    const double remaining = segment.duration * (1.0 - segment_fraction_);
    if (remaining > 0.0) advance(remaining);
    else finish_segment(segment);
    return finished();
}

void ToolpathSimulation::update_segment(const Segment& segment, double start_fraction, double end_fraction) {
    const auto start = interpolate(segment.start, segment.end, start_fraction);
    const auto end = interpolate(segment.start, segment.end, end_fraction);
    cursor = end;
    current_operation = segment.operation;
    if (segment.end.kind == cam::MoveKind::Cut || segment.end.kind == cam::MoveKind::Plunge)
        remove_tool_segment(heightmap, start, end, operations[segment.operation].tool, config, material_bottom_z);
}

void ToolpathSimulation::finish_segment(const Segment& segment) {
    cursor = {segment.end.x, segment.end.y, segment.end.z};
    current_operation = segment.operation;
    ++segment_index_;
    segment_fraction_ = 0.0;
}

std::vector<float> ToolpathSimulation::path_vertices() const {
    std::vector<float> vertices(draw_segment_indices_.size() * 4 * 6, 0.0f);
    constexpr double path_lift = 0.06;  // evita cintilacao quando o percurso coincide com a face da madeira
    constexpr std::array<float, 3> done{0.20f, 0.88f, 0.52f};
    constexpr std::array<float, 3> active{1.0f, 0.58f, 0.16f};
    constexpr std::array<float, 3> future{0.31f, 0.76f, 0.97f};
    constexpr std::array<float, 3> rapid{0.55f, 0.60f, 0.68f};
    const auto set_position = [&vertices](size_t line, int vertex, const cam::Move& move) {
        const size_t base = (line * 4 + static_cast<size_t>(vertex)) * 6;
        vertices[base] = static_cast<float>(move.x);
        vertices[base + 1] = static_cast<float>(move.y);
        vertices[base + 2] = static_cast<float>(move.z + path_lift);
    };
    const auto set_point = [&vertices](size_t line, int vertex, const std::array<double, 3>& point) {
        const size_t base = (line * 4 + static_cast<size_t>(vertex)) * 6;
        vertices[base] = static_cast<float>(point[0]);
        vertices[base + 1] = static_cast<float>(point[1]);
        vertices[base + 2] = static_cast<float>(point[2] + path_lift);
    };
    const auto set_color = [&vertices](size_t line, int first_vertex, int count, const std::array<float, 3>& color) {
        for (int vertex = first_vertex; vertex < first_vertex + count; ++vertex) {
            const size_t base = (line * 4 + static_cast<size_t>(vertex)) * 6 + 3;
            vertices[base] = color[0]; vertices[base + 1] = color[1]; vertices[base + 2] = color[2];
        }
    };

    for (size_t line = 0; line < draw_segment_indices_.size(); ++line) {
        const Segment& segment = segments_[draw_segment_indices_[line]];
        set_position(line, 0, segment.start);
        set_position(line, 1, segment.end);
        const bool is_rapid = segment.end.kind == cam::MoveKind::Rapid;
        set_color(line, 0, 2, is_rapid ? rapid : (draw_segment_indices_[line] < segment_index_ ? done : future));
        set_position(line, 2, segment.end);
        set_position(line, 3, segment.end);
        set_color(line, 2, 2, is_rapid ? rapid : future);

        if (draw_segment_indices_[line] == segment_index_) {
            const auto point = interpolate(segment.start, segment.end, segment_fraction_);
            set_point(line, 1, point);
            set_point(line, 2, point);
            set_point(line, 3, {segment.end.x, segment.end.y, segment.end.z});
            set_color(line, 2, 2, is_rapid ? rapid : active);
        }
    }
    return vertices;
}

MeshData ToolpathSimulation::stock_mesh(const std::array<float, 3>& color) const {
    return heightmap_mesh(heightmap, config, color);
}

}  // namespace cnc::sim
