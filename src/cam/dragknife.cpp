// Porte de src/cam/dragknife.py.
#include "cam/dragknife.hpp"

#include <cmath>

#include "cam/fluting.hpp"  // path_points_of

namespace cnc::cam {

namespace {

bool is_closed_path(const Points& pts) {
    if (pts.size() < 3) return false;
    return dist(pts.front(), pts.back()) < 0.01;
}

double dir_x(Pt a, Pt b) {
    const double d = dist(a, b);
    return d > 1e-9 ? (b.x - a.x) / d : 1.0;
}

double dir_y(Pt a, Pt b) {
    const double d = dist(a, b);
    return d > 1e-9 ? (b.y - a.y) / d : 0.0;
}

double corner_angle(Pt a, Pt b, Pt c) {
    const double v1 = std::atan2(b.y - a.y, b.x - a.x);
    const double v2 = std::atan2(c.y - b.y, c.x - b.x);
    double diff = v2 - v1;
    while (diff > kPi) diff -= 2 * kPi;
    while (diff < -kPi) diff += 2 * kPi;
    return diff;
}

size_t longest_span_midpoint(const Points& pts) {
    size_t best_i = 0;
    double best_len = 0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const double d = dist(pts[i], pts[i + 1]);
        if (d > best_len) {
            best_len = d;
            best_i = i;
        }
    }
    return best_i;
}

}  // namespace

std::vector<Toolpath> generate_dragknife(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                         const cad::MaterialSetup& material, const DragknifeSettings& s) {
    const double safe_z = material.surface_z() + material.clearance_z;
    const double surface = material.surface_z();
    const double cut_z = surface - s.cut_depth;
    const double swivel_z = surface - s.swivel_depth;
    const double offset = s.blade_offset;
    const double tol_rad = radians(s.tolerance_angle);

    std::vector<Move> all_moves;

    for (const cad::EntityPtr& ent : entities) {
        const Points pts = path_points_of(*ent);
        if (pts.size() < 2) continue;

        const bool closed = is_closed_path(pts);

        for (int pass = 0; pass < s.passes; ++pass) {
            Points pass_pts = pts;
            if (closed) {
                const size_t start_idx = longest_span_midpoint(pass_pts);
                // pass_pts[start_idx:-1] + pass_pts[:start_idx + 1]
                Points rotated(pass_pts.begin() + static_cast<long>(start_idx), pass_pts.end() - 1);
                rotated.insert(rotated.end(), pass_pts.begin(), pass_pts.begin() + static_cast<long>(start_idx) + 1);
                pass_pts = std::move(rotated);
            }

            const double lead_x = pass_pts[0].x - offset * dir_x(pass_pts[0], pass_pts[1]);
            const double lead_y = pass_pts[0].y - offset * dir_y(pass_pts[0], pass_pts[1]);

            all_moves.push_back({MoveKind::Rapid, lead_x, lead_y, safe_z});
            all_moves.push_back({MoveKind::Plunge, lead_x, lead_y, cut_z});
            all_moves.push_back({MoveKind::Cut, pass_pts[0].x, pass_pts[0].y, cut_z});

            for (size_t i = 1; i < pass_pts.size(); ++i) {
                all_moves.push_back({MoveKind::Cut, pass_pts[i].x, pass_pts[i].y, cut_z});

                if (i + 1 < pass_pts.size()) {
                    const double angle_change = corner_angle(pass_pts[i - 1], pass_pts[i], pass_pts[i + 1]);
                    if (std::fabs(angle_change) > tol_rad) {
                        const double overshoot_x = pass_pts[i].x + offset * dir_x(pass_pts[i - 1], pass_pts[i]);
                        const double overshoot_y = pass_pts[i].y + offset * dir_y(pass_pts[i - 1], pass_pts[i]);
                        all_moves.push_back({MoveKind::Cut, overshoot_x, overshoot_y, cut_z});
                        all_moves.push_back({MoveKind::Plunge, overshoot_x, overshoot_y, swivel_z});

                        const double approach_x = pass_pts[i].x - offset * dir_x(pass_pts[i], pass_pts[i + 1]);
                        const double approach_y = pass_pts[i].y - offset * dir_y(pass_pts[i], pass_pts[i + 1]);
                        all_moves.push_back({MoveKind::Cut, approach_x, approach_y, swivel_z});
                        all_moves.push_back({MoveKind::Plunge, approach_x, approach_y, cut_z});
                    }
                }
            }

            const size_t n = pass_pts.size();
            const double trail_x = pass_pts[n - 1].x + offset * dir_x(pass_pts[n - 2], pass_pts[n - 1]);
            const double trail_y = pass_pts[n - 1].y + offset * dir_y(pass_pts[n - 2], pass_pts[n - 1]);
            all_moves.push_back({MoveKind::Cut, trail_x, trail_y, cut_z});
            all_moves.push_back({MoveKind::Rapid, trail_x, trail_y, safe_z});
        }
    }

    if (all_moves.empty()) return {};
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Dragknife;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = 0;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(all_moves);
    return {std::move(tp)};
}

}  // namespace cnc::cam
