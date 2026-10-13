// Porte de src/cam/nesting.py.
#include "cam/nesting.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace cnc::cam {

int segments_for(double radius, double tolerance) {
    if (radius <= tolerance) return 8;
    const double ratio = std::max(-1.0, std::min(1.0, 1.0 - tolerance / radius));
    const double step = 2.0 * std::acos(ratio);
    if (step <= 1e-9) return 2048;
    return std::max(8, std::min(4096, static_cast<int>(std::ceil(2.0 * kPi / step))));
}

namespace {

double entity_radius(const cad::Entity& e) {
    if (auto* c = cad::as<cad::Circle>(e)) return c->radius;
    if (auto* a = cad::as<cad::Arc>(e)) return a->radius;
    if (auto* el = cad::as<cad::Ellipse>(e)) return std::max(el->rx, el->ry);
    if (auto* r = cad::as<cad::Rectangle>(e); r && r->corner_radius > 0) return r->corner_radius;
    const BBox b = e.bounding_box();
    return std::max(b.x1 - b.x0, b.y1 - b.y0) / 2.0;
}

// Polygon(pts) do shapely; se for invalido, .buffer(0) conserta (auto-intersecoes).
geo::Geom valid_polygon(const Points& pts) {
    geo::Geom poly = geo::make_polygon(pts);
    if (!poly) return {};
    if (!poly.is_valid()) poly = poly.buffer(0);
    return poly;
}

}  // namespace

int entity_segments(const cad::Entity& e, double tolerance) {
    return segments_for(entity_radius(e), tolerance);
}

geo::Geom entity_geometry(const cad::Entity& e, double tolerance) {
    if (auto* text = cad::as<cad::Text>(e)) {
        geo::Geom result;
        for (const Points& ring : text->outline_polygons()) {
            if (ring.size() < 3) continue;
            geo::Geom poly = valid_polygon(ring);
            if (!poly) continue;
            result = result ? result.symmetric_difference(poly) : std::move(poly);
        }
        return (result && !result.is_empty()) ? result : geo::Geom{};
    }

    const Points pts = e.to_points(entity_segments(e, tolerance));
    if (pts.size() < 2) return {};

    if (e.closed() && pts.size() >= 3) {
        geo::Geom poly = valid_polygon(pts);
        if (!poly || poly.is_empty()) return {};
        return poly;
    }
    return geo::make_linestring(pts);
}

geo::Geom Region::to_polygon() const {
    if (holes.empty()) return outer->polygon.shell_polygon();
    std::vector<Points> hole_rings;
    for (const ContourPtr& h : holes) hole_rings.push_back(h->polygon.exterior_coords());
    return geo::make_polygon(outer->polygon.exterior_coords(), hole_rings);
}

SplitResult split_entities(const std::vector<cad::EntityPtr>& entities, double tolerance) {
    SplitResult out;
    for (const cad::EntityPtr& e : entities) {
        geo::Geom geom = entity_geometry(*e, tolerance);
        if (!geom || geom.is_empty()) continue;
        const std::string type = geom.geom_type();
        if (type == "LineString" || type == "LinearRing") {
            if (geom.length() > 1e-9) out.open_paths.push_back({e, std::move(geom)});
            continue;
        }
        std::vector<geo::Geom> polys;
        if (type == "MultiPolygon") {
            polys = geom.geoms();
        } else {
            polys.push_back(std::move(geom));
        }
        for (const geo::Geom& poly : polys) {
            if (poly.geom_type() != "Polygon" || poly.area() <= 1e-9) continue;
            auto outer = std::make_shared<Contour>();
            outer->entity = e;
            outer->polygon = poly.shell_polygon();
            out.closed.push_back(std::move(outer));
            for (const Points& ring : poly.interior_coords()) {
                geo::Geom hole = geo::make_polygon(ring);
                if (hole && hole.area() > 1e-9) {
                    auto c = std::make_shared<Contour>();
                    c->entity = e;
                    c->polygon = std::move(hole);
                    out.closed.push_back(std::move(c));
                }
            }
        }
    }
    return out;
}

void classify(std::vector<ContourPtr>& contours) {
    if (contours.size() < 2) {
        for (const ContourPtr& c : contours) c->level = 0;
        return;
    }

    geo::BoxIndex tree;
    std::vector<double> areas;
    std::vector<geo::Geom> probes;
    areas.reserve(contours.size());
    for (const ContourPtr& c : contours) {
        tree.add(c->polygon.bounds());
        areas.push_back(c->polygon.area());
        probes.push_back(geo::make_point(c->polygon.representative_point()));
    }

    for (size_t i = 0; i < contours.size(); ++i) {
        const Pt probe = contours[i]->polygon.representative_point();
        int level = 0;
        // O indice devolve so os candidatos cuja caixa toca o ponto - e o filtro rapido.
        // A resposta vem sempre do contorno real.
        for (int j : tree.query({probe.x, probe.y, probe.x, probe.y})) {
            if (static_cast<size_t>(j) == i) continue;
            if (areas[static_cast<size_t>(j)] <= areas[i]) continue;
            if (contours[static_cast<size_t>(j)]->polygon.contains(probes[i])) ++level;
        }
        contours[i]->level = level;
    }
}

std::vector<Region> build_regions(const std::vector<ContourPtr>& contours) {
    std::vector<Region> regions;
    std::vector<ContourPtr> holes;
    for (const ContourPtr& c : contours) {
        if (c->is_hole()) {
            holes.push_back(c);
        } else {
            regions.push_back(Region{c, {}});
        }
    }
    if (regions.empty()) return {};

    geo::BoxIndex tree;
    std::vector<double> areas;
    for (const Region& r : regions) {
        tree.add(r.outer->polygon.bounds());
        areas.push_back(r.outer->polygon.area());
    }

    for (const ContourPtr& hole : holes) {
        const Pt probe_pt = hole->polygon.representative_point();
        const geo::Geom probe = geo::make_point(probe_pt);
        int best = -1;
        for (int idx : tree.query({probe_pt.x, probe_pt.y, probe_pt.x, probe_pt.y})) {
            const Region& cand = regions[static_cast<size_t>(idx)];
            if (cand.outer->level != hole->level - 1) continue;
            if (!cand.outer->polygon.contains(probe)) continue;
            if (best < 0 || areas[static_cast<size_t>(idx)] < areas[static_cast<size_t>(best)]) {
                best = idx;
            }
        }
        if (best >= 0) regions[static_cast<size_t>(best)].holes.push_back(hole);
    }
    return regions;
}

RegionsResult regions_from(const std::vector<cad::EntityPtr>& entities, double tolerance) {
    SplitResult split = split_entities(entities, tolerance);
    classify(split.closed);
    return {build_regions(split.closed), std::move(split.open_paths)};
}

std::vector<std::string> nesting_report(const std::vector<ContourPtr>& contours) {
    std::vector<ContourPtr> sorted = contours;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const ContourPtr& a, const ContourPtr& b) { return -a->area() < -b->area(); });
    std::vector<std::string> lines;
    for (const ContourPtr& c : sorted) {
        const BBox b = c->polygon.bounds();
        const char* kind = c->is_hole() ? "FURO  (corta por dentro)" : "PEÇA  (corta por fora)";
        lines.push_back(std::format("{}  nível {}  {:8.2f} × {:8.2f} mm  área {:10.1f} mm²", kind,
                                    c->level, b.x1 - b.x0, b.y1 - b.y0, c->area()));
    }
    return lines;
}

}  // namespace cnc::cam
