// Porte de src/cam/fluting.py.
#include "cam/fluting.hpp"

#include <algorithm>
#include <cmath>

namespace cnc::cam {

Points path_points_of(const cad::Entity& e) {
    Points points = e.to_points();
    if (e.closed() && points.size() >= 3 && dist(points.front(), points.back()) > 1e-9) {
        points.push_back(points.front());
    }
    return points;
}

namespace {

Points resample_fixed(const Points& pts, double step) {
    if (pts.size() < 2 || step <= 0) return pts;
    Points out{pts[0]};
    double accum = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double seg = dist(pts[i - 1], pts[i]);
        if (seg < 1e-9) continue;
        const double dx = (pts[i].x - pts[i - 1].x) / seg;
        const double dy = (pts[i].y - pts[i - 1].y) / seg;
        double remaining = seg;
        while (remaining >= step) {
            accum += step;
            remaining -= step;
            out.push_back({pts[i - 1].x + dx * (seg - remaining), pts[i - 1].y + dy * (seg - remaining)});
        }
        accum += remaining;
    }
    if (dist(out.back(), pts.back()) > 1e-9) out.push_back(pts.back());
    return out;
}

}  // namespace

std::vector<Toolpath> generate_fluting(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const FlutingSettings& s) {
    const double safe_z = material.surface_z() + material.clearance_z;
    const double surface = material.surface_z();
    const double start_z = surface - s.start_depth;
    std::vector<Move> moves;

    for (const cad::EntityPtr& ent : entities) {
        Points pts = path_points_of(*ent);
        if (pts.size() < 2) continue;

        const double total_len = path_length(pts);
        if (total_len < 1e-6) continue;

        pts = resample_fixed(pts, std::max(1.0, total_len / 100.0));
        const std::vector<double> passes = compute_passes(s.cut_depth, tool.pass_depth, false);

        for (double pass_depth : passes) {
            const double depth_start = std::min(pass_depth, s.cut_depth);
            const double depth_end = s.end_depth > 0 ? std::min(pass_depth, s.end_depth) : depth_start;

            moves.push_back({MoveKind::Rapid, pts[0].x, pts[0].y, safe_z});
            moves.push_back({MoveKind::Plunge, pts[0].x, pts[0].y, start_z});

            double accum = 0.0;
            Pt prev = pts[0];
            for (size_t i = 1; i < pts.size(); ++i) {
                const Pt cur = pts[i];
                accum += dist(prev, cur);

                const double t = total_len > 0 ? accum / total_len : 1.0;
                const double target_depth = depth_start + (depth_end - depth_start) * t;

                double z;
                if (s.ramp_in > 0 && accum < s.ramp_in) {
                    z = start_z - target_depth * (accum / s.ramp_in);
                } else if (s.ramp_out > 0 && (total_len - accum) < s.ramp_out) {
                    z = start_z - target_depth * ((total_len - accum) / s.ramp_out);
                } else {
                    z = start_z - target_depth;
                }

                moves.push_back({MoveKind::Cut, cur.x, cur.y, z});
                prev = cur;
            }

            moves.push_back({MoveKind::Rapid, pts.back().x, pts.back().y, safe_z});
        }
    }

    if (moves.empty()) return {};
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Fluting;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(moves);
    return {std::move(tp)};
}

}  // namespace cnc::cam
