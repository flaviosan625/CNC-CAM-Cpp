// Porte de src/cam/model.py.
#include "cam/model.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>

#include "clipper.hpp"

namespace cnc::cam {

const char* toolpath_label(ToolpathType t) {
    switch (t) {
        case ToolpathType::Profile: return "Perfil";
        case ToolpathType::Pocket: return "Bolso";
        case ToolpathType::Drill: return "Furação";
        case ToolpathType::VCarve: return "Entalhe V";
        case ToolpathType::Fluting: return "Canais (Fluting)";
        case ToolpathType::Texture: return "Textura";
        case ToolpathType::Keyhole: return "Rasgo de fixação";
        case ToolpathType::Inlay: return "Inlay";
        case ToolpathType::Dragknife: return "Faca de arraste";
    }
    return "";
}

const char* order_label(MachineOrder o) {
    switch (o) {
        case MachineOrder::Selection: return "Ordem da seleção";
        case MachineOrder::LeftRight: return "Esquerda → direita";
        case MachineOrder::BottomTop: return "Baixo → cima";
        case MachineOrder::Grid: return "Grelha";
        case MachineOrder::Shortest: return "Percurso mais curto";
    }
    return "";
}

// ======================================================================
// Percurso calculado
// ======================================================================

std::optional<BBox> Toolpath::bounds() const {
    bool any = false;
    BBox b;
    for (const Move& m : moves) {
        if (m.kind == MoveKind::Dwell) continue;
        if (!any) {
            b = {m.x, m.y, m.x, m.y};
            any = true;
        } else {
            b.x0 = std::min(b.x0, m.x);
            b.y0 = std::min(b.y0, m.y);
            b.x1 = std::max(b.x1, m.x);
            b.y1 = std::max(b.y1, m.y);
        }
    }
    if (!any) return std::nullopt;
    return b;
}

std::pair<double, double> Toolpath::depth_range() const {
    bool any = false;
    double lo = 0.0, hi = 0.0;
    for (const Move& m : moves) {
        if (m.kind == MoveKind::Dwell) continue;
        if (!any) {
            lo = hi = m.z;
            any = true;
        } else {
            lo = std::min(lo, m.z);
            hi = std::max(hi, m.z);
        }
    }
    return any ? std::make_pair(lo, hi) : std::make_pair(0.0, 0.0);
}

std::pair<double, double> Toolpath::lengths() const {
    double cut = 0.0, rapid = 0.0;
    bool have_prev = false;
    double px = 0, py = 0, pz = 0;
    for (const Move& m : moves) {
        if (m.kind == MoveKind::Dwell) continue;
        if (have_prev) {
            const double d = dist3(px, py, pz, m.x, m.y, m.z);
            if (m.kind == MoveKind::Rapid) {
                rapid += d;
            } else {
                cut += d;
            }
        }
        px = m.x;
        py = m.y;
        pz = m.z;
        have_prev = true;
    }
    return {cut, rapid};
}

double Toolpath::move_seconds(const Move& start, const Move& end, double rapid_rate) const {
    if (end.kind == MoveKind::Dwell) return std::max(0.0, end.seconds);
    const double distance = dist3(start.x, start.y, start.z, end.x, end.y, end.z);
    double rate;
    if (end.kind == MoveKind::Rapid) {
        rate = rapid_rate;
    } else if (end.kind == MoveKind::Plunge) {
        rate = plunge_rate;
    } else {
        rate = feed_rate;
    }
    return rate > 0 ? 60.0 * distance / rate : 0.0;
}

double Toolpath::estimated_seconds(double rapid_rate) const {
    double total = 0.0;
    const Move* previous = nullptr;
    for (const Move& m : moves) {
        if (previous) total += move_seconds(*previous, m, rapid_rate);
        previous = &m;
    }
    return total;
}

std::string fmt_time(double minutes) {
    if (minutes < 1) return std::format("{:.0f} s", minutes * 60);
    if (minutes < 60) return std::format("{:.0f} min", minutes);
    const int hours = static_cast<int>(floordiv_f(minutes, 60.0));
    const int mins = static_cast<int>(fmod_py(minutes, 60.0));
    return std::format("{} h {:02d} min", hours, mins);
}

std::string Toolpath::summary() const {
    const auto [cut, rapid] = lengths();
    const double mins = estimated_seconds() / 60.0;
    return std::format("{} mov. · corte {:.2f} m · rápido {:.2f} m · ≈ {}", moves.size(),
                       cut / 1000, rapid / 1000, fmt_time(mins));
}

// ======================================================================
// Passadas em Z
// ======================================================================

std::vector<double> compute_passes(double total_depth, double pass_depth, bool maintain_exact,
                                   double variation) {
    if (total_depth <= 1e-9) return {};
    if (pass_depth <= 1e-9) return {total_depth};

    std::vector<double> out;
    if (maintain_exact) {
        const int n = static_cast<int>(std::ceil(total_depth / pass_depth - 1e-9));
        for (int i = 0; i < n; ++i) out.push_back(std::min(total_depth, (i + 1) * pass_depth));
        return out;
    }

    const int n = std::max(
        1, static_cast<int>(std::ceil(total_depth / (pass_depth * (1.0 + variation)) - 1e-9)));
    const double step = total_depth / n;
    for (int i = 0; i < n; ++i) out.push_back(step * (i + 1));
    return out;
}

// ======================================================================
// Offset de contornos (Clipper)
// ======================================================================

namespace {

// _to_clipper: int(round(x * escala)). round() do Python e bancario.
ClipperLib::Path to_clipper(const Points& pts) {
    ClipperLib::Path path;
    path.reserve(pts.size());
    for (const Pt& p : pts) {
        path.emplace_back(static_cast<ClipperLib::cInt>(py_round_i64(p.x * kClipperScale)),
                          static_cast<ClipperLib::cInt>(py_round_i64(p.y * kClipperScale)));
    }
    return path;
}

Points from_clipper(const ClipperLib::Path& path) {
    Points out;
    out.reserve(path.size());
    for (const auto& p : path) {
        out.push_back({static_cast<double>(p.X) / kClipperScale,
                       static_cast<double>(p.Y) / kClipperScale});
    }
    return out;
}

Points without_closing_point(Points pts) {
    if (!pts.empty()) pts.pop_back();
    return pts;
}

}  // namespace

std::vector<Points> offset_polygon(const geo::Geom& polygon, double delta, double tolerance) {
    if (polygon.is_empty()) return {};

    const geo::Geom ring_source = geo::orient(polygon, 1.0);  // exterior CCW, furos CW
    if (!ring_source) return {};

    ClipperLib::ClipperOffset co(2.0, std::max(1.0, tolerance * kClipperScale));
    co.ArcTolerance = std::max(1.0, tolerance * kClipperScale);
    co.MiterLimit = 2.0;

    co.AddPath(to_clipper(without_closing_point(ring_source.exterior_coords())),
               ClipperLib::jtRound, ClipperLib::etClosedPolygon);
    for (const Points& interior : ring_source.interior_coords()) {
        co.AddPath(to_clipper(without_closing_point(interior)), ClipperLib::jtRound,
                   ClipperLib::etClosedPolygon);
    }

    ClipperLib::Paths solution;
    try {
        co.Execute(solution, delta * kClipperScale);
    } catch (const std::exception&) {
        return {};
    }
    std::vector<Points> out;
    for (const auto& p : solution) {
        if (p.size() >= 3) out.push_back(from_clipper(p));
    }
    return out;
}

std::vector<Points> offset_open_path(const geo::Geom& line, double delta, OpenSide side,
                                     double tolerance) {
    if (side == OpenSide::On || std::fabs(delta) < 1e-9) return {line.coords()};
    const double signed_d = side == OpenSide::Left ? delta : -delta;
    // line.parallel_offset(abs(signed), "left" if signed > 0 else "right", join_style=1,
    // mitre_limit=2.0): no shapely 2.x e offset_curve com o sinal trocado para "right".
    const double d = std::fabs(signed_d);
    const geo::Geom result =
        line.offset_curve(signed_d > 0 ? d : -d, 16, geo::Join::Round, 2.0);
    if (!result) return {line.coords()};
    if (result.is_empty()) return {};
    std::vector<Points> out;
    for (const geo::Geom& g : result.geoms()) {
        if (g.length() > tolerance) out.push_back(g.coords());
    }
    return out;
}

// ======================================================================
// Utilidades de polilinha
// ======================================================================

double signed_area(const Points& pts) {
    double total = 0.0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[(i + 1) % n];
        total += a.x * b.y - b.x * a.y;
    }
    return total / 2.0;
}

bool is_ccw(const Points& pts) { return signed_area(pts) > 0.0; }

Points orient_ring(const Points& pts, bool want_ccw) {
    if (is_ccw(pts) == want_ccw) return pts;
    return Points(pts.rbegin(), pts.rend());
}

bool travel_ccw(bool material_inside, CutDirection direction) {
    const bool climb = direction == CutDirection::Climb;
    return material_inside != climb;
}

Points close_ring(Points pts) {
    if (pts.empty()) return pts;
    if (dist(pts.front(), pts.back()) > 1e-9) pts.push_back(pts.front());
    return pts;
}

double path_length(const Points& pts) {
    PySum s;
    for (size_t i = 0; i + 1 < pts.size(); ++i) s.add(dist(pts[i], pts[i + 1]));
    return s.result();
}

Points rotate_to_start(const Points& ring, Pt start) {
    Points pts;
    if (ring.size() > 1 && dist(ring.front(), ring.back()) < 1e-9) {
        pts.assign(ring.begin(), ring.end() - 1);
    } else {
        pts = ring;
    }
    if (pts.size() < 2) return close_ring(pts);
    size_t idx = 0;
    double best = dist(pts[0], start);
    for (size_t i = 1; i < pts.size(); ++i) {
        const double d = dist(pts[i], start);
        if (d < best) {
            best = d;
            idx = i;
        }
    }
    Points rotated(pts.begin() + static_cast<long>(idx), pts.end());
    rotated.insert(rotated.end(), pts.begin(), pts.begin() + static_cast<long>(idx));
    return close_ring(std::move(rotated));
}

Points resample(const Points& pts, const std::vector<double>& distances) {
    if (distances.empty()) return pts;
    std::vector<double> wanted;
    for (double d : distances) {
        if (d > 1e-9) wanted.push_back(d);
    }
    std::sort(wanted.begin(), wanted.end());
    Points out{pts[0]};
    double acc = 0.0;
    size_t k = 0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[i + 1];
        const double seg = dist(a, b);
        if (seg < 1e-12) continue;
        while (k < wanted.size() && wanted[k] <= acc + seg + 1e-9) {
            const double t = (wanted[k] - acc) / seg;
            if (1e-9 < t && t < 1 - 1e-9) {
                out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
            }
            ++k;
        }
        out.push_back(b);
        acc += seg;
    }
    return out;
}

Pt point_at(const Points& pts, double distance) {
    double acc = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[i + 1];
        const double seg = dist(a, b);
        if (seg < 1e-12) continue;
        if (acc + seg >= distance) {
            const double t = (distance - acc) / seg;
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        }
        acc += seg;
    }
    return pts.back();
}

Points slice_path(const Points& pts, double start, double end) {
    Points out{point_at(pts, start)};
    double acc = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[i + 1];
        const double seg = dist(a, b);
        if (seg < 1e-12) continue;
        if (acc + seg > start + 1e-9 && acc < end - 1e-9) {
            if (start - 1e-9 < acc + seg && acc + seg < end + 1e-9) out.push_back(b);
        }
        acc += seg;
    }
    const Pt tail = point_at(pts, end);
    if (dist(out.back(), tail) > 1e-9) out.push_back(tail);
    return out;
}

Pt tangent_at_start(const Points& pts) {
    for (size_t i = 1; i < pts.size(); ++i) {
        const double dx = pts[i].x - pts[0].x, dy = pts[i].y - pts[0].y;
        const double n = hypot(dx, dy);
        if (n > 1e-9) return {dx / n, dy / n};
    }
    return {1.0, 0.0};
}

std::vector<int> order_loops(const std::vector<Points>& loops, MachineOrder order,
                             double grid_band) {
    if (loops.empty()) return {};

    std::vector<Pt> centers;
    centers.reserve(loops.size());
    for (const Points& pts : loops) {
        double x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
        for (const Pt& p : pts) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        centers.push_back({(x0 + x1) / 2, (y0 + y1) / 2});
    }

    std::vector<int> idx(loops.size());
    std::iota(idx.begin(), idx.end(), 0);
    if (order == MachineOrder::Selection) return idx;
    // sorted() do Python e estavel; std::stable_sort tambem.
    if (order == MachineOrder::LeftRight) {
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (centers[a].x != centers[b].x) return centers[a].x < centers[b].x;
            return centers[a].y < centers[b].y;
        });
        return idx;
    }
    if (order == MachineOrder::BottomTop) {
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (centers[a].y != centers[b].y) return centers[a].y < centers[b].y;
            return centers[a].x < centers[b].x;
        });
        return idx;
    }
    if (order == MachineOrder::Grid) {
        const double band = std::max(grid_band, 1e-6);
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            const long long ra = py_round_i64(centers[a].y / band);
            const long long rb = py_round_i64(centers[b].y / band);
            if (ra != rb) return ra < rb;
            return centers[a].x < centers[b].x;
        });
        return idx;
    }

    // Percurso mais curto: vizinho mais proximo a partir da origem. Em empate vence o
    // menor indice, como o min() do Python sobre um set de inteiros pequenos.
    std::vector<bool> used(loops.size(), false);
    std::vector<int> out;
    Pt cur{0.0, 0.0};
    for (size_t step = 0; step < loops.size(); ++step) {
        int best = -1;
        double best_d = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < loops.size(); ++i) {
            if (used[i]) continue;
            const double d = dist(cur, centers[i]);
            if (best < 0 || d < best_d) {
                best = static_cast<int>(i);
                best_d = d;
            }
        }
        out.push_back(best);
        used[static_cast<size_t>(best)] = true;
        cur = centers[static_cast<size_t>(best)];
    }
    return out;
}

double travel_length(const std::vector<Points>& loops, const std::vector<int>& sequence) {
    double total = 0.0;
    Pt atual{0.0, 0.0};
    for (int i : sequence) {
        const Points& pts = loops[static_cast<size_t>(i)];
        if (pts.empty()) continue;
        total += dist(atual, pts.front());
        atual = pts.back();
    }
    return total;
}

std::pair<std::vector<int>, MachineOrder> best_order(const std::vector<Points>& loops,
                                                     const std::vector<MachineOrder>& orders,
                                                     double grid_band) {
    std::vector<MachineOrder> candidatas = orders;
    if (candidatas.empty()) candidatas = {MachineOrder::Shortest};
    std::vector<int> melhor_seq;
    MachineOrder melhor_ordem = candidatas[0];
    double melhor_custo = std::numeric_limits<double>::infinity();
    for (MachineOrder ordem : candidatas) {
        std::vector<int> seq = order_loops(loops, ordem, grid_band);
        const double custo = travel_length(loops, seq);
        if (custo < melhor_custo - 1e-9) {
            melhor_custo = custo;
            melhor_seq = std::move(seq);
            melhor_ordem = ordem;
        }
    }
    return {melhor_seq, melhor_ordem};
}

}  // namespace cnc::cam
