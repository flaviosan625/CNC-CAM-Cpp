// Porte de src/cam/drill.py.
#include "cam/drill.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace cnc::cam {

const char* retract_label(RetractMode m) {
    return m == RetractMode::AboveStart ? "Acima do início do corte" : "Acima da passada anterior";
}

namespace {

struct Hole {
    double x;
    double y;
    std::optional<double> diameter;
    bool round_enough;
};

std::pair<std::vector<Hole>, std::vector<std::string>> holes_from(
    const std::vector<cad::EntityPtr>& entities, double tolerance) {
    std::vector<Hole> holes;
    std::vector<cad::EntityPtr> others;
    for (const cad::EntityPtr& e : entities) {
        if (auto* dp = cad::as<cad::DrillPoint>(e)) {
            holes.push_back({dp->x, dp->y, std::nullopt, true});
        } else {
            others.push_back(e);
        }
    }
    std::vector<std::string> notes;
    const SplitResult split = split_entities(others, tolerance);
    for (const ContourPtr& c : split.closed) {
        const BBox b = c->polygon.bounds();
        const double w = b.x1 - b.x0, h = b.y1 - b.y0;
        if (w < 1e-6 || h < 1e-6) continue;
        const double diameter = std::min(w, h);
        // Um circulo tem area = pi d^2/4. Desvio grande quer dizer que o vetor nao e redondo e
        // furar no centro dele provavelmente nao e o desejado.
        const double area = c->polygon.area();
        const bool round_enough = (std::fabs(w - h) <= std::max(tolerance * 4, 0.05 * std::max(w, h)) &&
                                   std::fabs(area - kPi * std::pow(diameter, 2.0) / 4) <= 0.08 * area);
        const Pt center = c->polygon.centroid();
        holes.push_back({center.x, center.y, diameter, round_enough});
        if (!round_enough) {
            notes.push_back(std::format("Vetor de {:.1f} × {:.1f} mm não é redondo — furado no centro "
                                        "mesmo assim; confira se era isso que você queria.",
                                        w, h));
        }
    }
    return {std::move(holes), std::move(notes)};
}

void peck(Emitter& em, const Hole& hole, const DrillSettings& s, double surface,
          const std::vector<double>& depths) {
    const double top = surface - s.start_depth;
    em.rapid_to(hole.x, hole.y);
    double previous_z = top;
    for (double depth : depths) {
        const double z = top - depth;
        em.plunge_to(z);
        if (s.dwell && s.dwell_time > 0) em.moves.push_back({MoveKind::Dwell, hole.x, hole.y, z, s.dwell_time});
        if (depth >= depths.back() - 1e-9) break;
        const double retract_z = s.retract_mode == RetractMode::AboveStart ? top + s.retract_gap
                                                                           : previous_z + s.retract_gap;
        em.force_z(retract_z);
        em.moves.push_back({MoveKind::Rapid, hole.x, hole.y, retract_z});
        previous_z = z;
    }
    em.retract();
}

// Desce em helice sobre a parede do furo e limpa o fundo no final.
void helical(Emitter& em, const Hole& hole, const Tool& tool, const DrillSettings& s, double surface,
             const std::vector<double>& depths) {
    const double radius = (*hole.diameter - tool.diameter) / 2.0;
    const double top = surface - s.start_depth;
    const double bottom = top - s.total_depth();
    const bool ccw = travel_ccw(false, s.helical_direction);  // vazio dentro: fresa por dentro
    const double sign = ccw ? 1.0 : -1.0;
    const int steps = std::max(24, segments_for(radius, s.tolerance));

    em.rapid_to(hole.x + radius, hole.y);
    em.plunge_to(top);

    const int turns = std::max(1, static_cast<int>(depths.size()));
    const int total_steps = steps * turns;
    for (int i = 1; i <= total_steps; ++i) {
        const double frac = static_cast<double>(i) / total_steps;
        const double ang = 2.0 * kPi * turns * frac * sign;
        const double z = top + (bottom - top) * frac;
        em.cut_to(hole.x + radius * std::cos(ang), hole.y + radius * std::sin(ang), z, true);
    }

    // Volta completa no fundo para deixar a parede reta.
    for (int i = 0; i <= steps; ++i) {
        const double ang = 2.0 * kPi * i / steps * sign;
        em.cut_to(hole.x + radius * std::cos(ang), hole.y + radius * std::sin(ang), bottom);
    }

    // Limpa o miolo que sobra quando o furo e mais que o dobro da fresa.
    if (radius > tool.radius()) {
        double r = radius - tool.stepover;
        while (r > 1e-6) {
            em.cut_to(hole.x + r, hole.y, bottom);
            for (int i = 1; i <= steps; ++i) {
                const double ang = 2.0 * kPi * i / steps * sign;
                em.cut_to(hole.x + r * std::cos(ang), hole.y + r * std::sin(ang), bottom);
            }
            r -= tool.stepover;
        }
        em.cut_to(hole.x, hole.y, bottom);
    }

    em.retract();
}

}  // namespace

std::vector<Toolpath> generate_drill(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                     const cad::MaterialSetup& material, const DrillSettings& s) {
    auto [holes, notes] = holes_from(entities, s.tolerance);
    const double surface = material.surface_z();
    const double clearance = surface + material.clearance_z;
    Emitter em(clearance, material.plunge_gap);

    if (holes.empty()) notes.push_back("Nenhum vetor fechado selecionado para furar.");

    std::vector<double> depths = compute_passes(s.total_depth(), s.peck ? tool.pass_depth : 0.0);
    if (depths.empty()) depths = {s.total_depth()};

    std::vector<Points> loops;
    for (const Hole& h : holes) loops.push_back({{h.x, h.y}, {h.x + 1e-6, h.y}});
    for (int idx : order_loops(loops, s.order)) {
        const Hole& hole = holes[static_cast<size_t>(idx)];
        if (hole.diameter && *hole.diameter < tool.diameter - s.tolerance) {
            notes.push_back(std::format("Furo de ⌀{:.2f} mm é menor que a ferramenta ⌀{:g} mm — ignorado.",
                                        *hole.diameter, tool.diameter));
            continue;
        }
        const bool use_helical = hole.diameter && s.helical && *hole.diameter > tool.diameter + 2 * s.tolerance;
        if (use_helical) {
            helical(em, hole, tool, s, surface, depths);
        } else {
            peck(em, hole, s, surface, depths);
        }
    }

    em.retract();
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Drill;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(em.moves);
    tp.notes = std::move(notes);
    return {std::move(tp)};
}

}  // namespace cnc::cam
