// Porte de src/viewport/snapping.py.
#include "viewport/snapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>

namespace cnc::vp {

const char* snap_label(SnapKind k) {
    switch (k) {
        case SnapKind::Node: return "nó";
        case SnapKind::Center: return "centro";
        case SnapKind::Midpoint: return "ponto médio";
        case SnapKind::Intersection: return "interseção";
        case SnapKind::OnSpan: return "sobre o vetor";
        case SnapKind::Grid: return "grelha";
        case SnapKind::Material: return "material";
    }
    return "";
}

int snap_priority(SnapKind k) {
    switch (k) {
        case SnapKind::Node: return 0;
        case SnapKind::Intersection: return 1;
        case SnapKind::Center: return 2;
        case SnapKind::Midpoint: return 3;
        case SnapKind::Material: return 4;
        case SnapKind::Grid: return 5;
        case SnapKind::OnSpan: return 6;
    }
    return 9;
}

Json SnapSettings::to_json() const {
    Json d = Json::object();
    d["enabled"] = enabled;
    d["geometry"] = geometry;
    d["grid"] = grid;
    d["material"] = material;
    d["nodes"] = nodes;
    d["centers"] = centers;
    d["midpoints"] = midpoints;
    d["intersections"] = intersections;
    d["on_span"] = on_span;
    d["radius_px"] = radius_px;
    d["grid_spacing"] = grid_spacing;
    d["show_label"] = show_label;
    return d;
}

SnapSettings SnapSettings::from_json(const Json& data) {
    SnapSettings s;
    if (!data.is_object()) return s;
    auto flag = [&](const char* k, bool& out) {
        if (auto it = data.find(k); it != data.end() && it->is_boolean()) out = it->get<bool>();
    };
    auto num = [&](const char* k, double& out) {
        if (auto it = data.find(k); it != data.end() && it->is_number()) out = it->get<double>();
    };
    flag("enabled", s.enabled);
    flag("geometry", s.geometry);
    flag("grid", s.grid);
    flag("material", s.material);
    flag("nodes", s.nodes);
    flag("centers", s.centers);
    flag("midpoints", s.midpoints);
    flag("intersections", s.intersections);
    flag("on_span", s.on_span);
    num("radius_px", s.radius_px);
    num("grid_spacing", s.grid_spacing);
    flag("show_label", s.show_label);
    return s;
}

// ======================================================================
// Pontos notaveis de uma entidade
// ======================================================================

std::vector<NotablePoint> entity_points(const cad::Entity& e, int segments) {
    std::vector<NotablePoint> out;

    if (auto* l = cad::as<cad::Line>(e)) {
        out.push_back({{l->x1, l->y1}, SnapKind::Node});
        out.push_back({{l->x2, l->y2}, SnapKind::Node});
        return out;
    }

    if (auto* c = cad::as<cad::Circle>(e)) {
        out.push_back({{c->cx, c->cy}, SnapKind::Center});
        for (double ang : {0.0, 90.0, 180.0, 270.0}) {
            const double r = radians(ang);
            out.push_back({{c->cx + c->radius * std::cos(r), c->cy + c->radius * std::sin(r)}, SnapKind::Node});
        }
        return out;
    }

    if (auto* el = cad::as<cad::Ellipse>(e)) {
        out.push_back({{el->cx, el->cy}, SnapKind::Center});
        const double a = radians(el->rotation);
        const double ca = std::cos(a), sa = std::sin(a);
        const double offsets[4][2] = {{el->rx, 0}, {-el->rx, 0}, {0, el->ry}, {0, -el->ry}};
        for (const auto& o : offsets) {
            out.push_back({{el->cx + o[0] * ca - o[1] * sa, el->cy + o[0] * sa + o[1] * ca}, SnapKind::Node});
        }
        return out;
    }

    if (auto* arc = cad::as<cad::Arc>(e)) {
        out.push_back({{arc->cx, arc->cy}, SnapKind::Center});
        const Points pts = arc->to_points(std::max(8, segments));
        if (!pts.empty()) {
            out.push_back({pts.front(), SnapKind::Node});
            out.push_back({pts.back(), SnapKind::Node});
            out.push_back({pts[pts.size() / 2], SnapKind::Midpoint});
        }
        return out;
    }

    if (auto* pl = cad::as<cad::Polyline>(e)) {
        for (const Pt& p : pl->points) out.push_back({p, SnapKind::Node});
        const long n = static_cast<long>(pl->points.size());
        const long limite = pl->is_closed ? n : n - 1;
        for (long i = 0; i < std::max(0L, limite); ++i) {
            const Pt& a = pl->points[static_cast<size_t>(i)];
            const Pt& b = pl->points[static_cast<size_t>((i + 1) % n)];
            out.push_back({{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, SnapKind::Midpoint});
        }
        if (pl->is_closed) out.push_back({e.center(), SnapKind::Center});
        return out;
    }

    if (auto* r = cad::as<cad::Rectangle>(e); r && r->corner_radius <= 0) {
        const double x1 = r->x, y1 = r->y;
        const double x2 = r->x + r->width, y2 = r->y + r->height;
        Pt corners[4] = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
        if (std::fabs(r->rotation) > 1e-9) {
            const double cx = r->x + r->width / 2.0, cy = r->y + r->height / 2.0;
            const double a = radians(r->rotation);
            for (auto& pt : corners) pt = cad::rot_pt(pt.x, pt.y, cx, cy, a);
        }
        for (int i = 0; i < 4; ++i) out.push_back({corners[i], SnapKind::Node});
        out.push_back({r->center(), SnapKind::Center});
        for (int i = 0; i < 4; ++i) {
            const Pt& a = corners[i];
            const Pt& b = corners[(i + 1) % 4];
            out.push_back({{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, SnapKind::Midpoint});
        }
        return out;
    }

    const Points pts = e.to_points(segments);
    if (pts.empty()) return out;

    // Texto, cota e o que mais vier: extremidades e meio.
    out.push_back({pts.front(), SnapKind::Node});
    out.push_back({pts.back(), SnapKind::Node});
    if (pts.size() > 2) out.push_back({pts[pts.size() / 2], SnapKind::Midpoint});
    if (e.closed()) out.push_back({e.center(), SnapKind::Center});
    return out;
}

std::optional<std::pair<Pt, double>> closest_on_entity(const cad::Entity& e, double x, double y, int segments) {
    if (auto* pl = cad::as<cad::Polyline>(e)) {
        const auto& pts = pl->points;
        if (pts.size() < 2) return std::nullopt;
        std::optional<Pt> best;
        double best_d = std::numeric_limits<double>::infinity();
        const size_t n = pts.size();
        const size_t count = e.closed() ? n : n - 1;
        for (size_t i = 0; i < count; ++i) {
            const Pt& a = pts[i];
            const Pt& b = pts[(i + 1) % n];
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double ll = dx * dx + dy * dy;
            if (ll < 1e-12) continue;
            const double t = std::max(0.0, std::min(1.0, ((x - a.x) * dx + (y - a.y) * dy) / ll));
            const double px = a.x + t * dx, py = a.y + t * dy;
            const double d = hypot(x - px, y - py);
            if (d < best_d) {
                best = Pt{px, py};
                best_d = d;
            }
        }
        if (!best) return std::nullopt;
        return std::make_pair(*best, best_d);
    }
    const Points pts = e.to_points(segments);
    if (pts.size() < 2) return std::nullopt;
    std::optional<Pt> best;
    double best_d = std::numeric_limits<double>::infinity();
    const size_t n = pts.size();
    const size_t count = e.closed() ? n : n - 1;
    for (size_t i = 0; i < count; ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[(i + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double ll = dx * dx + dy * dy;
        if (ll < 1e-12) continue;
        const double t = std::max(0.0, std::min(1.0, ((x - a.x) * dx + (y - a.y) * dy) / ll));
        const double px = a.x + t * dx, py = a.y + t * dy;
        const double d = hypot(x - px, y - py);
        if (d < best_d) {
            best = Pt{px, py};
            best_d = d;
        }
    }
    if (!best) return std::nullopt;
    return std::make_pair(*best, best_d);
}

namespace {

std::optional<Pt> cross(Pt p1, Pt p2, Pt p3, Pt p4) {
    const double den = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
    if (std::fabs(den) < 1e-12) return std::nullopt;
    const double t = ((p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x)) / den;
    const double u = ((p1.x - p3.x) * (p1.y - p2.y) - (p1.y - p3.y) * (p1.x - p2.x)) / den;
    if (!(0.0 <= t && t <= 1.0 && 0.0 <= u && u <= 1.0)) return std::nullopt;
    return Pt{p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
}

BBox bbox_of(const std::vector<Pt>& pts) {
    BBox b{pts[0].x, pts[0].y, pts[0].x, pts[0].y};
    for (const Pt& p : pts) {
        b.x0 = std::min(b.x0, p.x);
        b.y0 = std::min(b.y0, p.y);
        b.x1 = std::max(b.x1, p.x);
        b.y1 = std::max(b.y1, p.y);
    }
    return b;
}

bool boxes_overlap(const BBox& a, const BBox& b) {
    return a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
}

BBox merged_box(const BBox& a, const BBox& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

// Consulta apenas segmentos cujas caixas podem tocar o cursor/outro segmento.
// Os indices voltam na ordem original: empates e o limite de 33 intersecoes
// continuam iguais aos da varredura completa.
struct SegmentIndex {
    struct Node {
        BBox bounds;
        size_t begin = 0, end = 0;
        size_t left = 0, right = 0;
    };
    Points points;
    std::vector<BBox> bounds;
    std::vector<size_t> order;
    std::vector<Node> nodes;

    SegmentIndex(Points pts, bool closed) : points(std::move(pts)) {
        if (points.size() < 2) return;
        const size_t count = closed ? points.size() : points.size() - 1;
        bounds.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const Pt& a = points[i];
            const Pt& b = points[(i + 1) % points.size()];
            bounds.push_back({std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)});
        }
        order.resize(count);
        std::iota(order.begin(), order.end(), size_t{0});
        nodes.reserve(count * 2);
        build(0, count);
    }

    size_t build(size_t begin, size_t end) {
        BBox box = bounds[order[begin]];
        for (size_t i = begin + 1; i < end; ++i) box = merged_box(box, bounds[order[i]]);
        const size_t index = nodes.size();
        nodes.push_back({box, begin, end, 0, 0});
        if (end - begin <= 8) return index;
        const bool x_axis = box.x1 - box.x0 >= box.y1 - box.y0;
        const size_t middle = begin + (end - begin) / 2;
        std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                         [&](size_t a, size_t b) {
            const BBox& aa = bounds[a];
            const BBox& bb = bounds[b];
            return x_axis ? aa.x0 + aa.x1 < bb.x0 + bb.x1 : aa.y0 + aa.y1 < bb.y0 + bb.y1;
        });
        const size_t left = build(begin, middle);
        const size_t right = build(middle, end);
        nodes[index].left = left;
        nodes[index].right = right;
        return index;
    }

    void query_node(size_t index, const BBox& box, std::vector<size_t>& out) const {
        const Node& node = nodes[index];
        if (!boxes_overlap(node.bounds, box)) return;
        if (node.left != 0) {
            query_node(node.left, box, out);
            query_node(node.right, box, out);
        } else {
            for (size_t i = node.begin; i < node.end; ++i) {
                if (boxes_overlap(bounds[order[i]], box)) out.push_back(order[i]);
            }
        }
    }

    void query(const BBox& box, std::vector<size_t>& out) const {
        out.clear();
        if (!nodes.empty()) query_node(0, box, out);
        std::sort(out.begin(), out.end());
    }
};

std::vector<Pt> indexed_intersections(const SegmentIndex& a, const SegmentIndex& b) {
    std::vector<Pt> out;
    std::vector<size_t> nearby;
    for (size_t i = 0; i < a.bounds.size(); ++i) {
        b.query(a.bounds[i], nearby);
        for (const size_t j : nearby) {
            if (auto point = cross(a.points[i], a.points[(i + 1) % a.points.size()],
                                   b.points[j], b.points[(j + 1) % b.points.size()])) {
                out.push_back(*point);
                if (out.size() > 32) return out;
            }
        }
    }
    return out;
}

std::optional<std::pair<Pt, double>> indexed_closest(const SegmentIndex& path, double x, double y, double radius) {
    std::vector<size_t> nearby;
    path.query({x - radius, y - radius, x + radius, y + radius}, nearby);
    std::optional<Pt> best;
    double best_distance = std::numeric_limits<double>::infinity();
    for (const size_t i : nearby) {
        const Pt& a = path.points[i];
        const Pt& b = path.points[(i + 1) % path.points.size()];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double length_squared = dx * dx + dy * dy;
        if (length_squared < 1e-12) continue;
        const double t = std::max(0.0, std::min(1.0, ((x - a.x) * dx + (y - a.y) * dy) / length_squared));
        const Pt point{a.x + t * dx, a.y + t * dy};
        const double distance = hypot(x - point.x, y - point.y);
        if (distance < best_distance) {
            best = point;
            best_distance = distance;
        }
    }
    if (!best) return std::nullopt;
    return std::make_pair(*best, best_distance);
}

}  // namespace

std::vector<Pt> segment_intersections(const cad::Entity& a, const cad::Entity& b, int segments) {
    return indexed_intersections(SegmentIndex(a.to_points(segments), a.closed()),
                                 SegmentIndex(b.to_points(segments), b.closed()));
}

// ======================================================================
// Motor
// ======================================================================

struct SnapEngine::GeometryCache {
    struct Entry {
        cad::EntityPtr entity;
        uint64_t version = 0;
        BBox bounds;
        std::optional<std::vector<NotablePoint>> notable;
        std::optional<SegmentIndex> segments64;
        std::optional<SegmentIndex> segments96;

        const std::vector<NotablePoint>& points() {
            if (!notable) notable = entity_points(*entity);
            return *notable;
        }
        const SegmentIndex& path(int segments) {
            // Polilinhas usam os vertices originais em ambas as resolucoes.
            auto& cached = segments == 64 || cad::as<cad::Polyline>(*entity) ? segments64 : segments96;
            if (!cached) cached.emplace(entity->to_points(segments), entity->closed());
            return *cached;
        }
    };
    const cad::Document* document = nullptr;
    std::vector<Entry> entries;
    std::unordered_map<const cad::Entity*, size_t> by_entity;
    struct IntersectionEntry {
        uint64_t first_version = 0, second_version = 0;
        Points points;
    };
    std::map<std::pair<size_t, size_t>, IntersectionEntry> intersections;
};

SnapEngine::GeometryCache& SnapEngine::geometry_cache() const {
    if (!geometry_cache_) geometry_cache_ = std::make_shared<GeometryCache>();
    GeometryCache& cache = *geometry_cache_;
    if (cache.document != document) {
        cache = {};
        cache.document = document;
    }
    const size_t count = document ? document->entities.size() : 0;
    bool structure_changed = cache.entries.size() != count;
    cache.entries.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& entity = document->entities[i];
        auto& entry = cache.entries[i];
        if (entry.entity == entity && entry.version == entity->version) continue;
        if (entry.entity != entity) structure_changed = true;
        entry = {};
        entry.entity = entity;
        entry.version = entity->version;
        entry.bounds = entity->bounding_box();
    }
    if (structure_changed) {
        cache.intersections.clear();
        cache.by_entity.clear();
        for (size_t i = 0; i < cache.entries.size(); ++i) cache.by_entity.emplace(cache.entries[i].entity.get(), i);
    }
    return cache;
}

std::vector<cad::EntityPtr> SnapEngine::visible_entities(const std::set<std::string>& exclude) const {
    std::vector<cad::EntityPtr> out;
    if (!document) return out;
    for (const cad::EntityPtr& e : document->entities) {
        if (exclude.count(e->id)) continue;
        const cad::Layer* camada = document->get_layer(e->layer);
        if (camada && !camada->visible) continue;
        out.push_back(e);
    }
    return out;
}

bool SnapEngine::wants(SnapKind k) const {
    const SnapSettings& s = settings;
    switch (k) {
        case SnapKind::Node: return s.nodes;
        case SnapKind::Center: return s.centers;
        case SnapKind::Midpoint: return s.midpoints;
        case SnapKind::Intersection: return s.intersections;
        case SnapKind::OnSpan: return s.on_span;
        case SnapKind::Material: return s.material;
        case SnapKind::Grid: return s.grid;
    }
    return true;
}

std::vector<Pt> SnapEngine::material_points() const {
    if (!document) return {};
    const cad::MaterialSetup& m = document->material;
    const double w = m.width, h = m.height;
    std::vector<Pt> pontos = {{0.0, 0.0}, {w, 0.0}, {0.0, h}, {w, h}, {w / 2, h / 2},
                              {w / 2, 0.0}, {w / 2, h}, {0.0, h / 2}, {w, h / 2}};
    // A margem so vale como referencia se da para encostar nela.
    if (auto margem = m.margin_rect()) {
        const double x0 = margem->x0, y0 = margem->y0, x1 = margem->x1, y1 = margem->y1;
        const std::vector<Pt> extra = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}, {(x0 + x1) / 2, y0},
                                       {(x0 + x1) / 2, y1}, {x0, (y0 + y1) / 2}, {x1, (y0 + y1) / 2}};
        pontos.insert(pontos.end(), extra.begin(), extra.end());
    }
    return pontos;
}

std::optional<SnapResult> SnapEngine::snap(double wx, double wy, double px_per_mm,
                                           const std::set<std::string>& exclude) const {
    const SnapSettings& s = settings;
    if (!s.enabled || px_per_mm <= 0) return std::nullopt;
    const double raio = s.radius_px / px_per_mm;
    if (raio <= 0) return std::nullopt;

    std::vector<SnapResult> candidatos;
    std::vector<size_t> perto;

    if (s.geometry && document) {
        auto& cache = geometry_cache();
        for (size_t i = 0; i < cache.entries.size(); ++i) {
            auto& entry = cache.entries[i];
            const auto& e = entry.entity;
            const BBox& b = entry.bounds;
            if (wx < b.x0 - raio || wx > b.x1 + raio || wy < b.y0 - raio || wy > b.y1 + raio) continue;
            if (!exclude.empty() && exclude.count(e->id)) continue;
            const cad::Layer* camada = document->get_layer(e->layer);
            if (camada && !camada->visible) continue;

            perto.push_back(i);
            for (const auto& [ponto, tipo] : entry.points()) {
                if (!wants(tipo)) continue;
                const double d = dist(ponto, {wx, wy});
                if (d <= raio) candidatos.push_back({ponto.x, ponto.y, tipo, d, e});
            }
        }

        const bool has_node = std::any_of(candidatos.begin(), candidatos.end(), [](const SnapResult& candidate) {
            return candidate.kind == SnapKind::Node;
        });
        if (s.intersections && !has_node && perto.size() > 1) {
            for (size_t i = 0; i < perto.size(); ++i) {
                for (size_t j = i + 1; j < perto.size(); ++j) {
                    auto& a = cache.entries[perto[i]];
                    auto& b = cache.entries[perto[j]];
                    if (!boxes_overlap(a.bounds, b.bounds)) continue;
                    const auto key = std::make_pair(perto[i], perto[j]);
                    auto [found, inserted] = cache.intersections.try_emplace(key);
                    auto& intersections = found->second;
                    if (inserted || intersections.first_version != a.version || intersections.second_version != b.version) {
                        intersections.points = indexed_intersections(a.path(64), b.path(64));
                        intersections.first_version = a.version;
                        intersections.second_version = b.version;
                    }
                    for (const Pt& ponto : intersections.points) {
                        const double d = dist(ponto, {wx, wy});
                        if (d <= raio) candidatos.push_back({ponto.x, ponto.y, SnapKind::Intersection, d, a.entity});
                    }
                }
            }
        }
    }

    if (s.material && document) {
        for (const Pt& ponto : material_points()) {
            const double d = dist(ponto, {wx, wy});
            if (d <= raio) candidatos.push_back({ponto.x, ponto.y, SnapKind::Material, d, nullptr});
        }
    }

    if (s.grid && s.grid_spacing > 0) {
        const double gx = py_round(wx / s.grid_spacing) * s.grid_spacing;
        const double gy = py_round(wy / s.grid_spacing) * s.grid_spacing;
        const double d = dist({gx, gy}, {wx, wy});
        if (d <= raio) candidatos.push_back({gx, gy, SnapKind::Grid, d, nullptr});
    }

    // Ultimo recurso: um ponto qualquer sobre o contorno.
    if (s.geometry && s.on_span && candidatos.empty()) {
        auto& cache = geometry_cache();
        for (const size_t i : perto) {
            auto& entry = cache.entries[i];
            if (auto achado = indexed_closest(entry.path(96), wx, wy, raio); achado && achado->second <= raio) {
                candidatos.push_back({achado->first.x, achado->first.y, SnapKind::OnSpan, achado->second, entry.entity});
            }
        }
    }

    if (candidatos.empty()) return std::nullopt;
    const SnapResult* best = &candidatos[0];
    for (const SnapResult& c : candidatos) {
        const auto key_c = std::make_pair(snap_priority(c.kind), c.distance);
        const auto key_b = std::make_pair(snap_priority(best->kind), best->distance);
        if (key_c < key_b) best = &c;
    }
    return *best;
}

MovingSnap SnapEngine::snap_moving(const std::vector<cad::EntityPtr>& moving, double dx, double dy,
                                   double px_per_mm, const std::optional<Pt>& anchor) const {
    const SnapSettings& s = settings;
    if (!s.enabled || !s.geometry || moving.empty() || px_per_mm <= 0) return {dx, dy, std::nullopt};
    const double raio = s.radius_px / px_per_mm;

    std::set<std::string> ids;
    for (const cad::EntityPtr& e : moving) ids.insert(e->id);
    auto& cache = geometry_cache();

    std::vector<NotablePoint> todos;
    for (const cad::EntityPtr& e : moving) {
        auto append = [&](const auto& points) {
            for (const auto& [ponto, tipo] : points) {
                if (wants(tipo)) todos.push_back({{ponto.x + dx, ponto.y + dy}, tipo});
            }
        };
        const auto found = cache.by_entity.find(e.get());
        if (found != cache.by_entity.end()) append(cache.entries[found->second].points());
        else append(entity_points(*e));
    }
    if (todos.empty()) return {dx, dy, std::nullopt};

    std::vector<Pt> only_points;
    for (const auto& [p, t] : todos) only_points.push_back(p);
    const BBox caixa = bbox_of(only_points);
    std::vector<NotablePoint> fixos;
    bool has_targets = false;
    for (auto& entry : cache.entries) {
        const auto& e = entry.entity;
        if (ids.count(e->id)) continue;
        const auto* layer = document->get_layer(e->layer);
        if (layer && !layer->visible) continue;
        has_targets = true;
        const BBox& b = entry.bounds;
        if (b.x1 + raio < caixa.x0 || b.x0 - raio > caixa.x1 || b.y1 + raio < caixa.y0 || b.y0 - raio > caixa.y1) continue;
        for (const auto& np : entry.points()) {
            if (wants(np.second)) fixos.push_back(np);
        }
    }
    if (!has_targets) return {dx, dy, std::nullopt};
    if (s.material && document) {
        for (const Pt& p : material_points()) fixos.push_back({p, SnapKind::Material});
    }
    if (fixos.empty()) return {dx, dy, std::nullopt};

    // Uma busca por faixa de X evita comparar todos os vertices moveis com
    // todos os fixos (milhoes de hypot a cada evento em chapas detalhadas).
    std::vector<size_t> order(fixos.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return fixos[a].first.x < fixos[b].first.x; });

    auto search = [&](const auto& moveis) -> std::optional<MovingSnap> {
        struct Best {
            std::tuple<int, double, size_t, size_t> key;
            double corr_x, corr_y, fx, fy;
            SnapKind tipo;
        };
        std::optional<Best> melhor;
        for (size_t mi = 0; mi < moveis.size(); ++mi) {
            const auto& [m, tipo_m] = moveis[mi];
            auto first = std::lower_bound(order.begin(), order.end(), m.x - raio,
                                         [&](size_t i, double x) { return fixos[i].first.x < x; });
            for (auto it = first; it != order.end(); ++it) {
                const size_t fi = *it;
                const auto& [f, tipo_f] = fixos[fi];
                if (f.x > m.x + raio) break;
                if (std::fabs(f.y - m.y) > raio) continue;
                const double d = hypot(f.x - m.x, f.y - m.y);
                if (d > raio) continue;
                const std::tuple<int, double, size_t, size_t> chave{
                    std::min(snap_priority(tipo_m), snap_priority(tipo_f)), d, mi, fi};
                if (!melhor || chave < melhor->key) {
                    melhor = Best{chave, f.x - m.x, f.y - m.y, f.x, f.y,
                                  snap_priority(tipo_f) <= snap_priority(tipo_m) ? tipo_f : tipo_m};
                }
            }
        }
        if (melhor) {
            return MovingSnap{dx + melhor->corr_x, dy + melhor->corr_y,
                              SnapResult{melhor->fx, melhor->fy, melhor->tipo, 0.0, nullptr}};
        }
        return std::nullopt;
    };
    if (anchor) {
        const std::array<NotablePoint, 1> anchor_points{{{{anchor->x + dx, anchor->y + dy}, SnapKind::Node}}};
        if (auto found = search(anchor_points)) return *found;
    }
    if (auto found = search(todos)) return *found;
    return {dx, dy, std::nullopt};
}

}  // namespace cnc::vp
