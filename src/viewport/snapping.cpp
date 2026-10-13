// Porte de src/viewport/snapping.py.
#include "viewport/snapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

    const Points pts = e.to_points(segments);
    if (pts.empty()) return out;

    if (auto* r = cad::as<cad::Rectangle>(e); r && r->corner_radius <= 0) {
        const size_t n = pts.size() >= 4 ? 4 : pts.size();
        for (size_t i = 0; i < n; ++i) out.push_back({pts[i], SnapKind::Node});
        out.push_back({e.center(), SnapKind::Center});
        for (size_t i = 0; i < n; ++i) {
            const Pt& a = pts[i];
            const Pt& b = pts[(i + 1) % n];
            out.push_back({{(a.x + b.x) / 2, (a.y + b.y) / 2}, SnapKind::Midpoint});
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
            out.push_back({{(a.x + b.x) / 2, (a.y + b.y) / 2}, SnapKind::Midpoint});
        }
        if (pl->is_closed) out.push_back({e.center(), SnapKind::Center});
        return out;
    }

    // Linha, texto, cota e o que mais vier: extremidades e meio.
    out.push_back({pts.front(), SnapKind::Node});
    out.push_back({pts.back(), SnapKind::Node});
    if (pts.size() > 2) out.push_back({pts[pts.size() / 2], SnapKind::Midpoint});
    if (e.closed()) out.push_back({e.center(), SnapKind::Center});
    return out;
}

std::optional<std::pair<Pt, double>> closest_on_entity(const cad::Entity& e, double x, double y, int segments) {
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

}  // namespace

std::vector<Pt> segment_intersections(const cad::Entity& a, const cad::Entity& b, int segments) {
    const Points pa = a.to_points(segments), pb = b.to_points(segments);
    if (pa.size() < 2 || pb.size() < 2) return {};
    std::vector<Pt> out;
    const size_t na = a.closed() ? pa.size() : pa.size() - 1;
    const size_t nb = b.closed() ? pb.size() : pb.size() - 1;
    for (size_t i = 0; i < na; ++i) {
        const Pt& p1 = pa[i];
        const Pt& p2 = pa[(i + 1) % pa.size()];
        for (size_t j = 0; j < nb; ++j) {
            const Pt& p3 = pb[j];
            const Pt& p4 = pb[(j + 1) % pb.size()];
            if (auto ponto = cross(p1, p2, p3, p4)) {
                out.push_back(*ponto);
                if (out.size() > 32) return out;  // mais que isso nao ajuda ninguem
            }
        }
    }
    return out;
}

// ======================================================================
// Motor
// ======================================================================

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
    std::vector<cad::EntityPtr> perto;

    if (s.geometry) {
        for (const cad::EntityPtr& e : visible_entities(exclude)) {
            const BBox b = e->bounding_box();
            if (!(b.x0 - raio <= wx && wx <= b.x1 + raio && b.y0 - raio <= wy && wy <= b.y1 + raio)) continue;
            perto.push_back(e);
            for (const auto& [ponto, tipo] : entity_points(*e)) {
                if (!wants(tipo)) continue;
                const double d = dist(ponto, {wx, wy});
                if (d <= raio) candidatos.push_back({ponto.x, ponto.y, tipo, d, e});
            }
        }

        if (s.intersections && perto.size() > 1) {
            for (size_t i = 0; i < perto.size(); ++i) {
                for (size_t j = i + 1; j < perto.size(); ++j) {
                    for (const Pt& ponto : segment_intersections(*perto[i], *perto[j])) {
                        const double d = dist(ponto, {wx, wy});
                        if (d <= raio) candidatos.push_back({ponto.x, ponto.y, SnapKind::Intersection, d, perto[i]});
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
        for (const cad::EntityPtr& e : perto) {
            if (auto achado = closest_on_entity(*e, wx, wy); achado && achado->second <= raio) {
                candidatos.push_back({achado->first.x, achado->first.y, SnapKind::OnSpan, achado->second, e});
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
    const std::vector<cad::EntityPtr> alvos = visible_entities(ids);
    if (alvos.empty()) return {dx, dy, std::nullopt};

    std::vector<NotablePoint> todos;
    for (const cad::EntityPtr& e : moving) {
        for (const auto& [ponto, tipo] : entity_points(*e)) {
            if (wants(tipo)) todos.push_back({{ponto.x + dx, ponto.y + dy}, tipo});
        }
    }
    if (todos.empty()) return {dx, dy, std::nullopt};

    std::vector<Pt> only_points;
    for (const auto& [p, t] : todos) only_points.push_back(p);
    const BBox caixa = bbox_of(only_points);
    std::vector<NotablePoint> fixos;
    for (const cad::EntityPtr& e : alvos) {
        const BBox b = e->bounding_box();
        if (b.x1 + raio < caixa.x0 || b.x0 - raio > caixa.x1 || b.y1 + raio < caixa.y0 || b.y0 - raio > caixa.y1) continue;
        for (const auto& np : entity_points(*e)) {
            if (wants(np.second)) fixos.push_back(np);
        }
    }
    if (s.material && document) {
        for (const Pt& p : material_points()) fixos.push_back({p, SnapKind::Material});
    }
    if (fixos.empty()) return {dx, dy, std::nullopt};

    std::vector<std::vector<NotablePoint>> tentativas;
    if (anchor) tentativas.push_back({{{anchor->x + dx, anchor->y + dy}, SnapKind::Node}});
    tentativas.push_back(todos);

    for (const auto& moveis : tentativas) {
        struct Best {
            std::pair<int, double> key;
            double corr_x, corr_y, fx, fy;
            SnapKind tipo;
        };
        std::optional<Best> melhor;
        for (const auto& [m, tipo_m] : moveis) {
            for (const auto& [f, tipo_f] : fixos) {
                const double d = hypot(f.x - m.x, f.y - m.y);
                if (d > raio) continue;
                const std::pair<int, double> chave{std::min(snap_priority(tipo_m), snap_priority(tipo_f)), d};
                if (!melhor || chave < melhor->key) {
                    melhor = Best{chave, f.x - m.x, f.y - m.y, f.x, f.y,
                                  snap_priority(tipo_f) <= snap_priority(tipo_m) ? tipo_f : tipo_m};
                }
            }
        }
        if (melhor) {
            return {dx + melhor->corr_x, dy + melhor->corr_y,
                    SnapResult{melhor->fx, melhor->fy, melhor->tipo, 0.0, nullptr}};
        }
    }
    return {dx, dy, std::nullopt};
}

}  // namespace cnc::vp
