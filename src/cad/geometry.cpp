// Porte de src/cad/geometry.py.
#include "cad/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cnc::cad {

namespace {

// a % n do Python: resultado nunca negativo para n positivo.
inline long pymod(long a, long n) {
    long r = a % n;
    return (r != 0 && ((r < 0) != (n < 0))) ? r + n : r;
}

struct SegHit {
    Pt point;
    double t;
    double u;
};

std::optional<SegHit> seg_intersect(const Pt& p1, const Pt& p2, const Pt& p3, const Pt& p4) {
    const double x1 = p1.x, y1 = p1.y, x2 = p2.x, y2 = p2.y;
    const double x3 = p3.x, y3 = p3.y, x4 = p4.x, y4 = p4.y;
    const double d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::fabs(d) < 1e-12) return std::nullopt;
    const double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / d;
    const double u = ((x1 - x3) * (y1 - y2) - (y1 - y3) * (x1 - x2)) / d;
    return SegHit{{x1 + t * (x2 - x1), y1 + t * (y2 - y1)}, t, u};
}

}  // namespace

// ======================================================================
// Conversao entidade <-> GEOS
// ======================================================================

geo::Geom to_shapely(const Entity& e) {
    if (const auto* text = dynamic_cast<const Text*>(&e)) {
        geo::Geom result;
        for (const Points& ring : text->outline_polygons()) {
            if (ring.size() < 3) continue;
            geo::Geom polygon = geo::make_polygon(ring);
            if (!polygon) continue;
            if (!polygon.is_valid()) polygon = polygon.buffer(0.0);
            result = result ? result.symmetric_difference(polygon) : polygon;
        }
        return result;
    }
    const Points pts = e.to_points(kSeg);
    if (pts.size() < 2) return {};
    if (e.closed() && pts.size() >= 3) {
        geo::Geom poly = geo::make_polygon(pts);
        if (!poly) return {};
        if (!poly.is_valid()) poly = poly.buffer(0.0);
        if (!poly || poly.is_empty()) return {};
        return poly;
    }
    return geo::make_linestring(pts);
}

namespace {

Points drop_last(Points pts) {
    if (!pts.empty()) pts.pop_back();
    return pts;
}

std::shared_ptr<Polyline> make_poly(Points pts, bool closed, const std::string& layer, const Rgb& color) {
    auto p = std::make_shared<Polyline>();
    p->points = std::move(pts);
    p->is_closed = closed;
    p->layer = layer;
    p->color = color;
    return p;
}

void from_shapely_into(const geo::Geom& geom, const Entity* tmpl, std::vector<EntityPtr>& out) {
    if (!geom || geom.is_empty()) return;
    const std::string layer = tmpl ? tmpl->layer : "Camada 1";
    const Rgb color = tmpl ? tmpl->color : Rgb{0.1, 0.1, 0.1};

    const std::string gt = geom.geom_type();
    if (gt == "Polygon") {
        out.push_back(make_poly(drop_last(geom.exterior_coords()), true, layer, color));
        for (const Points& ring : geom.interior_coords()) out.push_back(make_poly(drop_last(ring), true, layer, color));
    } else if (gt == "LineString") {
        out.push_back(make_poly(geom.coords(), false, layer, color));
    } else if (gt == "LinearRing") {
        out.push_back(make_poly(drop_last(geom.coords()), true, layer, color));
    } else if (gt == "MultiPolygon" || gt == "MultiLineString" || gt == "GeometryCollection") {
        for (const geo::Geom& g : geom.geoms()) from_shapely_into(g, tmpl, out);
    }
}

}  // namespace

std::vector<EntityPtr> from_shapely(const geo::Geom& geom, const Entity* template_entity) {
    std::vector<EntityPtr> out;
    from_shapely_into(geom, template_entity, out);
    return out;
}

// ======================================================================
// Booleanas
// ======================================================================

namespace {

std::vector<geo::Geom> valid_geoms(const std::vector<EntityPtr>& entities) {
    std::vector<geo::Geom> out;
    for (const EntityPtr& e : entities) {
        geo::Geom g = to_shapely(*e);
        if (g) out.push_back(std::move(g));
    }
    return out;
}

}  // namespace

std::vector<EntityPtr> boolean_union(const std::vector<EntityPtr>& entities) {
    const std::vector<geo::Geom> geoms = valid_geoms(entities);
    if (geoms.size() < 2) return {};
    return from_shapely(geo::unary_union(geoms), entities.front().get());
}

std::vector<EntityPtr> boolean_subtract(const std::vector<EntityPtr>& base, const std::vector<EntityPtr>& cutters) {
    const std::vector<geo::Geom> bg = valid_geoms(base);
    const std::vector<geo::Geom> cg = valid_geoms(cutters);
    if (bg.empty() || cg.empty()) return {};
    const geo::Geom result = geo::unary_union(bg).difference(geo::unary_union(cg));
    return from_shapely(result, base.front().get());
}

std::vector<EntityPtr> boolean_intersect(const std::vector<EntityPtr>& entities) {
    std::vector<geo::Geom> geoms = valid_geoms(entities);
    if (geoms.size() < 2) return {};
    geo::Geom acc = geoms.front();
    for (size_t i = 1; i < geoms.size(); ++i) {
        acc = acc.intersection(geoms[i]);
        if (!acc || acc.is_empty()) return {};
    }
    return from_shapely(acc, entities.front().get());
}

// ======================================================================
// Offset
// ======================================================================

std::vector<EntityPtr> offset_entity(const Entity& e, double distance, bool outward, const std::string& join) {
    const geo::Geom g = to_shapely(e);
    if (!g) return {};

    const double d = outward ? std::fabs(distance) : -std::fabs(distance);
    geo::Join js = geo::Join::Round;
    if (join == "mitre") js = geo::Join::Mitre;
    else if (join == "bevel") js = geo::Join::Bevel;

    geo::Geom res;
    if (g.is_polygon()) {
        res = g.buffer(d, 16, js);
    } else if (g.is_line()) {
        res = g.offset_curve(d, 16, js);
    }
    // Qualquer outro tipo (multipoligono de texto) nao tem offset_curve: o Python cai no except.
    if (!res) return {};
    return from_shapely(res, &e);
}

// ======================================================================
// Filete (arredondamento de canto)
// ======================================================================

Points fillet_corner(const Points& pts, int idx, double radius, int seg, bool closed) {
    const int n = static_cast<int>(pts.size());
    if (n < 3 || radius <= 0) return pts;
    if (!closed && (idx == 0 || idx == n - 1)) return pts;

    const Pt p0 = pts[static_cast<size_t>(pymod(idx - 1, n))];
    const Pt p1 = pts[static_cast<size_t>(idx)];
    const Pt p2 = pts[static_cast<size_t>(pymod(idx + 1, n))];

    const Pt v1{p0.x - p1.x, p0.y - p1.y};
    const Pt v2{p2.x - p1.x, p2.y - p1.y};
    const double l1 = hypot(v1.x, v1.y);
    const double l2 = hypot(v2.x, v2.y);
    if (l1 < 1e-9 || l2 < 1e-9) return pts;

    const Pt u1{v1.x / l1, v1.y / l1};
    const Pt u2{v2.x / l2, v2.y / l2};

    const double dot = std::max(-1.0, std::min(1.0, u1.x * u2.x + u1.y * u2.y));
    const double angle = std::acos(dot);
    if (angle < 1e-6 || std::fabs(angle - kPi) < 1e-6) return pts;

    double tan_len = radius / std::tan(angle / 2);
    tan_len = std::min({tan_len, l1 * 0.5, l2 * 0.5});
    const double r_eff = tan_len * std::tan(angle / 2);

    const Pt t1{p1.x + u1.x * tan_len, p1.y + u1.y * tan_len};
    const Pt t2{p1.x + u2.x * tan_len, p1.y + u2.y * tan_len};

    Pt bis{u1.x + u2.x, u1.y + u2.y};
    const double bl = hypot(bis.x, bis.y);
    if (bl < 1e-9) return pts;
    bis = {bis.x / bl, bis.y / bl};
    const double cdist = r_eff / std::sin(angle / 2);
    const double cx = p1.x + bis.x * cdist, cy = p1.y + bis.y * cdist;

    const double a1 = std::atan2(t1.y - cy, t1.x - cx);
    const double a2 = std::atan2(t2.y - cy, t2.x - cx);
    double da = a2 - a1;
    while (da > kPi) da -= 2 * kPi;
    while (da < -kPi) da += 2 * kPi;

    Points out(pts.begin(), pts.begin() + idx);
    for (int i = 0; i <= seg; ++i) {
        out.push_back({cx + r_eff * std::cos(a1 + da * i / seg), cy + r_eff * std::sin(a1 + da * i / seg)});
    }
    out.insert(out.end(), pts.begin() + idx + 1, pts.end());
    return out;
}

// ======================================================================
// Unir vetores abertos
// ======================================================================

std::vector<EntityPtr> join_open_vectors(const std::vector<EntityPtr>& entities, double tol) {
    std::vector<Points> chains;
    for (const EntityPtr& e : entities) {
        if (e->closed()) continue;
        Points pts = e->to_points(kSeg);
        if (pts.size() >= 2) chains.push_back(std::move(pts));
    }
    if (chains.size() < 2) return {};

    auto concat = [](const Points& first, const Points& tail) {
        Points out = first;
        out.insert(out.end(), tail.begin(), tail.end());
        return out;
    };

    bool merged = true;
    bool did_merge = false;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < chains.size(); ++i) {
            if (merged) break;
            for (size_t j = i + 1; j < chains.size(); ++j) {
                const Points a = chains[i];
                const Points b = chains[j];
                Points b_rev(b.rbegin(), b.rend());
                // (ponta de a, ponta de b, resultado), na ordem do Python
                struct Option {
                    Pt pa;
                    Pt pb;
                    Points built;
                };
                const Points b_tail(b.begin() + 1, b.end());
                const Points b_rev_tail(b_rev.begin() + 1, b_rev.end());
                const Points a_tail(a.begin() + 1, a.end());
                const Option options[] = {
                    {a.back(), b.front(), concat(a, b_tail)},
                    {a.back(), b.back(), concat(a, b_rev_tail)},
                    {a.front(), b.back(), concat(b, a_tail)},
                    {a.front(), b.front(), concat(b_rev, a_tail)},
                };
                for (const Option& o : options) {
                    if (hypot(o.pa.x - o.pb.x, o.pa.y - o.pb.y) <= tol) {
                        chains[i] = o.built;
                        chains.erase(chains.begin() + static_cast<long>(j));
                        merged = true;
                        did_merge = true;
                        break;
                    }
                }
                if (merged) break;
            }
        }
    }

    if (!did_merge) return {};
    const Entity& tmpl = *entities.front();
    std::vector<EntityPtr> out;
    for (Points& ch : chains) {
        const bool closed = hypot(ch.front().x - ch.back().x, ch.front().y - ch.back().y) <= tol;
        if (closed && ch.size() > 2) ch.pop_back();
        out.push_back(make_poly(std::move(ch), closed, tmpl.layer, tmpl.color));
    }
    return out;
}

// ======================================================================
// Estender / Aparar
// ======================================================================

bool extend_line_to(Line& line, const std::vector<EntityPtr>& targets, const std::optional<std::string>& endpoint) {
    struct Best {
        double d;
        Pt ip;
        bool end;
    };
    std::optional<Best> best;
    const Pt p1{line.x1, line.y1};
    const Pt p2{line.x2, line.y2};
    const bool want_end = !endpoint || *endpoint == "end";
    const bool want_start = !endpoint || *endpoint == "start";

    for (const EntityPtr& tgt : targets) {
        const Points pts = tgt->to_points(kSeg);
        const size_t n = pts.size();
        const size_t count = tgt->closed() ? n : (n > 0 ? n - 1 : 0);
        for (size_t i = 0; i < count; ++i) {
            const Pt& a = pts[i];
            const Pt& b = pts[(i + 1) % n];
            const auto r = seg_intersect(p1, p2, a, b);
            if (!r) continue;
            if (!(0.0 <= r->u && r->u <= 1.0)) continue;
            if (r->t > 1.0 && want_end) {
                const double d = r->t - 1.0;
                if (!best || d < best->d) best = Best{d, r->point, true};
            } else if (r->t < 0.0 && want_start) {
                const double d = -r->t;
                if (!best || d < best->d) best = Best{d, r->point, false};
            }
        }
    }

    if (!best) return false;
    if (best->end) {
        line.x2 = best->ip.x;
        line.y2 = best->ip.y;
    } else {
        line.x1 = best->ip.x;
        line.y1 = best->ip.y;
    }
    return true;
}

namespace {

// Parametro (indice do segmento + fracao) do ponto da polilinha mais proximo de (x, y).
double param_at(const Points& pts, double x, double y, bool closed) {
    const size_t n = pts.size();
    double best_t = 0.0;
    double best_d = std::numeric_limits<double>::infinity();
    const size_t count = closed ? n : n - 1;
    for (size_t i = 0; i < count; ++i) {
        const double ax = pts[i].x, ay = pts[i].y;
        const double bx = pts[(i + 1) % n].x, by = pts[(i + 1) % n].y;
        const double dx = bx - ax, dy = by - ay;
        const double ll = dx * dx + dy * dy;
        if (ll < 1e-12) continue;
        const double t = std::max(0.0, std::min(1.0, ((x - ax) * dx + (y - ay) * dy) / ll));
        const double d = hypot(x - (ax + t * dx), y - (ay + t * dy));
        if (d < best_d) {
            best_d = d;
            best_t = static_cast<double>(i) + t;
        }
    }
    return best_t;
}

Pt lerp_poly(const Points& pts, double t) {
    const long n = static_cast<long>(pts.size());
    const long i = static_cast<long>(std::floor(t));
    const double f = t - static_cast<double>(i);
    const Pt a = pts[static_cast<size_t>(pymod(i, n))];
    const Pt b = pts[static_cast<size_t>(pymod(i + 1, n))];
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
}

Points slice_poly(const Points& pts, double t0, double t1) {
    const long n = static_cast<long>(pts.size());
    Points out;
    if (t1 <= t0) return out;
    out.push_back(lerp_poly(pts, t0));
    long k = static_cast<long>(std::floor(t0)) + 1;
    while (static_cast<double>(k) < t1) {
        out.push_back(pts[static_cast<size_t>(pymod(k, n))]);
        ++k;
    }
    out.push_back(lerp_poly(pts, t1));
    return out;
}

}  // namespace

std::vector<EntityPtr> trim_at_point(const Entity& e, double x, double y, const std::vector<EntityPtr>& others) {
    const Points pts = e.to_points(kSeg);
    if (pts.size() < 2) return {};

    const size_t n = pts.size();
    const bool closed = e.closed();
    const size_t seg_count = closed ? n : n - 1;

    std::vector<double> cuts;
    for (size_t i = 0; i < seg_count; ++i) {
        const Pt a = pts[i];
        const Pt b = pts[(i + 1) % n];
        for (const EntityPtr& o : others) {
            if (o.get() == &e) continue;
            const Points opts = o->to_points(kSeg);
            const size_t on = opts.size();
            const size_t ocount = o->closed() ? on : (on > 0 ? on - 1 : 0);
            for (size_t j = 0; j < ocount; ++j) {
                const auto r = seg_intersect(a, b, opts[j], opts[(j + 1) % on]);
                if (!r) continue;
                if (0.0 <= r->t && r->t <= 1.0 && 0.0 <= r->u && r->u <= 1.0) cuts.push_back(static_cast<double>(i) + r->t);
            }
        }
    }
    if (cuts.empty()) return {};

    const double click_t = param_at(pts, x, y, closed);
    std::sort(cuts.begin(), cuts.end());
    std::vector<double> unique_cuts;
    for (double cut : cuts) {
        if (unique_cuts.empty() || std::fabs(cut - unique_cuts.back()) > 1e-9) unique_cuts.push_back(cut);
    }
    cuts = unique_cuts;

    std::optional<double> lo, hi;
    for (double c : cuts) {
        if (c < click_t && (!lo || c > *lo)) lo = c;
        if (c > click_t && (!hi || c < *hi)) hi = c;
    }

    const double total = closed ? static_cast<double>(n) : static_cast<double>(n - 1);
    if (!lo && !hi) return {};
    std::vector<Points> parts;
    if (closed) {
        if (!lo) lo = cuts.back() - total;
        if (!hi) hi = cuts.front() + total;
        Points keep = slice_poly(pts, *hi, *lo + total);
        if (keep.size() >= 2) parts.push_back(std::move(keep));
    } else {
        if (lo) {
            Points s = slice_poly(pts, 0.0, *lo);
            if (s.size() >= 2) parts.push_back(std::move(s));
        }
        if (hi) {
            Points s = slice_poly(pts, *hi, total);
            if (s.size() >= 2) parts.push_back(std::move(s));
        }
    }

    std::vector<EntityPtr> out;
    for (Points& p : parts) out.push_back(make_poly(std::move(p), false, e.layer, e.color));
    return out;
}

// ======================================================================
// Contorno / Casco
// ======================================================================

std::vector<EntityPtr> vector_boundary(const std::vector<EntityPtr>& entities, double offset, bool rubber_band) {
    const std::vector<geo::Geom> geoms = valid_geoms(entities);
    if (geoms.empty()) return {};
    geo::Geom hull = geo::unary_union(geoms);
    if (rubber_band) hull = hull.convex_hull();
    if (offset != 0.0) hull = hull.buffer(offset, 16);
    return from_shapely(hull, entities.front().get());
}

// ======================================================================
// Suavizar curvas
// ======================================================================

namespace {

Pt catmull(const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3, double t, double a) {
    const double t2 = t * t, t3 = t * t * t;
    auto c = [&](double q0, double q1, double q2, double q3) {
        return ((2 * q1) + (-a * q0 + a * q2) * t +
                (2 * a * q0 + (a - 6) * q1 + (3 - 2 * a) * q2 - a * q3) * t2 +
                (-a * q0 + (2 - a) * q1 + (a - 2) * q2 + a * q3) * t3) *
               0.5;
    };
    return {c(p0.x, p1.x, p2.x, p3.x), c(p0.y, p1.y, p2.y, p3.y)};
}

}  // namespace

Points fit_curve(const Points& pts, bool closed, double smooth, int subdiv) {
    const long n = static_cast<long>(pts.size());
    if (n < 3) return pts;

    Points out;
    const long count = closed ? n : n - 1;
    for (long i = 0; i < count; ++i) {
        const Pt p0 = closed ? pts[static_cast<size_t>(pymod(i - 1, n))] : pts[static_cast<size_t>(std::max(i - 1, 0L))];
        const Pt p1 = pts[static_cast<size_t>(i)];
        const Pt p2 = pts[static_cast<size_t>(pymod(i + 1, n))];
        const Pt p3 = closed ? pts[static_cast<size_t>(pymod(i + 2, n))] : pts[static_cast<size_t>(std::min(i + 2, n - 1))];
        for (int s = 0; s < subdiv; ++s) {
            const double t = static_cast<double>(s) / subdiv;
            out.push_back(catmull(p0, p1, p2, p3, t, smooth));
        }
    }
    if (!closed) out.push_back(pts.back());
    return out;
}

}  // namespace cnc::cad
