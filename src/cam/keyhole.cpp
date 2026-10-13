// Porte de src/cam/keyhole.py.
#include "cam/keyhole.hpp"

#include <optional>
#include <utility>

#include "utils/errors.hpp"

namespace cnc::cam {

namespace {

// So linhas (e cotas, que tem x1/y1/x2/y2) definem um rasgo: no Python, o resto cai no
// AttributeError de polyline_points() e vira "sem rasgo".
std::optional<std::pair<Pt, Pt>> entity_endpoints(const cad::Entity& e) {
    if (e.closed()) return std::nullopt;
    Pt a{}, b{};
    if (auto* l = cad::as<cad::Line>(e)) {
        a = {l->x1, l->y1};
        b = {l->x2, l->y2};
    } else if (auto* d = cad::as<cad::Dimension>(e)) {
        a = {d->x1, d->y1};
        b = {d->x2, d->y2};
    } else {
        return std::nullopt;
    }
    if (dist(a, b) > 1e-9) return std::make_pair(a, b);
    return std::nullopt;
}

}  // namespace

std::vector<Toolpath> generate_keyhole(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const KeyholeSettings& s) {
    if (s.slot_depth > s.plunge_depth) {
        throw ValueError("A profundidade do rasgo deve ser menor ou igual à do furo.");
    }
    const double safe_z = material.surface_z() + material.clearance_z;
    const double surface = material.surface_z();

    std::vector<Move> moves;
    for (const cad::EntityPtr& ent : entities) {
        const auto pts = entity_endpoints(*ent);
        if (!pts) continue;
        const auto [p1, p2] = *pts;
        const double entry_z = surface - s.plunge_depth;
        const double slot_z = surface - s.slot_depth;

        moves.push_back({MoveKind::Rapid, p1.x, p1.y, safe_z});
        moves.push_back({MoveKind::Plunge, p1.x, p1.y, entry_z});
        moves.push_back({MoveKind::Plunge, p1.x, p1.y, slot_z});
        moves.push_back({MoveKind::Cut, p2.x, p2.y, slot_z});
        moves.push_back({MoveKind::Cut, p1.x, p1.y, slot_z});
        moves.push_back({MoveKind::Plunge, p1.x, p1.y, entry_z});
        moves.push_back({MoveKind::Rapid, p1.x, p1.y, safe_z});
    }

    if (moves.empty()) return {};
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Keyhole;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(moves);
    return {std::move(tp)};
}

}  // namespace cnc::cam
