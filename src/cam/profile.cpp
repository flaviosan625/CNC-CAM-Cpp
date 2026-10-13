// Porte de src/cam/profile.py.
#include "cam/profile.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>

namespace cnc::cam {

// ======================================================================
// Emissor de movimentos
// ======================================================================

void Emitter::retract() {
    if (z_ < clearance_z_ - 1e-9) {
        z_ = clearance_z_;
        moves.push_back({MoveKind::Rapid, x_, y_, z_});
    }
}

void Emitter::rapid_to(double x, double y) {
    retract();
    x_ = x;
    y_ = y;
    moves.push_back({MoveKind::Rapid, x, y, z_});
}

void Emitter::plunge_to(double z) {
    const double gap_z = z + plunge_gap_;
    if (gap_z < z_ - 1e-9) {
        z_ = gap_z;
        moves.push_back({MoveKind::Rapid, x_, y_, gap_z});
    }
    if (z < z_ - 1e-9) {
        z_ = z;
        moves.push_back({MoveKind::Plunge, x_, y_, z});
    }
}

void Emitter::cut_to(double x, double y, double z, bool plunging) {
    if (std::fabs(x - x_) < 1e-9 && std::fabs(y - y_) < 1e-9 && std::fabs(z - z_) < 1e-9) return;
    const MoveKind kind = plunging ? MoveKind::Plunge : MoveKind::Cut;
    x_ = x;
    y_ = y;
    z_ = z;
    moves.push_back({kind, x, y, z});
}

// ======================================================================
// Pontes de fixacao (tabs)
// ======================================================================

std::vector<MachineOrder> machine_orders(const ProfileSettings& s) {
    std::vector<MachineOrder> saida;
    for (const std::string& valor : s.orders) {
        if (auto o = from_str<MachineOrder>(valor)) saida.push_back(*o);  // ordem de versao futura: ignora
    }
    if (saida.empty()) saida.push_back(s.order);
    return saida;
}

int tab_count_for(double total_length, const ProfileSettings& s) {
    int count;
    if (s.tab_spacing > 0) {
        count = static_cast<int>(std::floor(total_length / s.tab_spacing));
        count = std::max(s.tab_min_count, std::min(s.tab_max_count, count));
    } else {
        count = s.tab_count;
    }
    return std::max(1, count);
}

namespace {

double nearest_ring_distance(Pt point, const Points& ring) {
    // Projeta um ponto escolhido a mao sobre o anel do percurso.
    const Points points = close_ring(ring);
    double best_distance = std::numeric_limits<double>::infinity();
    double best_position = 0.0;
    double accumulated = 0.0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const Pt& start = points[i];
        const Pt& end = points[i + 1];
        const double dx = end.x - start.x, dy = end.y - start.y;
        const double length_sq = dx * dx + dy * dy;
        const double length = std::sqrt(length_sq);
        if (length_sq <= 1e-18) continue;
        const double t = std::max(
            0.0, std::min(1.0, ((point.x - start.x) * dx + (point.y - start.y) * dy) / length_sq));
        const Pt projected{start.x + t * dx, start.y + t * dy};
        const double distance = dist(point, projected);
        if (distance < best_distance) {
            best_distance = distance;
            best_position = accumulated + t * length;
        }
        accumulated += length;
    }
    return best_position;
}

Pt point_at_ring_distance(const Points& ring, double distance) {
    const Points points = close_ring(ring);
    const double total = path_length(points);
    const double target = total > 1e-12 ? fmod_py(distance, total) : 0.0;
    double accumulated = 0.0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const Pt& start = points[i];
        const Pt& end = points[i + 1];
        const double length = dist(start, end);
        if (accumulated + length >= target - 1e-9) {
            const double ratio = length <= 1e-12 ? 0.0 : (target - accumulated) / length;
            return {start.x + (end.x - start.x) * ratio, start.y + (end.y - start.y) * ratio};
        }
        accumulated += length;
    }
    return points.back();
}

// Uma ponte centrada em `center` vira um ou dois trechos (se cruza o ponto inicial).
void add_span(std::vector<Span>& spans, double center, double half, double total_length) {
    const double start = center - half, end = center + half;
    if (start < 0) {
        spans.push_back({0.0, end});
        spans.push_back({total_length + start, total_length});
    } else if (end > total_length) {
        spans.push_back({start, total_length});
        spans.push_back({0.0, end - total_length});
    } else {
        spans.push_back({start, end});
    }
}

}  // namespace

std::vector<Span> tab_spans(double total_length, const ProfileSettings& s, const Points* ring,
                            const std::string* entity_id) {
    if (!s.tabs || s.tab_length <= 0 || total_length <= 0) return {};
    const double half = s.tab_length / 2.0;
    std::vector<Span> spans;

    std::vector<Pt> manual_for_ring;
    if (entity_id) {
        for (const ManualTab& t : s.manual_tabs) {
            if (t.entity_id == *entity_id) manual_for_ring.push_back({t.x, t.y});
        }
    }
    if (!s.tab_manual_only) {
        int count = tab_count_for(total_length, s);
        if (count * s.tab_length >= total_length) {
            count = std::max(1, static_cast<int>(floordiv_f(total_length, s.tab_length * 2)));
        }
        for (int i = 0; i < count; ++i) {
            const double center = s.tab_first_at_start ? total_length * i / count
                                                       : total_length * (i + 0.5) / count;
            add_span(spans, center, half, total_length);
        }
    }

    if (ring && !ring->empty() && entity_id && !entity_id->empty()) {
        for (const Pt& p : manual_for_ring) {
            add_span(spans, nearest_ring_distance(p, *ring), half, total_length);
        }
    }

    std::sort(spans.begin(), spans.end());
    std::vector<Span> merged;
    for (const Span& sp : spans) {
        if (!merged.empty() && sp.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, sp.second);
        } else {
            merged.push_back(sp);
        }
    }
    return merged;
}

namespace {

// Z imposto pela ponte na posicao `pos`, ou nada fora das pontes.
std::optional<double> tab_z(double pos, const std::vector<Span>& spans, double base_z,
                            double tab_top_z, bool tabs_3d) {
    for (const auto& [s0, s1] : spans) {
        if (s0 - 1e-9 <= pos && pos <= s1 + 1e-9) {
            if (!tabs_3d) return tab_top_z;
            const double mid = (s0 + s1) / 2.0;
            const double half = std::max(mid - s0, 1e-9);
            const double t = 1.0 - std::fabs(pos - mid) / half;  // 0 nas bordas, 1 no meio
            return base_z + (tab_top_z - base_z) * std::max(0.0, std::min(1.0, t));
        }
    }
    return std::nullopt;
}

// ======================================================================
// Entradas e saidas (leads)
// ======================================================================

// Pontos da entrada e da saida, no lado livre de material.
std::pair<Points, Points> lead_points(const Points& ring, const ProfileSettings& s,
                                      bool material_left) {
    if (s.lead == LeadType::None) return {{}, {}};

    const Pt tangent = tangent_at_start(ring);
    const double tx = tangent.x, ty = tangent.y;
    // Normal apontando para o lado SEM material.
    const double nx = material_left ? ty : -ty;
    const double ny = material_left ? -tx : tx;
    const double x0 = ring[0].x, y0 = ring[0].y;

    Points entry;
    if (s.lead == LeadType::Linear) {
        const double ang = radians(std::max(0.1, std::min(90.0, s.lead_angle)));
        const double dx = -(tx * std::cos(ang) + nx * std::sin(ang));
        const double dy = -(ty * std::cos(ang) + ny * std::sin(ang));
        double n = hypot(dx, dy);
        if (n == 0.0) n = 1.0;  // "or 1.0" do Python
        entry = {{x0 + dx / n * s.lead_length, y0 + dy / n * s.lead_length}, {x0, y0}};
    } else {
        const double r = std::max(0.1, s.lead_radius);
        const double cx = x0 + nx * r, cy = y0 + ny * r;
        const double sweep = radians(std::max(0.1, std::min(90.0, s.lead_angle)));
        const double a0 = std::atan2(y0 - cy, x0 - cx);
        const double sign = material_left ? -1.0 : 1.0;
        const int steps = std::max(4, static_cast<int>(sweep / 0.15));
        for (int i = 0; i <= steps; ++i) {
            entry.push_back({cx + r * std::cos(a0 + sign * sweep * (1 - static_cast<double>(i) / steps)),
                             cy + r * std::sin(a0 + sign * sweep * (1 - static_cast<double>(i) / steps))});
        }
    }

    Points exit_pts;
    if (s.lead_out) {
        for (size_t k = entry.size() - 1; k-- > 0;) {  // reversed(entry[:-1])
            exit_pts.push_back({2 * ring.back().x - entry[k].x, 2 * ring.back().y - entry[k].y});
        }
    }
    return {entry, exit_pts};
}

// ======================================================================
// Uma passada sobre um laco
// ======================================================================

// Emite uma passada completa sobre um laco, com rampa, pontes e leads.
void emit_loop(Emitter& em, const Points& ring, bool closed, double z_prev, double z_target,
               std::optional<double> tab_top_z, const ProfileSettings& s, bool material_left,
               const std::string* entity_id) {
    const Points base = closed ? close_ring(ring) : ring;
    const double loop_len = path_length(base);
    if (loop_len < 1e-6) return;

    std::vector<Span> spans;
    if (closed && tab_top_z) spans = tab_spans(loop_len, s, &ring, entity_id);

    // Rampa: distancia percorrida descendo ate a profundidade da passada.
    double ramp_len = 0.0;
    const double drop = z_prev - z_target;
    if (s.ramp != RampType::None && drop > 1e-9) {
        if (s.ramp_angle > 0.1) {
            ramp_len = drop / std::tan(radians(std::min(89.0, s.ramp_angle)));
        } else {
            ramp_len = s.ramp_distance;
        }
        ramp_len = std::min(std::max(ramp_len, 0.0), closed ? loop_len : loop_len * 0.5);
    }

    if (s.ramp == RampType::Spiral && closed) ramp_len = loop_len;  // desce ao longo de toda a volta

    // Caminho a percorrer: o laco mais o trecho inicial recortado no fundo.
    Points path = base;
    if (closed && ramp_len > 1e-9) {
        const Points tail = slice_path(base, 0.0, ramp_len);
        path.insert(path.end(), tail.begin() + 1, tail.end());
    }

    // Pontos obrigatorios: fim da rampa e bordas/meio de cada ponte.
    std::vector<double> marks;
    if (ramp_len > 1e-9) marks.push_back(ramp_len);
    for (const auto& [s0, s1] : spans) {
        marks.push_back(s0);
        marks.push_back((s0 + s1) / 2.0);
        marks.push_back(s1);
        if (ramp_len > 1e-9 && s0 < ramp_len) {
            marks.push_back(loop_len + s0);
            marks.push_back(loop_len + (s0 + s1) / 2.0);
            marks.push_back(std::min(loop_len + s1, loop_len + ramp_len));
        }
    }
    const double current_len = path_length(path);
    std::vector<double> wanted;
    for (double m : marks) {
        if (0 < m && m < current_len) wanted.push_back(m);
    }
    path = resample(path, wanted);

    // Entrada
    const auto [entry, exit_pts] = lead_points(base, s, material_left);
    const Pt start = !entry.empty() ? entry[0] : path[0];
    em.rapid_to(start.x, start.y);
    em.plunge_to(ramp_len > 1e-9 ? z_prev : z_target);
    for (size_t i = 1; i < entry.size(); ++i) em.cut_to(entry[i].x, entry[i].y, em.z());

    // Percurso com perfil de Z
    double acc = 0.0;
    Pt prev = path[0];
    em.cut_to(prev.x, prev.y, em.z());
    for (size_t i = 1; i < path.size(); ++i) {
        const Pt pt = path[i];
        acc += dist(prev, pt);
        prev = pt;
        const double pos = acc <= loop_len ? acc : acc - loop_len;

        double z = z_target;
        bool ramping = false;
        if (ramp_len > 1e-9 && acc < ramp_len - 1e-9) {
            z = z_prev + (z_target - z_prev) * (acc / ramp_len);
            ramping = true;
        }

        if (!spans.empty()) {
            const std::optional<double> tz = tab_z(pos, spans, z, *tab_top_z, s.tabs_3d);
            if (tz && *tz > z) {
                if (!s.tabs_3d) {
                    em.cut_to(pt.x, pt.y, em.z());
                    em.cut_to(pt.x, pt.y, *tz);
                    continue;
                }
                z = *tz;
            }
        }

        em.cut_to(pt.x, pt.y, z, ramping);
        if (!spans.empty() && !s.tabs_3d && em.z() > z_target + 1e-9) {
            bool inside = false;
            for (const auto& [s0, s1] : spans) {
                if (s0 - 1e-9 <= pos && pos <= s1 + 1e-9) {
                    inside = true;
                    break;
                }
            }
            if (!inside) em.cut_to(pt.x, pt.y, z_target, true);
        }
    }

    // Corte alem do ponto inicial, para limpar a marca da entrada
    if (closed && s.overcut > 1e-9) {
        const Points over = slice_path(base, 0.0, std::min(s.overcut, loop_len));
        for (size_t i = 1; i < over.size(); ++i) em.cut_to(over[i].x, over[i].y, z_target);
    }

    for (const Pt& p : exit_pts) em.cut_to(p.x, p.y, z_target);

    em.retract();
}

// ======================================================================
// Geracao
// ======================================================================

struct ProfileJob {
    Points ring;
    bool closed = true;
    int level = 0;
    bool material_inside = false;
    bool interior = false;
    std::optional<Points> final_ring;  // geometria da passada final separada
    // Primeiro no do vetor desenhado. Com "usar pontos iniciais", e aqui que a fresa
    // entra - e onde a marca de entrada vai ficar.
    std::optional<Pt> anchor;
    std::optional<std::string> entity_id;
};

// Casa um laco de desbaste com o laco final correspondente.
Points closest_ring(const std::vector<Points>& candidates, const Points& ring) {
    if (candidates.empty()) return ring;
    auto centroid_of = [](const Points& c) {
        const double ax = py_sum(c, [](const Pt& p) { return p.x; }) / static_cast<double>(c.size());
        const double ay = py_sum(c, [](const Pt& p) { return p.y; }) / static_cast<double>(c.size());
        return Pt{ax, ay};
    };
    const Pt center = centroid_of(ring);
    const Points* best = &candidates[0];
    double best_d = dist(center, centroid_of(candidates[0]));
    for (size_t i = 1; i < candidates.size(); ++i) {
        const double d = dist(center, centroid_of(candidates[i]));
        if (d < best_d) {
            best_d = d;
            best = &candidates[i];
        }
    }
    return *best;
}

Points drop_last(Points pts) {
    if (!pts.empty()) pts.pop_back();
    return pts;
}

std::pair<std::vector<ProfileJob>, std::vector<std::string>> build_jobs(
    const std::vector<cad::EntityPtr>& entities, const Tool& tool, const ProfileSettings& s) {
    std::vector<std::string> notes;
    RegionsResult found = regions_from(entities, s.tolerance);
    std::vector<ProfileJob> jobs;

    const double radius = tool.radius();
    const double rough_extra = s.separate_last_pass ? s.last_pass_allowance : 0.0;

    for (const Region& region : found.regions) {
        const geo::Geom poly = region.to_polygon();
        double base_delta;
        if (s.side == ProfileSide::On) {
            base_delta = 0.0;
        } else if (s.side == ProfileSide::Outside) {
            base_delta = radius + s.allowance;
        } else {
            base_delta = -(radius + s.allowance);
        }

        std::vector<Points> rings_final;
        if (s.side != ProfileSide::On) {
            rings_final = offset_polygon(poly, base_delta, s.tolerance);
        } else {
            rings_final.push_back(drop_last(poly.exterior_coords()));
            for (const Points& r : poly.interior_coords()) rings_final.push_back(drop_last(r));
        }
        if (rings_final.empty()) {
            const BBox b = poly.bounds();
            notes.push_back(std::format(
                "Fresa ⌀{:g} mm não cabe no vetor de {:.1f} × {:.1f} mm — nada foi cortado ali.",
                tool.diameter, b.x1 - b.x0, b.y1 - b.y0));
            continue;
        }

        const double rough_delta = base_delta + (base_delta >= 0 ? rough_extra : -rough_extra);
        const std::vector<Points> rings_rough =
            rough_extra > 1e-9 ? offset_polygon(poly, rough_delta, s.tolerance) : rings_final;

        const Pt ancora_fora = poly.exterior_coords()[0];
        std::vector<Pt> ancoras_dentro;
        for (const Points& r : poly.interior_coords()) ancoras_dentro.push_back(r[0]);

        for (const Points& ring : rings_rough) {
            const bool exterior = is_ccw(ring);
            Pt ancora;
            if (exterior || ancoras_dentro.empty()) {
                ancora = ancora_fora;
            } else {
                ancora = ancoras_dentro[0];
                double best = dist(ancora, ring[0]);
                for (size_t i = 1; i < ancoras_dentro.size(); ++i) {
                    const double d = dist(ancoras_dentro[i], ring[0]);
                    if (d < best) {
                        best = d;
                        ancora = ancoras_dentro[i];
                    }
                }
            }
            const bool material_inside = s.side == ProfileSide::Inside ? !exterior : exterior;
            const int level = region.outer->level + (exterior ? 0 : 1);
            const Points match = rough_extra > 1e-9 ? closest_ring(rings_final, ring) : ring;

            std::string entity_id;
            if (exterior || region.holes.empty()) {
                entity_id = region.outer->entity->id;
            } else {
                const double cx = py_sum(ring, [](const Pt& p) { return p.x; }) / static_cast<double>(ring.size());
                const double cy = py_sum(ring, [](const Pt& p) { return p.y; }) / static_cast<double>(ring.size());
                const ContourPtr* best = &region.holes[0];
                Pt c0 = (*best)->polygon.centroid();
                double best_d = dist({cx, cy}, c0);
                for (size_t i = 1; i < region.holes.size(); ++i) {
                    const Pt ci = region.holes[i]->polygon.centroid();
                    const double d = dist({cx, cy}, ci);
                    if (d < best_d) {
                        best_d = d;
                        best = &region.holes[i];
                    }
                }
                entity_id = (*best)->entity->id;
            }

            ProfileJob job;
            job.ring = ring;
            job.closed = true;
            job.level = level;
            job.material_inside = material_inside;
            job.interior = !exterior || s.side == ProfileSide::Inside;
            job.final_ring = match;
            job.anchor = ancora;
            job.entity_id = entity_id;
            jobs.push_back(std::move(job));
        }
    }

    for (const OpenPath& op : found.open_paths) {
        const double delta = s.open_side == OpenSide::On ? 0.0 : radius + s.allowance;
        for (const Points& pts : offset_open_path(op.line, delta, s.open_side, s.tolerance)) {
            if (pts.size() >= 2) {
                ProfileJob job;
                job.ring = pts;
                job.closed = false;
                job.level = 0;
                job.material_inside = s.open_side == OpenSide::Right;
                job.interior = false;
                job.final_ring = pts;
                job.anchor = pts[0];
                job.entity_id = op.entity->id;
                jobs.push_back(std::move(job));
            }
        }
    }
    return {std::move(jobs), std::move(notes)};
}

void machine_job(Emitter& em, const ProfileJob& job, const std::vector<double>& depths,
                 double surface, double tab_top, const ProfileSettings& s) {
    const bool want_ccw = travel_ccw(job.material_inside, s.direction);
    Points ring = job.closed ? orient_ring(job.ring, want_ccw) : job.ring;
    // Normalmente o contorno e girado para comecar perto de onde a fresa esta - anda menos
    // em vazio. Com "usar pontos iniciais", o no que o desenho traz manda: e onde a marca de
    // entrada vai ficar, e as vezes ela nao pode cair numa face a vista.
    if (job.closed) {
        const Pt alvo = (s.use_start_points && job.anchor) ? *job.anchor : em.position();
        ring = rotate_to_start(ring, alvo);
    }
    const bool material_left = job.closed ? (is_ccw(ring) == job.material_inside) : false;

    double z_prev = surface - s.start_depth;
    const size_t last_index = depths.size() - 1;
    for (size_t i = 0; i < depths.size(); ++i) {
        const double z = surface - s.start_depth - depths[i];
        const bool final_pass = i == last_index;
        const bool use_tabs = s.tabs && job.closed && z < tab_top - 1e-9;
        const std::string* id = job.entity_id ? &*job.entity_id : nullptr;
        emit_loop(em, ring, job.closed, z_prev, z, use_tabs ? std::optional<double>(tab_top) : std::nullopt,
                  s, material_left, id);
        z_prev = z;

        if (final_pass && s.separate_last_pass && job.final_ring) {
            Points fring = job.closed ? orient_ring(*job.final_ring, want_ccw) : *job.final_ring;
            if (s.reverse_last_pass) fring = Points(fring.rbegin(), fring.rend());
            if (job.closed) {
                const Pt alvo = (s.use_start_points && job.anchor) ? *job.anchor : em.position();
                fring = rotate_to_start(fring, alvo);
            }
            emit_loop(em, fring, job.closed, z, z,
                      use_tabs ? std::optional<double>(tab_top) : std::nullopt, s, material_left, id);
        }
    }
}

Toolpath run_group(const std::string& name, const std::vector<const ProfileJob*>& jobs,
                   const Tool& tool, const cad::MaterialSetup& material, const ProfileSettings& s) {
    const double surface = material.surface_z();
    const double clearance = surface + material.clearance_z;
    Emitter em(clearance, material.plunge_gap);

    const std::vector<double> depths = compute_passes(s.total_depth(), tool.pass_depth, s.maintain_exact_step);
    const double tab_top = surface - s.start_depth - s.total_depth() + s.tab_thickness;

    // Mais aninhado primeiro: furo antes da peca que o contem, sempre.
    std::map<int, std::vector<const ProfileJob*>> by_level;
    for (const ProfileJob* j : jobs) by_level[j->level].push_back(j);

    std::optional<MachineOrder> escolhida;
    for (auto it = by_level.rbegin(); it != by_level.rend(); ++it) {
        const auto& group = it->second;
        std::vector<Points> rings;
        for (const ProfileJob* j : group) rings.push_back(j->ring);
        const auto [order, usada] = best_order(rings, machine_orders(s));
        if (!escolhida) escolhida = usada;
        for (int idx : order) machine_job(em, *group[static_cast<size_t>(idx)], depths, surface, tab_top, s);
    }

    em.retract();
    Toolpath path;
    path.name = name;
    path.kind = ToolpathType::Profile;
    path.tool_name = tool.name;
    path.tool_number = tool.tool_number;
    path.spindle_speed = tool.spindle_speed;
    path.feed_rate = tool.feed_rate;
    path.plunge_rate = tool.plunge_rate;
    path.moves = std::move(em.moves);
    if (machine_orders(s).size() > 1 && escolhida) {
        path.notes.push_back(std::format(
            "Ordem escolhida: {} — foi a que deu menos deslocamento em vazio entre as marcadas.",
            order_label(*escolhida)));
    }
    return path;
}

}  // namespace

std::vector<TabPreview> profile_tab_preview(const std::vector<cad::EntityPtr>& entities,
                                            const ProfileSettings& s, const Tool* tool) {
    if (!s.tabs) return {};
    std::vector<TabPreview> result;
    ProfileSettings automatic_settings = s;
    automatic_settings.manual_tabs.clear();

    std::vector<std::pair<std::string, Points>> rings;
    if (tool) {
        const auto [jobs, notes] = build_jobs(entities, *tool, s);
        for (const ProfileJob& job : jobs) {
            if (job.closed && job.entity_id && !job.entity_id->empty()) {
                rings.push_back({*job.entity_id, job.ring});
            }
        }
    } else {
        for (const cad::EntityPtr& e : entities) {
            if (e->closed()) rings.push_back({e->id, e->to_points(96)});
        }
    }

    for (const auto& [entity_id, ring] : rings) {
        if (ring.size() < 3) continue;
        const double total = path_length(close_ring(ring));
        if (!s.tab_manual_only) {
            std::vector<Span> spans = tab_spans(total, automatic_settings);
            // Uma ponte que cruza o ponto inicial vira dois trechos. Unimos a representacao
            // visual pelo centro circular correto.
            std::vector<double> centers;
            if (!spans.empty() && spans.front().first <= 1e-9 && spans.back().second >= total - 1e-9) {
                centers.push_back(fmod_py((spans.back().first + spans.front().second + total) / 2, total));
                spans = std::vector<Span>(spans.begin() + 1, spans.end() - 1);
            }
            for (const Span& sp : spans) centers.push_back((sp.first + sp.second) / 2);
            for (double center : centers) {
                const Pt p = point_at_ring_distance(ring, center);
                result.push_back({entity_id, p.x, p.y, false});
            }
        }
        for (const ManualTab& t : s.manual_tabs) {
            if (t.entity_id != entity_id) continue;
            const double center = nearest_ring_distance({t.x, t.y}, ring);
            const Pt p = point_at_ring_distance(ring, center);
            result.push_back({entity_id, p.x, p.y, true});
        }
    }
    return result;
}

std::vector<Toolpath> generate_profile(const std::vector<cad::EntityPtr>& entities,
                                       const Tool& tool, const cad::MaterialSetup& material,
                                       const ProfileSettings& s) {
    const auto [jobs, notes] = build_jobs(entities, tool, s);
    if (jobs.empty()) {
        Toolpath tp;
        tp.name = s.name;
        tp.kind = ToolpathType::Profile;
        tp.tool_name = tool.name;
        tp.tool_number = tool.tool_number;
        tp.spindle_speed = tool.spindle_speed;
        tp.feed_rate = tool.feed_rate;
        tp.plunge_rate = tool.plunge_rate;
        tp.notes = notes;
        return {tp};
    }

    std::vector<const ProfileJob*> interior, exterior, all;
    for (const ProfileJob& j : jobs) {
        all.push_back(&j);
        (j.interior ? interior : exterior).push_back(&j);
    }

    std::vector<std::pair<std::string, std::vector<const ProfileJob*>>> groups;
    if (s.separate_interior && !interior.empty() && !exterior.empty()) {
        groups.push_back({s.name + " — internos", interior});
        groups.push_back({s.name + " — externos", exterior});
    } else {
        groups.push_back({s.name, all});
    }

    std::vector<Toolpath> out;
    for (const auto& [gname, gjobs] : groups) {
        Toolpath tp = run_group(gname, gjobs, tool, material, s);
        // Os avisos do grupo (qual ordem ganhou) somam-se aos da geometria.
        std::vector<std::string> merged = notes;
        merged.insert(merged.end(), tp.notes.begin(), tp.notes.end());
        tp.notes = std::move(merged);
        out.push_back(std::move(tp));
    }
    return out;
}

}  // namespace cnc::cam
