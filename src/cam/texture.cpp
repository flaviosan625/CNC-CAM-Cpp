// Porte de src/cam/texture.py.
#include "cam/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <sstream>

#include "cam/nesting.hpp"
#include "utils/errors.hpp"
#include "utils/py_random.hpp"

namespace cnc::cam {

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n\f\v";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// float(texto) do Python; texto invalido e ValueError.
double to_float(const std::string& text) {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        throw ValueError("could not convert string to float: '" + text + "'");
    }
    return v;
}

double randomize(double val, double var_pct, PyRandom& rng) {
    if (var_pct <= 0) return val;
    const double delta = val * (var_pct / 100.0);
    return std::max(0.01, val + rng.uniform(-delta, delta));
}

// Pares de pontas de cada LineString, inclusive dentro de colecoes.
void line_segments(const geo::Geom& g, std::vector<std::pair<Pt, Pt>>& out) {
    if (!g || g.is_empty()) return;
    const std::string t = g.geom_type();
    if (t == "LineString") {
        const Points coords = g.coords();
        if (coords.size() >= 2) out.push_back({coords.front(), coords.back()});
        return;
    }
    if (t.rfind("Multi", 0) == 0 || t == "GeometryCollection") {
        for (const geo::Geom& part : g.geoms()) line_segments(part, out);
    }
}

}  // namespace

TextureSettings load_vtexture(const std::filesystem::path& path) {
    TextureSettings s;
    std::istringstream in(read_text_file(path));
    std::string line;
    while (std::getline(in, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon));
        std::istringstream rest(trim(line.substr(colon + 1)));
        std::vector<std::string> parts;
        for (std::string p; rest >> p;) parts.push_back(p);
        if (parts.empty()) continue;
        const double val = to_float(parts[0]);
        const double var = parts.size() > 1 ? to_float(parts[1]) : 0.0;
        if (key == "ToolRadius") {
            s.tool_radius = val;
        } else if (key == "Length") {
            s.length = val;
            s.length_var = var;
        } else if (key == "Depth") {
            s.depth = val;
            s.depth_var = var;
        } else if (key == "Stepover") {
            s.stepover = val;
            s.stepover_var = var;
        } else if (key == "Overlap") {
            s.overlap = val;
            s.overlap_var = var;
        } else if (key == "Angle") {
            s.angle = val;
            s.angle_var = var;
        }
    }
    return s;
}

void save_vtexture(const std::filesystem::path& path, const TextureSettings& s) {
    const std::string text = std::format(
        "Vectric Vector Texture Definition\nVersion: 1\nType: 1\nUnits: 1\n"
        "ToolRadius: {:.5f}\nLength: {:.5f} {:.0f}\nDepth: {:.5f} {:.0f}\nStepover: {:.5f} {:.0f}\n"
        "Overlap: {:.5f} {:.0f}\nAngle: {:.5f} {:.0f}\n",
        s.tool_radius, s.length, s.length_var, s.depth, s.depth_var, s.stepover, s.stepover_var, s.overlap,
        s.overlap_var, s.angle, s.angle_var);
    write_text_file(path, text);
}

std::vector<Toolpath> generate_texture(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const TextureSettings& s) {
    const double safe_z = material.surface_z() + material.clearance_z;
    const double surface = material.surface_z();
    PyRandom rng(s.seed);

    const RegionsResult found = regions_from(entities);
    if (found.regions.empty()) return {};
    std::vector<geo::Geom> polys;
    for (const Region& r : found.regions) polys.push_back(r.to_polygon());
    // O centro da ferramenta precisa ficar pelo menos um raio para dentro; assim a textura nao
    // ultrapassa a borda nem invade os furos.
    const geo::Geom cutting_area = geo::unary_union(polys).buffer(-tool.radius());
    if (!cutting_area || cutting_area.is_empty()) return {};
    const BBox b = cutting_area.bounds();

    std::vector<Move> moves;
    const double base_angle = radians(s.angle);

    double y = b.y0;
    while (y <= b.y1) {
        const double step = randomize(s.stepover, s.stepover_var, rng);
        double x = b.x0;
        while (x <= b.x1) {
            const double stroke_len = randomize(s.length, s.length_var, rng);
            const double stroke_depth = randomize(s.depth, s.depth_var, rng);
            const double stroke_angle = base_angle + radians(randomize(0, s.angle_var, rng));
            const double overlap = randomize(s.overlap, s.overlap_var, rng);

            const double dx = std::cos(stroke_angle) * stroke_len;
            const double dy = std::sin(stroke_angle) * stroke_len;

            const double sx = x, sy = y;
            const double ex = x + dx, ey = y + dy;

            const double z = surface - stroke_depth;
            const geo::Geom clipped = cutting_area.intersection(geo::make_linestring({{sx, sy}, {ex, ey}}));
            std::vector<std::pair<Pt, Pt>> segments;
            line_segments(clipped, segments);
            std::stable_sort(segments.begin(), segments.end(), [&](const auto& a, const auto& bb) {
                return dist(Pt{sx, sy}, a.first) < dist(Pt{sx, sy}, bb.first);
            });
            for (const auto& [c0, c1] : segments) {
                if (dist(c0, c1) <= 1e-6) continue;
                moves.push_back({MoveKind::Rapid, c0.x, c0.y, safe_z});
                moves.push_back({MoveKind::Plunge, c0.x, c0.y, surface});
                const double mid_x = (c0.x + c1.x) / 2, mid_y = (c0.y + c1.y) / 2;
                moves.push_back({MoveKind::Cut, mid_x, mid_y, z});
                moves.push_back({MoveKind::Cut, c1.x, c1.y, surface});
                moves.push_back({MoveKind::Rapid, c1.x, c1.y, safe_z});
            }

            const double advance = stroke_len * (1.0 - overlap);
            // Sobreposicao de 100% nao pode travar o calculo: avanco minimo de 10% do traco.
            x += std::max({0.5, stroke_len * 0.10, advance});
        }
        y += step;
    }

    if (moves.empty()) return {};
    Toolpath tp;
    tp.name = s.name;
    tp.kind = ToolpathType::Texture;
    tp.tool_name = tool.name;
    tp.tool_number = tool.tool_number;
    tp.spindle_speed = tool.spindle_speed;
    tp.feed_rate = tool.feed_rate;
    tp.plunge_rate = tool.plunge_rate;
    tp.moves = std::move(moves);
    return {std::move(tp)};
}

}  // namespace cnc::cam
