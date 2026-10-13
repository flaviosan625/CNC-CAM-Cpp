// Porte de src/cam/vcarve.py.
#include "cam/vcarve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>

#include "utils/errors.hpp"

namespace cnc::cam {

const char* vcarve_material_label(VCarveMaterial m) {
    switch (m) {
        case VCarveMaterial::Mdf: return "MDF";
        case VCarveMaterial::Mdp: return "MDP";
        case VCarveMaterial::Plywood: return "Compensado";
        case VCarveMaterial::Softwood: return "Madeira macia (Pinus e similares)";
        case VCarveMaterial::Hardwood: return "Madeira dura";
        case VCarveMaterial::ExpandedPvc: return "PVC expandido";
        case VCarveMaterial::Acrylic: return "Acrílico";
        case VCarveMaterial::Other: return "Outro / parâmetros manuais";
    }
    return "";
}

const MaterialAdvice& material_advice(VCarveMaterial m) {
    static const MaterialAdvice mdf{4.0, 1.00, 1.00, 0.35, "Boa definição em letras e placas."};
    static const MaterialAdvice mdp{3.0, 0.85, 0.80, 0.30, "Evitar detalhes muito finos nas faces laminadas."};
    static const MaterialAdvice ply{3.0, 0.90, 0.80, 0.30, "Reduzir avanço se houver lascas na camada superior."};
    static const MaterialAdvice soft{4.0, 1.00, 0.85, 0.30, "Usar fresa afiada para reduzir fibras levantadas."};
    static const MaterialAdvice hard{2.5, 0.70, 0.65, 0.25, "Perfil conservador para madeira densa."};
    static const MaterialAdvice pvc{2.5, 1.00, 0.80, 0.35, "Evitar permanência no mesmo ponto para não aquecer."};
    static const MaterialAdvice acr{1.5, 0.60, 0.55, 0.20, "Usar fresa adequada para plástico e retirar cavaco."};
    static const MaterialAdvice other{3.0, 0.80, 0.70, 0.25, "Confirmar os dados de corte antes de usinar."};
    switch (m) {
        case VCarveMaterial::Mdf: return mdf;
        case VCarveMaterial::Mdp: return mdp;
        case VCarveMaterial::Plywood: return ply;
        case VCarveMaterial::Softwood: return soft;
        case VCarveMaterial::Hardwood: return hard;
        case VCarveMaterial::ExpandedPvc: return pvc;
        case VCarveMaterial::Acrylic: return acr;
        case VCarveMaterial::Other: return other;
    }
    return other;
}

namespace {

// ----------------------------------------------------------------------
// Profundidade x raio da fresa
// ----------------------------------------------------------------------

double depth_for_radius(const Tool& tool, double radius) {
    radius = std::max(0.0, radius);
    if (tool.tool_type == ToolType::BallNose) {
        const double r = std::max(tool.radius(), 1e-9);
        if (radius >= r) return r;
        return r - std::sqrt(std::max(0.0, r * r - radius * radius));
    }
    const double tip_radius = std::max(0.0, tool.tip_diameter / 2.0);
    const double tangent = std::tan(radians(tool.v_angle / 2.0));
    return std::max(0.0, radius - tip_radius) / std::max(tangent, 1e-9);
}

double radius_at_depth(const Tool& tool, double depth) {
    depth = std::max(0.0, depth);
    if (tool.tool_type == ToolType::BallNose) {
        const double r = std::max(tool.radius(), 1e-9);
        depth = std::min(depth, r);
        return std::sqrt(std::max(0.0, 2.0 * r * depth - depth * depth));
    }
    return std::max(0.0, tool.tip_diameter / 2.0) + depth * std::tan(radians(tool.v_angle / 2.0));
}

double maximum_radius(const geo::Geom& poly, double tolerance) {
    const auto [center, radius] = geo::maximum_inscribed_circle(poly, std::max(tolerance, 0.001));
    (void)center;
    return radius > 0 ? radius : 0.0;
}

Toolpath empty_path(const std::string& name, const Tool& tool, const std::string& note) {
    Toolpath tp;
    tp.name = name;
    tp.kind = ToolpathType::VCarve;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.notes = {note};
    return tp;
}

// ----------------------------------------------------------------------
// Eixo medial
// ----------------------------------------------------------------------

// distancia entre dois pontos como o GEOS a calcula (sqrt(dx^2 + dy^2)); o Python usa
// shapely .distance entre pontos nesses testes.
double geos_dist(Pt a, Pt b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

Points sample_ring(const geo::Geom& ring, double step) {
    const double length = ring.length();
    const int count = std::max(3, std::min(6000, static_cast<int>(std::ceil(length / step))));
    Points points;
    points.reserve(static_cast<size_t>(count) + 1);
    for (int i = 0; i < count; ++i) points.push_back(ring.interpolate(i * length / count));
    points.push_back(points.front());
    return points;
}

struct SegMeta {
    int ring;
    int index;
    int count;
    double angle;
};

bool independent_segments(const SegMeta& a, const SegMeta& b, Pt contact_a, Pt contact_b, double contact_gap) {
    if (geos_dist(contact_a, contact_b) <= contact_gap * 0.25) return false;
    if (a.ring != b.ring) return true;
    int separation = std::abs(a.index - b.index);
    separation = std::min(separation, a.count - separation);
    if ((a.count <= 8 && separation > 1) || separation > 2) return true;
    double angle = std::fabs(fmod_py(a.angle - b.angle + kPi, 2.0 * kPi) - kPi);
    angle = std::min(angle, kPi - angle);
    return angle > radians(15.0) && geos_dist(contact_a, contact_b) > contact_gap;
}

bool has_distinct_contacts(const geo::Geom& point, Pt point_xy, const std::vector<geo::Geom>& segments,
                           const std::vector<SegMeta>& metadata, const geo::STRtree& tree, double tie,
                           double contact_gap) {
    const int nearest = tree.nearest(point);
    if (nearest < 0) return false;
    const double distance = segments[static_cast<size_t>(nearest)].distance(point);
    if (!std::isfinite(distance)) return false;
    const geo::Geom around = point.buffer(distance + tie);
    struct Contact {
        Pt xy;
        SegMeta meta;
    };
    std::vector<Contact> contacts;
    for (int raw : tree.query(around)) {
        const geo::Geom& segment = segments[static_cast<size_t>(raw)];
        if (segment.distance(point) > distance + tie) continue;
        const Pt contact = geo::nearest_points(point, segment).second;
        const SegMeta& meta = metadata[static_cast<size_t>(raw)];
        for (const Contact& old : contacts) {
            if (independent_segments(meta, old.meta, contact, old.xy, contact_gap)) return true;
        }
        contacts.push_back({contact, meta});
    }
    (void)point_xy;
    return false;
}

void extend_paths_to_corners(std::vector<Points>& paths, const std::vector<geo::Geom>& rings, double step) {
    Points corners;
    for (const geo::Geom& ring : rings) {
        Points c = ring.coords();
        if (!c.empty()) c.pop_back();
        corners.insert(corners.end(), c.begin(), c.end());
    }
    if (corners.empty()) return;
    for (Points& path : paths) {
        if (path.size() < 2) continue;
        for (bool at_start : {true, false}) {
            const Pt endpoint = at_start ? path.front() : path.back();
            Pt corner = corners[0];
            double best = dist(endpoint, corner);
            for (const Pt& c : corners) {
                const double d = dist(endpoint, c);
                if (d < best) {
                    best = d;
                    corner = c;
                }
            }
            if (dist(endpoint, corner) > step * 1.05) continue;
            if (at_start) {
                path.insert(path.begin(), corner);
            } else {
                path.push_back(corner);
            }
        }
    }
}

Points resample_line(const geo::Geom& line, double step) {
    const int count = std::max(1, static_cast<int>(std::ceil(line.length() / step)));
    Points out;
    for (int i = 0; i <= count; ++i) out.push_back(line.interpolate_normalized(static_cast<double>(i) / count));
    return out;
}

double boundary_distance_span(const geo::Geom& boundary, const Points& path) {
    if (path.empty()) return 0.0;
    double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
    for (const Pt& p : path) {
        const double d = boundary.distance(geo::make_point(p));
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return hi - lo;
}

// Linhas simples dentro de qualquer geometria (recursivo em Multi*/colecoes).
void collect_lines(const geo::Geom& g, std::vector<geo::Geom>& out) {
    if (!g || g.is_empty()) return;
    const std::string t = g.geom_type();
    if (t == "LineString" || t == "LinearRing") {
        out.push_back(g);
        return;
    }
    if (t == "MultiLineString" || t == "GeometryCollection" || t == "MultiPolygon" || t == "MultiPoint") {
        for (const geo::Geom& part : g.geoms()) collect_lines(part, out);
    }
}

}  // namespace

std::vector<Points> medial_paths(const geo::Geom& poly, double sample_step, double tolerance) {
    if (poly.is_empty() || poly.area() <= tolerance * tolerance) return {};
    const double step = std::max(sample_step, tolerance * 4.0);
    const double compactness = 4.0 * kPi * poly.area() / std::max(poly.length() * poly.length(), 1e-12);
    const std::vector<geo::Geom> interiors = poly.interior_rings();
    if (interiors.empty() && compactness > 0.97) {
        const auto [center, radius] = geo::maximum_inscribed_circle(poly, std::max(tolerance, step * 0.05));
        (void)radius;
        return {{center}};
    }

    std::vector<geo::Geom> rings;
    rings.push_back(poly.exterior_ring());
    rings.insert(rings.end(), interiors.begin(), interiors.end());
    Points sites;
    for (const geo::Geom& ring : rings) {
        const Points sampled = sample_ring(ring, step);
        sites.insert(sites.end(), sampled.begin(), sampled.end() - 1);
    }
    if (sites.size() < 3) return {};

    const geo::Geom voronoi = geo::voronoi_polygons(geo::make_multipoint(sites), 0.0, true, poly.envelope());
    if (!voronoi) return {};
    const geo::Geom diagram = voronoi.intersection(poly);
    std::vector<geo::Geom> raw_lines;
    collect_lines(diagram, raw_lines);

    // A filtragem usa os segmentos reais da geometria. Usar os proprios pontos de amostragem
    // aqui criaria falsos contatos entre dois pedacinhos consecutivos da mesma parede reta.
    std::vector<geo::Geom> segments;
    std::vector<SegMeta> segment_meta;
    for (size_t ring_index = 0; ring_index < rings.size(); ++ring_index) {
        const Points coords = rings[ring_index].coords();
        const int count = static_cast<int>(coords.size()) - 1;
        for (int si = 0; si + 1 < static_cast<int>(coords.size()); ++si) {
            const Pt a = coords[static_cast<size_t>(si)], b = coords[static_cast<size_t>(si) + 1];
            if (a == b) continue;
            segments.push_back(geo::make_linestring({a, b}));
            segment_meta.push_back({static_cast<int>(ring_index), si, count, std::atan2(b.y - a.y, b.x - a.x)});
        }
    }
    const geo::STRtree tree(segments);
    const double tie = std::max(tolerance * 3.0, step * 0.08);
    const double contact_gap = std::max(tolerance * 6.0, step * 0.75);

    std::vector<geo::Geom> kept;
    for (const geo::Geom& line : raw_lines) {
        if (line.length() <= tolerance) continue;
        const Pt middle_xy = line.interpolate_normalized(0.5);
        const geo::Geom middle = geo::make_point(middle_xy);
        if (has_distinct_contacts(middle, middle_xy, segments, segment_meta, tree, tie, contact_gap)) {
            kept.push_back(line);
        }
    }

    std::vector<geo::Geom> merged_lines;
    if (!kept.empty()) collect_lines(geo::line_merge(geo::unary_union(kept)), merged_lines);

    const geo::Geom boundary = poly.boundary();
    std::vector<Points> result;
    for (const geo::Geom& line : merged_lines) {
        if (line.length() <= tolerance) continue;
        Points path = resample_line(line, step);
        if (path_length(path) > 2.0 * step || boundary_distance_span(boundary, path) > 2.0 * step) {
            result.push_back(std::move(path));
        }
    }
    extend_paths_to_corners(result, rings, step);

    // Regioes redondas podem ter eixo medial reduzido a um unico ponto.
    std::vector<Pt> medial_points;
    {
        const auto [center, radius] = geo::maximum_inscribed_circle(poly, std::max(tolerance, step * 0.05));
        if (radius >= 0) medial_points.push_back(center);
    }
    std::vector<Pt> unique_points;
    for (const Pt& point : medial_points) {
        bool seen = false;
        for (const Pt& old : unique_points) {
            if (dist(point, old) <= tolerance) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        unique_points.push_back(point);
        const geo::Geom pg = geo::make_point(point);
        bool near_path = false;
        for (const Points& path : result) {
            if (path.size() <= 1) continue;
            const geo::Geom line = geo::make_linestring(path);
            if (line && line.distance(pg) <= step * 0.25) {
                near_path = true;
                break;
            }
        }
        if (!near_path) result.push_back({point});
    }
    return result;
}

namespace {

// Ordena os caminhos pelo vizinho mais proximo, podendo inverter cada um.
std::vector<Points> order_paths(const std::vector<Points>& paths) {
    std::vector<Points> pending;
    for (const Points& p : paths) {
        if (!p.empty()) pending.push_back(p);
    }
    std::vector<Points> ordered;
    std::optional<Pt> current;
    while (!pending.empty()) {
        size_t index = 0;
        bool reverse = false;
        if (current) {
            // min() sobre tuplas (distancia, indice, inverter): desempata por indice, depois False < True
            double best_d = std::numeric_limits<double>::infinity();
            bool have = false;
            for (size_t i = 0; i < pending.size(); ++i) {
                for (bool rev : {false, true}) {
                    const double d = dist(*current, rev ? pending[i].back() : pending[i].front());
                    if (!have || d < best_d) {
                        best_d = d;
                        index = i;
                        reverse = rev;
                        have = true;
                    }
                }
            }
        }
        Points path = pending[index];
        pending.erase(pending.begin() + static_cast<long>(index));
        if (reverse) std::reverse(path.begin(), path.end());
        current = path.back();
        ordered.push_back(std::move(path));
    }
    return ordered;
}

Toolpath finish_path(const std::vector<geo::Geom>& polygons, const Tool& tool, const cad::MaterialSetup& material,
                     const VCarveSettings& s, double depth_cap) {
    const double surface = material.surface_z();
    const double clearance = surface + material.clearance_z;
    Emitter em(clearance, material.plunge_gap);
    std::vector<std::string> notes;

    std::vector<Points> all_paths;
    for (const geo::Geom& poly : polygons) {
        for (Points& p : medial_paths(poly, std::max(s.finish_stepover, s.tolerance * 4.0), s.tolerance)) {
            all_paths.push_back(std::move(p));
        }
    }
    all_paths = order_paths(all_paths);

    // buffer e contorno de cada poligono nao mudam: calculados uma vez so
    std::vector<geo::Geom> buffered, boundaries;
    for (const geo::Geom& p : polygons) {
        buffered.push_back(p.buffer(s.tolerance));
        boundaries.push_back(p.boundary());
    }

    struct Target {
        Points path;
        std::vector<double> depths;
    };
    std::vector<Target> targets;
    for (const Points& path : all_paths) {
        const geo::Geom start_pt = geo::make_point(path[0]);
        int owner = -1;
        for (size_t i = 0; i < polygons.size(); ++i) {
            if (buffered[i].covers(start_pt)) {
                owner = static_cast<int>(i);
                break;
            }
        }
        if (owner < 0) continue;
        std::vector<double> depths;
        depths.reserve(path.size());
        for (const Pt& p : path) {
            const double d = boundaries[static_cast<size_t>(owner)].distance(geo::make_point(p));
            depths.push_back(std::min(depth_cap, depth_for_radius(tool, d)));
        }
        const double max_depth = *std::max_element(depths.begin(), depths.end());
        if (max_depth > s.tolerance) {
            size_t deepest = 0;
            for (size_t i = 1; i < depths.size(); ++i) {
                if (depths[i] > depths[deepest]) deepest = i;
            }
            Target forward{Points(path.begin() + static_cast<long>(deepest), path.end()),
                           std::vector<double>(depths.begin() + static_cast<long>(deepest), depths.end())};
            Points back_path(path.begin(), path.begin() + static_cast<long>(deepest) + 1);
            std::reverse(back_path.begin(), back_path.end());
            std::vector<double> back_depths(depths.begin(), depths.begin() + static_cast<long>(deepest) + 1);
            std::reverse(back_depths.begin(), back_depths.end());
            Target backward{std::move(back_path), std::move(back_depths)};
            for (Target* branch : {&forward, &backward}) {
                if (branch->path.size() > 1 || targets.empty()) targets.push_back(std::move(*branch));
            }
        }
    }

    double maximum = 0.0;
    for (const Target& t : targets) maximum = std::max(maximum, *std::max_element(t.depths.begin(), t.depths.end()));
    const std::vector<double> levels = compute_passes(maximum, tool.pass_depth, s.maintain_exact_step);
    for (double level : levels) {
        for (const Target& target : targets) {
            std::vector<double> depths;
            for (double d : target.depths) depths.push_back(std::min(d, level));
            if (*std::max_element(depths.begin(), depths.end()) <= s.tolerance) continue;
            // Cada ramo comeca na parte profunda. Assim o corte sobe em direcao ao canto sem
            // atravessar de um extremo ao outro pelo material.
            em.rapid_to(target.path[0].x, target.path[0].y);
            em.plunge_to(surface - s.start_depth - depths[0]);
            for (size_t i = 1; i < target.path.size(); ++i) {
                const double z = surface - s.start_depth - depths[i];
                em.cut_to(target.path[i].x, target.path[i].y, z, depths[i] > depths[0]);
            }
        }
    }
    em.retract();
    if (targets.empty()) notes.push_back("Não foi possível construir o eixo central da seleção.");

    Toolpath tp;
    tp.name = "[Acabamento V] " + s.name;
    tp.kind = ToolpathType::VCarve;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(em.moves);
    tp.notes = std::move(notes);
    return tp;
}

std::vector<Toolpath> clear_flat_area(const std::vector<geo::Geom>& polygons, const Tool& vtool,
                                      const cad::MaterialSetup& material, const VCarveSettings& s,
                                      double wall_radius, double depth) {
    std::vector<Toolpath> outputs;
    std::vector<Tool> tools = s.clearance_tools;
    std::stable_sort(tools.begin(), tools.end(), [](const Tool& a, const Tool& b) { return a.diameter > b.diameter; });
    int index = 1;
    for (const Tool& clear_tool : tools) {
        PocketSettings ps;
        ps.name = std::format("[Desbaste {}] {}", index++, s.name);
        ps.start_depth = s.start_depth;
        ps.cut_depth = depth;
        ps.maintain_exact_step = s.maintain_exact_step;
        ps.fill = s.clearance_fill;
        ps.raster_angle = s.raster_angle;
        ps.direction = s.direction;
        ps.stepover = s.clearance_stepover > 0 ? s.clearance_stepover : clear_tool.stepover;
        ps.profile_pass = ProfilePass::None;
        ps.allowance = wall_radius + (s.skip_flat_regions ? 0.0 : std::max(0.0, s.clearance_allowance));
        ps.ramp = s.ramp;
        ps.ramp_distance = s.ramp_distance;
        ps.order = s.order;
        ps.tolerance = s.tolerance;
        for (Toolpath& tp : generate_pocket_polygons(polygons, clear_tool, material, ps)) {
            tp.kind = ToolpathType::VCarve;
            tp.notes.push_back("Executar antes do acabamento com a fresa V.");
            outputs.push_back(std::move(tp));
        }
    }

    if (!s.skip_flat_regions || tools.empty()) {
        Tool effective = vtool;
        effective.tool_type = ToolType::EndMill;
        effective.diameter = std::max(2.0 * wall_radius, s.tolerance * 2.0);
        effective.stepover = s.finish_stepover > 0 ? s.finish_stepover
                                                   : std::max(s.tolerance, effective.diameter * 0.1);
        PocketSettings ps;
        ps.name = "[Fundo plano] " + s.name;
        ps.start_depth = s.start_depth;
        ps.cut_depth = depth;
        ps.maintain_exact_step = s.maintain_exact_step;
        ps.fill = FillType::Offset;
        ps.direction = s.direction;
        ps.stepover = effective.stepover;
        ps.profile_pass = ProfilePass::None;
        ps.allowance = 0.0;
        ps.ramp = s.ramp;
        ps.ramp_distance = s.ramp_distance;
        ps.order = s.order;
        ps.tolerance = s.tolerance;
        for (Toolpath& tp : generate_pocket_polygons(polygons, effective, material, ps)) {
            tp.kind = ToolpathType::VCarve;
            tp.tool_name = vtool.name;
            tp.tool_number = vtool.tool_number;
            tp.spindle_speed = vtool.spindle_speed;
            tp.feed_rate = vtool.feed_rate;
            tp.plunge_rate = vtool.plunge_rate;
            outputs.push_back(std::move(tp));
        }
    }
    return outputs;
}

}  // namespace

VCarveSettings automatic_settings(const Tool& tool, VCarveMaterial material_profile, double material_thickness) {
    const MaterialAdvice& advice = material_advice(material_profile);
    const double physical = tool.max_v_depth();
    const double cap = std::min({advice.max_flat_depth, std::max(0.2, material_thickness * 0.35), physical});
    VCarveSettings s;
    s.mode = VCarveMode::Automatic;
    s.material_profile = material_profile;
    s.max_depth = cap;
    s.finish_stepover = std::max(0.15, std::min(0.5, tool.diameter * 0.03));
    s.clearance_stepover = std::max(0.1, tool.diameter * advice.stepover_factor);
    return s;
}

Tool advised_tool(const Tool& tool, VCarveMaterial material_profile) {
    Tool out = tool;
    const MaterialAdvice& advice = material_advice(material_profile);
    out.feed_rate *= advice.feed_factor;
    out.plunge_rate *= advice.plunge_factor;
    return out;
}

VCarveRecommendation recommend_vcarve(const std::vector<cad::EntityPtr>& entities,
                                      const std::vector<std::shared_ptr<Tool>>& tools,
                                      const cad::MaterialSetup& material, VCarveMaterial material_profile) {
    const RegionsResult found = regions_from(entities, kDefaultTolerance);
    std::vector<geo::Geom> polygons;
    for (const Region& r : found.regions) polygons.push_back(r.to_polygon());
    std::vector<std::shared_ptr<Tool>> candidates;
    for (const auto& t : tools) {
        if (t->tool_type == ToolType::VBit || t->tool_type == ToolType::Engraving || t->tool_type == ToolType::BallNose) {
            candidates.push_back(t);
        }
    }
    if (polygons.empty() || candidates.empty()) {
        VCarveSettings s;
        s.material_profile = material_profile;
        return {nullptr, s, "Cadastre uma fresa V e selecione pelo menos um vetor fechado."};
    }

    double radius = 0.0;
    bool first = true;
    for (const geo::Geom& poly : polygons) {
        const double r = maximum_radius(poly, kDefaultTolerance);
        radius = first ? r : std::max(radius, r);
        first = false;
    }
    const double target_angle = radius < 2.0 ? 60.0 : 90.0;

    auto score = [&](const Tool& c) -> std::array<double, 3> {
        if (c.tool_type == ToolType::BallNose) return {2, c.diameter, 0};
        const double kind_penalty = c.tool_type == ToolType::VBit ? 0 : 1;
        return {kind_penalty, std::fabs(c.v_angle - target_angle), -c.diameter};
    };
    std::shared_ptr<Tool> vtool = candidates[0];
    for (const auto& c : candidates) {
        if (score(*c) < score(*vtool)) vtool = c;
    }

    VCarveSettings settings = automatic_settings(*vtool, material_profile, material.thickness);
    const double wall_radius = radius_at_depth(*vtool, settings.max_depth);
    const double flat_core_radius = std::max(0.0, radius - wall_radius);
    std::shared_ptr<Tool> best_clear;
    for (const auto& t : tools) {
        if (t->tool_type == ToolType::EndMill && t->radius() <= flat_core_radius + kDefaultTolerance) {
            if (!best_clear || t->diameter > best_clear->diameter) best_clear = t;
        }
    }
    if (best_clear) settings.clearance_tools = {*best_clear};

    const double geometric_depth = depth_for_radius(*vtool, radius);
    std::string depth_text;
    if (geometric_depth <= settings.max_depth + kDefaultTolerance) {
        depth_text = std::format("profundidade geométrica aproximada {:.2f} mm", geometric_depth);
    } else {
        depth_text = std::format("limitado a {:.2f} mm; as regiões largas terão fundo plano", settings.max_depth);
    }
    const std::string clear_text = !settings.clearance_tools.empty()
                                       ? "desbaste com " + settings.clearance_tools[0].name
                                       : "sem desbaste auxiliar necessário";
    const std::string report = std::format("{} · {} · {}. Todos os valores permanecem editáveis.", vtool->name,
                                           depth_text, clear_text);
    return {vtool, settings, report};
}

std::vector<Toolpath> generate_vcarve(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const VCarveSettings& s) {
    if (tool.tool_type != ToolType::VBit && tool.tool_type != ToolType::Engraving &&
        tool.tool_type != ToolType::BallNose) {
        throw ValueError("Entalhe V exige fresa em V, ponta de gravação ou esférica.");
    }
    if (tool.tool_type != ToolType::BallNose && !(0.0 < tool.v_angle && tool.v_angle < 180.0)) {
        throw ValueError("Ângulo da fresa V deve estar entre 0° e 180°.");
    }

    const RegionsResult found = regions_from(entities, s.tolerance);
    std::vector<geo::Geom> polygons;
    for (const Region& r : found.regions) polygons.push_back(r.to_polygon());
    if (polygons.empty()) return {empty_path(s.name, tool, "O Entalhe V precisa de vetores fechados.")};

    const Tool work_tool = s.mode == VCarveMode::Automatic ? advised_tool(tool, s.material_profile) : tool;
    const double available = std::max(0.0, material.thickness - s.start_depth);
    const double physical_depth =
        work_tool.tool_type == ToolType::BallNose ? work_tool.radius() : work_tool.max_v_depth();
    const double physical_cap = std::min(physical_depth, available);
    const double requested_cap = s.flat_depth > 0 ? s.flat_depth : s.max_depth;
    const double depth_cap = requested_cap > 0 ? std::min(physical_cap, requested_cap) : physical_cap;
    if (depth_cap <= 1e-9) return {empty_path(s.name, work_tool, "Não há profundidade disponível no material.")};
    const double flat_radius = radius_at_depth(work_tool, depth_cap);

    double deepest_geometry = 0.0;
    bool first = true;
    for (const geo::Geom& poly : polygons) {
        const double r = maximum_radius(poly, s.tolerance);
        deepest_geometry = first ? r : std::max(deepest_geometry, r);
        first = false;
    }
    const double geometry_depth = depth_for_radius(work_tool, deepest_geometry);
    const bool capped = geometry_depth > depth_cap + s.tolerance;

    std::vector<Toolpath> outputs;
    if (capped) {
        for (Toolpath& tp : clear_flat_area(polygons, work_tool, material, s, flat_radius, depth_cap)) {
            outputs.push_back(std::move(tp));
        }
    }

    Toolpath finish = finish_path(polygons, work_tool, material, s, depth_cap);
    if (capped && s.flat_depth <= 0) {
        finish.notes.push_back(std::format("Profundidade limitada a {:g} mm; regiões largas receberam fundo plano.",
                                           depth_cap));
    }
    finish.notes.push_back(material_advice(s.material_profile).note);
    outputs.push_back(std::move(finish));
    return outputs;
}

}  // namespace cnc::cam
