// Porte de src/cam/pocket.py.
#include "cam/pocket.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace cnc::cam {

namespace {

// ======================================================================
// Preenchimentos
// ======================================================================

// Aneis concentricos do centro para fora. Sair do miolo para a parede deixa sempre espaco
// livre ao lado da fresa, que e por onde o cavaco escapa.
std::vector<Points> offset_rings(const geo::Geom& poly, const Tool& tool, const PocketSettings& s,
                                 double stepover) {
    struct Item {
        int k;
        Points ring;
    };
    std::vector<Item> rings;
    int k = 0;
    while (k < 10000) {
        const double delta = -(tool.radius() + s.allowance + k * stepover);
        const std::vector<Points> step_rings = offset_polygon(poly, delta, s.tolerance);
        if (step_rings.empty()) break;
        for (const Points& ring : step_rings) {
            if (ring.size() >= 3) rings.push_back({k, ring});
        }
        ++k;
    }
    std::stable_sort(rings.begin(), rings.end(), [](const Item& a, const Item& b) { return a.k > b.k; });
    std::vector<Points> out;
    out.reserve(rings.size());
    for (Item& it : rings) out.push_back(std::move(it.ring));
    return out;
}

// Varredura paralela em ziguezague dentro do poligono.
std::vector<Points> raster_lines(const geo::Geom& poly, double stepover, double angle_deg,
                                 double tolerance) {
    if (poly.is_empty() || stepover <= 1e-9) return {};

    const Pt c = poly.centroid();
    const double cx = c.x, cy = c.y;
    const geo::Geom flat = poly.rotated(-angle_deg, {cx, cy});
    const BBox b = flat.bounds();
    if (b.x1 - b.x0 < tolerance || b.y1 - b.y0 < tolerance) return {};

    std::vector<Points> segments;
    double y = b.y0 + stepover * 0.5;
    bool flip = false;
    while (y < b.y1) {
        const geo::Geom line = geo::make_linestring({{b.x0 - 1.0, y}, {b.x1 + 1.0, y}});
        const geo::Geom clipped = line.intersection(flat);
        if (clipped && !clipped.is_empty()) {
            const std::string type = clipped.geom_type();
            std::vector<geo::Geom> parts;
            if (type.rfind("Multi", 0) == 0) {
                parts = clipped.geoms();
            } else {
                parts.push_back(clipped);
            }
            std::vector<Points> runs;
            for (const geo::Geom& part : parts) {
                if (part.geom_type() != "LineString" || part.length() <= tolerance) continue;
                runs.push_back(part.coords());
            }
            std::stable_sort(runs.begin(), runs.end(), [&](const Points& a, const Points& bb) {
                return flip ? a[0].x > bb[0].x : a[0].x < bb[0].x;
            });
            for (Points& coords : runs) {
                if (flip) std::reverse(coords.begin(), coords.end());
                segments.push_back(std::move(coords));
            }
        }
        flip = !flip;
        y += stepover;
    }

    if (segments.empty()) return {};

    // Volta para o referencial do desenho, girando em torno do mesmo centro.
    const double ang = radians(angle_deg);
    const double ca = std::cos(ang), sa = std::sin(ang);
    std::vector<Points> out;
    for (const Points& coords : segments) {
        Points pts;
        pts.reserve(coords.size());
        for (const Pt& p : coords) {
            pts.push_back({cx + (p.x - cx) * ca - (p.y - cy) * sa, cy + (p.x - cx) * sa + (p.y - cy) * ca});
        }
        out.push_back(std::move(pts));
    }
    return out;
}

std::optional<geo::Geom> shrunk(const geo::Geom& poly, double distance, double tolerance) {
    const std::vector<Points> rings = offset_polygon(poly, -distance, tolerance);
    if (rings.empty()) return std::nullopt;
    std::vector<geo::Geom> outers, holes;
    for (const Points& r : rings) {
        geo::Geom g = geo::make_polygon(r);
        if (!g) continue;
        (is_ccw(r) ? outers : holes).push_back(std::move(g));
    }
    if (outers.empty()) return std::nullopt;
    geo::Geom result = outers.size() == 1 ? outers[0] : geo::make_multipolygon(outers).buffer(0);
    for (const geo::Geom& h : holes) result = result.difference(h);
    if (!result || result.is_empty()) return std::nullopt;
    return result;
}

// ======================================================================
// Entrada e corte
// ======================================================================

void ramp_along(Emitter& em, const Points& pts, double length, double z_from, double z_to) {
    double acc = 0.0;
    Pt prev = em.position();
    for (const Pt& pt : pts) {
        acc += dist(prev, pt);
        prev = pt;
        const double t = length > 1e-9 ? std::min(1.0, acc / length) : 1.0;
        em.cut_to(pt.x, pt.y, z_from + (z_to - z_from) * t, true);
    }
}

// Entra no proximo trecho: corta em linha reta se couber, senao levanta.
void enter(Emitter& em, const Points& pts, double z_prev, double z, const PocketSettings& s,
           const geo::Geom& safe) {
    const Pt cur = em.position();
    bool link_ok = !em.moves.empty() && em.moves.back().kind != MoveKind::Rapid &&
                   std::fabs(em.z() - z) < 1e-9;
    if (link_ok) {
        const geo::Geom link = geo::make_linestring({cur, pts[0]});
        link_ok = link && safe.contains(link);
    }
    if (link_ok) {
        em.cut_to(pts[0].x, pts[0].y, z);
        return;
    }

    em.rapid_to(pts[0].x, pts[0].y);
    const double drop = z_prev - z;
    double ramp_len = s.ramp != RampType::None ? s.ramp_distance : 0.0;
    ramp_len = std::min(ramp_len, path_length(pts) * 0.5);
    if (ramp_len <= 1e-9 || drop <= 1e-9) {
        em.plunge_to(z);
        return;
    }

    // Rampa em ziguezague: desce indo, desce voltando, e termina no ponto de partida ja na
    // profundidade - sem deixar trecho cortado pela metade.
    em.plunge_to(z_prev);
    const Points ramp = slice_path(pts, 0.0, ramp_len);
    const double mid_z = (z_prev + z) / 2.0;
    ramp_along(em, Points(ramp.begin() + 1, ramp.end()), ramp_len, z_prev, mid_z);
    Points back(ramp.rbegin(), ramp.rend());
    ramp_along(em, Points(back.begin() + 1, back.end()), ramp_len, mid_z, z);
}

void cut_rings(Emitter& em, const geo::Geom& poly, const std::vector<Points>& rings, double z_prev,
               double z, const PocketSettings& s) {
    const geo::Geom safe = poly.buffer(1e-6);
    for (const Points& ring : rings) {
        const bool material_inside = !is_ccw(ring);  // ilha = material dentro do anel
        const bool want_ccw = travel_ccw(material_inside, s.direction);
        const Points pts = close_ring(orient_ring(ring, want_ccw));
        if (path_length(pts) < 1e-6) continue;
        enter(em, pts, z_prev, z, s, safe);
        for (size_t i = 1; i < pts.size(); ++i) em.cut_to(pts[i].x, pts[i].y, z);
    }
}

void cut_lines(Emitter& em, const geo::Geom& poly, const std::vector<Points>& lines, double z_prev,
               double z, const PocketSettings& s) {
    const geo::Geom safe = poly.buffer(1e-6);
    for (const Points& pts : lines) {
        if (pts.size() < 2) continue;
        enter(em, pts, z_prev, z, s, safe);
        for (size_t i = 1; i < pts.size(); ++i) em.cut_to(pts[i].x, pts[i].y, z);
    }
}

void machine_level(Emitter& em, const geo::Geom& poly, const std::vector<Points>& rings,
                   const std::vector<Points>& profile_rings, double z_prev, double z, const Tool& tool,
                   const PocketSettings& s, double stepover) {
    if (s.profile_pass == ProfilePass::First) cut_rings(em, poly, profile_rings, z_prev, z, s);

    if (s.fill == FillType::Offset) {
        cut_rings(em, poly, rings, z_prev, z, s);
    } else {
        const std::optional<geo::Geom> inner = shrunk(poly, tool.radius() + s.allowance, s.tolerance);
        const std::vector<Points> lines =
            inner ? raster_lines(*inner, stepover, s.raster_angle, s.tolerance) : std::vector<Points>{};
        cut_lines(em, poly, lines, z_prev, z, s);
    }

    if (s.profile_pass == ProfilePass::Last) cut_rings(em, poly, profile_rings, z_prev, z, s);
}

}  // namespace

// ======================================================================
// Geracao
// ======================================================================

std::vector<Toolpath> generate_pocket(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const PocketSettings& s) {
    const RegionsResult found = regions_from(entities, s.tolerance);
    std::vector<geo::Geom> polys;
    for (const Region& r : found.regions) polys.push_back(r.to_polygon());
    return generate_pocket_polygons(polys, tool, material, s);
}

std::vector<Toolpath> generate_pocket_polygons(const std::vector<geo::Geom>& polys, const Tool& tool,
                                               const cad::MaterialSetup& material, const PocketSettings& s) {
    const double surface = material.surface_z();
    const double clearance = surface + material.clearance_z;
    const double plunge_gap = material.plunge_gap;
    const double stepover = s.stepover > 1e-9 ? s.stepover : tool.stepover;

    Emitter em(clearance, plunge_gap);
    std::vector<std::string> notes;
    if (tool.needs_tails()) {
        notes.push_back(std::format(
            "Passo lateral de {:g} mm passa de 50% da fresa ⌀{:g} mm — pode sobrar material nos cantos.",
            stepover, tool.diameter));
    }

    const std::vector<double> depths = compute_passes(s.total_depth(), tool.pass_depth, s.maintain_exact_step);
    if (polys.empty()) notes.push_back("Nenhum vetor fechado selecionado — o bolso precisa de área.");

    std::vector<Points> exteriors;
    for (const geo::Geom& p : polys) exteriors.push_back(p.exterior_coords());
    for (int idx : order_loops(exteriors, s.order)) {
        const geo::Geom& poly = polys[static_cast<size_t>(idx)];
        const std::vector<Points> rings = offset_rings(poly, tool, s, stepover);
        if (rings.empty()) {
            const BBox b = poly.bounds();
            notes.push_back(std::format("Fresa ⌀{:g} mm não entra no bolso de {:.1f} × {:.1f} mm.",
                                        tool.diameter, b.x1 - b.x0, b.y1 - b.y0));
            continue;
        }

        const std::vector<Points> profile_ring = offset_polygon(poly, -tool.radius(), s.tolerance);
        double z_prev = surface - s.start_depth;
        for (double depth : depths) {
            const double z = surface - s.start_depth - depth;
            machine_level(em, poly, rings, profile_ring, z_prev, z, tool, s, stepover);
            z_prev = z;
        }
    }

    em.retract();
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Pocket;
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
