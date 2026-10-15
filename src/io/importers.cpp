// Porte de src/cad/importers/__init__.py.
#include "io/importers.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <format>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <QXmlStreamReader>
#include <QFile>
#include <QBuffer>

#ifdef CNC_HAVE_MUPDF
#include <mupdf/classes.h>
#endif

#include "cad/document.hpp"

namespace cnc::io {
namespace {

double number_or(const QString& text, double fallback = 0.0) {
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? value : fallback;
}

double svg_length(const QString& text) {
    QString numeric;
    for (const QChar ch : text) {
        if (ch.isDigit() || ch == '.' || ch == '-' || ch == '+' || ch == 'e' || ch == 'E') numeric += ch;
        else if (!numeric.isEmpty()) break;
    }
    return number_or(numeric);
}

void normalize(std::vector<cad::EntityPtr>& entities) {
    if (entities.empty()) return;
    bool first = true;
    BBox bounds{};
    for (const auto& entity : entities) {
        const BBox box = entity->bounding_box();
        if (first) {
            bounds = box;
            first = false;
        } else {
            bounds.x0 = std::min(bounds.x0, box.x0);
            bounds.y0 = std::min(bounds.y0, box.y0);
            bounds.x1 = std::max(bounds.x1, box.x1);
            bounds.y1 = std::max(bounds.y1, box.y1);
        }
    }
    if (std::fabs(bounds.x0) > 1e-9 || std::fabs(bounds.y0) > 1e-9)
        for (const auto& entity : entities) entity->translate(-bounds.x0, -bounds.y0);
}

void flip_svg_y(std::vector<cad::EntityPtr>& entities, double height) {
    if (height <= 0.0) return;
    for (const auto& entity : entities) entity->scale(1.0, -1.0, 0.0, height / 2.0);
}

struct SvgToken {
    bool command = false;
    char letter = 0;
    double number = 0.0;
};

std::vector<SvgToken> tokenize_path(const QString& data) {
    const std::string text = data.toStdString();
    std::vector<SvgToken> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ',')) ++i;
        if (i >= text.size()) break;
        if (std::isalpha(static_cast<unsigned char>(text[i]))) {
            out.push_back({true, text[i], 0.0});
            ++i;
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(text.c_str() + i, &end);
        if (end == text.c_str() + i) {
            ++i;
            continue;
        }
        out.push_back({false, 0, value});
        i = static_cast<size_t>(end - text.c_str());
    }
    return out;
}

bool same_point(const Pt& a, const Pt& b) { return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9; }

void append_cubic(Points& points, const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3) {
    for (int step = 1; step <= 16; ++step) {
        const double t = static_cast<double>(step) / 16.0;
        const double u = 1.0 - t;
        points.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                          u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
    }
}

void append_quadratic(Points& points, const Pt& p0, const Pt& p1, const Pt& p2) {
    for (int step = 1; step <= 16; ++step) {
        const double t = static_cast<double>(step) / 16.0;
        const double u = 1.0 - t;
        points.push_back({u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
                          u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
}

// Conversao endpoint-to-center da especificacao SVG 1.1, amostrada em 16 pontos.
void append_arc(Points& points, Pt start, double rx, double ry, double rotation,
                bool large_arc, bool sweep, Pt end) {
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < 1e-12 || ry < 1e-12 || same_point(start, end)) {
        points.push_back(end);
        return;
    }
    const double phi = rotation * M_PI / 180.0;
    const double cos_phi = std::cos(phi), sin_phi = std::sin(phi);
    const double dx = (start.x - end.x) / 2.0, dy = (start.y - end.y) / 2.0;
    const double xp = cos_phi * dx + sin_phi * dy;
    const double yp = -sin_phi * dx + cos_phi * dy;
    const double lambda = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lambda > 1.0) {
        const double scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }
    const double numerator = std::max(0.0, (rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp));
    const double denominator = rx * rx * yp * yp + ry * ry * xp * xp;
    const double factor = denominator < 1e-12 ? 0.0 : std::sqrt(numerator / denominator) * (large_arc == sweep ? -1.0 : 1.0);
    const double cxp = factor * rx * yp / ry;
    const double cyp = factor * -ry * xp / rx;
    const double cx = cos_phi * cxp - sin_phi * cyp + (start.x + end.x) / 2.0;
    const double cy = sin_phi * cxp + cos_phi * cyp + (start.y + end.y) / 2.0;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    double theta = angle(1.0, 0.0, (xp - cxp) / rx, (yp - cyp) / ry);
    double delta = angle((xp - cxp) / rx, (yp - cyp) / ry,
                         (-xp - cxp) / rx, (-yp - cyp) / ry);
    if (!sweep && delta > 0.0) delta -= 2 * M_PI;
    if (sweep && delta < 0.0) delta += 2 * M_PI;
    const int count = std::max(4, static_cast<int>(std::ceil(std::fabs(delta) / (M_PI / 8.0))));
    for (int i = 1; i <= count; ++i) {
        const double a = theta + delta * static_cast<double>(i) / count;
        points.push_back({cx + cos_phi * rx * std::cos(a) - sin_phi * ry * std::sin(a),
                          cy + sin_phi * rx * std::cos(a) + cos_phi * ry * std::sin(a)});
    }
}

std::vector<cad::EntityPtr> path_entities(const QString& data) {
    const auto tokens = tokenize_path(data);
    std::vector<cad::EntityPtr> entities;
    Points points;
    Pt current{}, start{}, cubic_control{}, quadratic_control{};
    char command = 0;
    bool closed = false;
    bool have_cubic_control = false, have_quadratic_control = false;
    const auto flush_path = [&] {
        if (points.size() >= 2) {
            auto polyline = std::make_shared<cad::Polyline>();
            polyline->points = points;
            polyline->is_closed = closed;
            entities.push_back(polyline);
        }
        points.clear();
        closed = false;
    };
    size_t i = 0;
    const auto has_numbers = [&](size_t count) {
        if (i + count > tokens.size()) return false;
        for (size_t j = 0; j < count; ++j) if (tokens[i + j].command) return false;
        return true;
    };
    const auto number = [&](size_t offset) { return tokens[i + offset].number; };
    while (i < tokens.size()) {
        if (tokens[i].command) {
            command = tokens[i++].letter;
            if (command == 'Z' || command == 'z') {
                if (!points.empty()) {
                    if (!same_point(current, start)) points.push_back(start);
                    current = start;
                    closed = true;
                    flush_path();
                }
                command = 0;
                continue;
            }
        } else if (!command) {
            ++i;
            continue;
        }
        const bool relative = std::islower(static_cast<unsigned char>(command));
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        if (upper == 'M' || upper == 'L' || upper == 'T') {
            if (!has_numbers(2)) { command = 0; continue; }
            Pt next{number(0), number(1)};
            i += 2;
            if (relative) next = {current.x + next.x, current.y + next.y};
            if (upper == 'M') {
                if (!points.empty()) flush_path();
                current = start = next;
                points.push_back(current);
                command = relative ? 'l' : 'L';
                have_quadratic_control = false;
            } else if (upper == 'T') {
                const Pt control = have_quadratic_control ? Pt{2 * current.x - quadratic_control.x,
                                                               2 * current.y - quadratic_control.y} : current;
                append_quadratic(points, current, control, next);
                quadratic_control = control;
                have_quadratic_control = true;
                current = next;
            } else {
                points.push_back(next);
                current = next;
                have_cubic_control = have_quadratic_control = false;
            }
        } else if (upper == 'H' || upper == 'V') {
            if (!has_numbers(1)) { command = 0; continue; }
            const double value = number(0); ++i;
            const Pt next = upper == 'H' ? Pt{relative ? current.x + value : value, current.y}
                                         : Pt{current.x, relative ? current.y + value : value};
            points.push_back(next); current = next;
            have_cubic_control = have_quadratic_control = false;
        } else if (upper == 'C') {
            if (!has_numbers(6)) { command = 0; continue; }
            Pt c1{number(0), number(1)}, c2{number(2), number(3)}, end{number(4), number(5)};
            i += 6;
            if (relative) { c1 = {current.x + c1.x, current.y + c1.y}; c2 = {current.x + c2.x, current.y + c2.y}; end = {current.x + end.x, current.y + end.y}; }
            append_cubic(points, current, c1, c2, end); current = end; cubic_control = c2; have_cubic_control = true; have_quadratic_control = false;
        } else if (upper == 'S') {
            if (!has_numbers(4)) { command = 0; continue; }
            const Pt c1 = have_cubic_control ? Pt{2 * current.x - cubic_control.x, 2 * current.y - cubic_control.y} : current;
            Pt c2{number(0), number(1)}, end{number(2), number(3)}; i += 4;
            if (relative) { c2 = {current.x + c2.x, current.y + c2.y}; end = {current.x + end.x, current.y + end.y}; }
            append_cubic(points, current, c1, c2, end); current = end; cubic_control = c2; have_cubic_control = true; have_quadratic_control = false;
        } else if (upper == 'Q') {
            if (!has_numbers(4)) { command = 0; continue; }
            Pt control{number(0), number(1)}, end{number(2), number(3)}; i += 4;
            if (relative) { control = {current.x + control.x, current.y + control.y}; end = {current.x + end.x, current.y + end.y}; }
            append_quadratic(points, current, control, end); current = end; quadratic_control = control; have_quadratic_control = true; have_cubic_control = false;
        } else if (upper == 'A') {
            if (!has_numbers(7)) { command = 0; continue; }
            const double rx = number(0), ry = number(1), rotation = number(2); const bool large = number(3) != 0, sweep = number(4) != 0;
            Pt end{number(5), number(6)}; i += 7; if (relative) end = {current.x + end.x, current.y + end.y};
            append_arc(points, current, rx, ry, rotation, large, sweep, end); current = end; have_cubic_control = have_quadratic_control = false;
        } else {
            command = 0;
        }
    }
    flush_path();
    return entities;
}

std::vector<double> svg_numbers(const QString& text) {
    std::vector<double> values;
    const std::string s = text.toStdString();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
        if (i >= s.size()) break;
        char* end = nullptr; const double value = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) { ++i; continue; }
        values.push_back(value); i = static_cast<size_t>(end - s.c_str());
    }
    return values;
}

std::vector<Pt> svg_points(const QString& text) {
    const auto values = svg_numbers(text);
    std::vector<Pt> points;
    for (size_t i = 1; i < values.size(); i += 2) points.push_back({values[i - 1], values[i]});
    return points;
}

struct DxfPair { int code = 0; std::string value; };

std::vector<DxfPair> read_dxf_pairs(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Não foi possível abrir o DXF: " + path.string());
    std::vector<DxfPair> pairs;
    std::string code, value;
    while (std::getline(input, code) && std::getline(input, value)) {
        try { pairs.push_back({std::stoi(code), value}); } catch (...) { continue; }
    }
    return pairs;
}

double dxf_value(const std::vector<DxfPair>& record, int code, double fallback = 0.0) {
    for (const auto& pair : record) if (pair.code == code) {
        try { return std::stod(pair.value); } catch (...) { return fallback; }
    }
    return fallback;
}

std::vector<double> dxf_values(const std::vector<DxfPair>& record, int code) {
    std::vector<double> out;
    for (const auto& pair : record) if (pair.code == code) {
        try { out.push_back(std::stod(pair.value)); } catch (...) {}
    }
    return out;
}

std::string dxf_string(const std::vector<DxfPair>& record, int code, const std::string& fallback = "") {
    for (const auto& pair : record) if (pair.code == code) {
        return pair.value;
    }
    return fallback;
}

std::vector<cad::EntityPtr> parse_svg(QXmlStreamReader& xml) {
    std::vector<cad::EntityPtr> entities;
    double height = 0.0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto attrs = xml.attributes();
        const QString name = xml.name().toString().toLower();
        if (name == "svg") {
            height = svg_length(attrs.value("height").toString());
            if (height <= 0.0) {
                const QString viewbox = attrs.value("viewBox").toString().isEmpty()
                                            ? attrs.value("viewbox").toString()
                                            : attrs.value("viewBox").toString();
                const auto view = svg_numbers(viewbox);
                if (view.size() >= 4) height = view[3];
            }
        } else if (name == "path") {
            auto parts = path_entities(attrs.value("d").toString());
            entities.insert(entities.end(), parts.begin(), parts.end());
        } else if (name == "polyline" || name == "polygon") {
            const auto points = svg_points(attrs.value("points").toString());
            if (points.size() >= 2) {
                auto entity = std::make_shared<cad::Polyline>();
                entity->points = points;
                entity->is_closed = name == "polygon";
                entities.push_back(entity);
            }
        } else if (name == "line") {
            auto entity = std::make_shared<cad::Line>();
            entity->x1 = number_or(attrs.value("x1").toString());
            entity->y1 = number_or(attrs.value("y1").toString());
            entity->x2 = number_or(attrs.value("x2").toString());
            entity->y2 = number_or(attrs.value("y2").toString());
            entities.push_back(entity);
        } else if (name == "rect") {
            const double x = number_or(attrs.value("x").toString());
            const double y = number_or(attrs.value("y").toString());
            const double w = number_or(attrs.value("width").toString());
            const double h = number_or(attrs.value("height").toString());
            if (w > 0.0 && h > 0.0) {
                auto entity = std::make_shared<cad::Polyline>();
                entity->points = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
                entity->is_closed = true;
                entities.push_back(entity);
            }
        } else if (name == "circle") {
            auto entity = std::make_shared<cad::Circle>();
            entity->cx = number_or(attrs.value("cx").toString());
            entity->cy = number_or(attrs.value("cy").toString());
            entity->radius = number_or(attrs.value("r").toString());
            entities.push_back(entity);
        } else if (name == "ellipse") {
            auto entity = std::make_shared<cad::Ellipse>();
            entity->cx = number_or(attrs.value("cx").toString());
            entity->cy = number_or(attrs.value("cy").toString());
            entity->rx = number_or(attrs.value("rx").toString());
            entity->ry = number_or(attrs.value("ry").toString());
            entities.push_back(entity);
        }
    }
    if (xml.hasError()) throw std::runtime_error("SVG invalido: " + xml.errorString().toStdString());
    flip_svg_y(entities, height);
    normalize(entities);
    return entities;
}

#ifdef CNC_HAVE_MUPDF
class PdfVectorDevice : public mupdf::FzDevice2 {
public:
    std::vector<cad::EntityPtr> entities;
    std::string current_layer;
    double page_height_pt = 0.0;
    static constexpr double pt_to_mm = 25.4 / 72.0;

    PdfVectorDevice() {
        use_virtual_fill_path(true);
        use_virtual_stroke_path(true);
    }

    const ::fz_path* last_path = nullptr;

    void fill_path(::fz_context* ctx, const ::fz_path* path, int, ::fz_matrix ctm,
                   ::fz_colorspace*, const float*, float,
                   ::fz_color_params) override {
        last_path = path;
        extract_path(ctx, path, ctm);
    }

    void stroke_path(::fz_context* ctx, const ::fz_path* path, const ::fz_stroke_state*,
                     ::fz_matrix ctm, ::fz_colorspace*, const float*, float,
                     ::fz_color_params) override {
        if (path && path == last_path) return;
        last_path = path;
        extract_path(ctx, path, ctm);
    }

private:
    struct WalkerState {
        PdfVectorDevice* self = nullptr;
        ::fz_matrix ctm{};
        Points current;
        bool closed = false;

        Pt to_world(float x, float y) const {
            const fz_point p = fz_transform_point_xy(x, y, ctm);
            return {p.x * pt_to_mm, (self->page_height_pt - p.y) * pt_to_mm};
        }

        void flush() {
            if (current.size() >= 2) {
                Points clean;
                for (const auto& pt : current) {
                    if (clean.empty() || std::hypot(clean.back().x - pt.x, clean.back().y - pt.y) > 1e-6) {
                        clean.push_back(pt);
                    }
                }
                if (clean.size() >= 2) {
                    if (clean.size() > 2 &&
                        std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) < 0.02) {
                        clean.pop_back();
                        closed = true;
                    }

                    // Reconhecer círculo
                    if (closed && clean.size() >= 8) {
                        double cx = 0, cy = 0;
                        for (const auto& p : clean) { cx += p.x; cy += p.y; }
                        cx /= clean.size(); cy /= clean.size();
                        double r_sum = 0;
                        for (const auto& p : clean) r_sum += std::hypot(p.x - cx, p.y - cy);
                        const double r = r_sum / clean.size();
                        if (r > 1e-4) {
                            double max_dev = 0;
                            for (const auto& p : clean)
                                max_dev = std::max(max_dev, std::abs(std::hypot(p.x - cx, p.y - cy) - r));
                            if (max_dev <= std::max(0.04, r * 0.015)) {
                                auto circle = std::make_shared<cad::Circle>();
                                circle->cx = cx; circle->cy = cy; circle->radius = r;
                                circle->layer = self->current_layer;
                                self->entities.push_back(circle);
                                current.clear();
                                closed = false;
                                return;
                            }
                        }
                    }

                    // Reconhecer retângulo alinhado
                    auto check_pts = clean;
                    if (check_pts.size() > 1 &&
                        std::hypot(check_pts.front().x - check_pts.back().x,
                                   check_pts.front().y - check_pts.back().y) < 1e-6) {
                        check_pts.pop_back();
                    }
                    if (closed && check_pts.size() == 4) {
                        std::vector<double> xs, ys;
                        for (const auto& p : check_pts) { xs.push_back(p.x); ys.push_back(p.y); }
                        std::sort(xs.begin(), xs.end());
                        std::sort(ys.begin(), ys.end());
                        if (std::abs(xs[0] - xs[1]) < 0.02 && std::abs(xs[2] - xs[3]) < 0.02 &&
                            std::abs(ys[0] - ys[1]) < 0.02 && std::abs(ys[2] - ys[3]) < 0.02 &&
                            (xs[2] - xs[0]) > 0.01 && (ys[2] - ys[0]) > 0.01) {
                            auto rect = std::make_shared<cad::Rectangle>();
                            rect->x = xs[0]; rect->y = ys[0];
                            rect->width = xs[2] - xs[0]; rect->height = ys[2] - ys[0];
                            rect->layer = self->current_layer;
                            self->entities.push_back(rect);
                            current.clear();
                            closed = false;
                            return;
                        }
                    }

                    auto poly = std::make_shared<cad::Polyline>();
                    poly->points = clean;
                    poly->is_closed = closed;
                    poly->layer = self->current_layer;
                    self->entities.push_back(poly);
                }
            }
            current.clear();
            closed = false;
        }
    };

    void extract_path(::fz_context* ctx, const ::fz_path* path, ::fz_matrix ctm) {
        if (!path) return;
        WalkerState state{this, ctm, {}, false};
        ::fz_path_walker walker{};
        walker.moveto = [](fz_context*, void* arg, float x, float y) {
            auto* s = static_cast<WalkerState*>(arg);
            s->flush();
            s->current.push_back(s->to_world(x, y));
        };
        walker.lineto = [](fz_context*, void* arg, float x, float y) {
            auto* s = static_cast<WalkerState*>(arg);
            s->current.push_back(s->to_world(x, y));
        };
        walker.curveto = [](fz_context*, void* arg, float x1, float y1, float x2, float y2, float x3, float y3) {
            auto* s = static_cast<WalkerState*>(arg);
            if (s->current.empty()) return;
            const Pt p0 = s->current.back();
            const Pt p1 = s->to_world(x1, y1);
            const Pt p2 = s->to_world(x2, y2);
            const Pt p3 = s->to_world(x3, y3);
            append_cubic(s->current, p0, p1, p2, p3);
        };
        walker.closepath = [](fz_context*, void* arg) {
            auto* s = static_cast<WalkerState*>(arg);
            s->closed = true;
            s->flush();
        };
        walker.rectto = [](fz_context*, void* arg, float x1, float y1, float x2, float y2) {
            auto* s = static_cast<WalkerState*>(arg);
            s->flush();
            s->current = {s->to_world(x1, y1), s->to_world(x2, y1), s->to_world(x2, y2), s->to_world(x1, y2)};
            s->closed = true;
            s->flush();
        };
        walker.quadto = [](fz_context*, void* arg, float x1, float y1, float x2, float y2) {
            auto* s = static_cast<WalkerState*>(arg);
            if (s->current.empty()) return;
            const Pt p0 = s->current.back();
            const Pt p1 = s->to_world(x1, y1);
            const Pt p2 = s->to_world(x2, y2);
            append_quadratic(s->current, p0, p1, p2);
        };
        fz_walk_path(ctx, path, &walker, &state);
        state.flush();
    }
};

std::vector<cad::EntityPtr> import_pdf_native(const std::filesystem::path& path) {
    const auto utf8_path = path.u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8_path.c_str()), utf8_path.size());
    mupdf::FzDocument document(filename.c_str());
    const int page_count = document.fz_count_pages();
    if (page_count <= 0) throw std::runtime_error("O PDF nao tem paginas.");

    std::vector<cad::EntityPtr> result;
    for (int page_number = 0; page_number < page_count; ++page_number) {
        mupdf::FzPage page(document, page_number);
        const mupdf::FzRect bounds = page.fz_bound_page();
        PdfVectorDevice dev;
        dev.page_height_pt = bounds.y1;
        dev.current_layer = "PDF p" + std::to_string(page_number + 1);
        mupdf::FzCookie cookie;
        const ::fz_matrix identity{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        page.fz_run_page(dev, identity, cookie);
        result.insert(result.end(), dev.entities.begin(), dev.entities.end());
    }
    normalize(result);
    return result;
}
#endif

}  // namespace

bool is_supported(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(kSupportedSuffixes.begin(), kSupportedSuffixes.end(), ext) != kSupportedSuffixes.end();
}

std::vector<cad::EntityPtr> import_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".svg") return import_svg(path);
    if (ext == ".dxf") return import_dxf(path);
    if (ext == ".pdf") {
#ifdef CNC_HAVE_MUPDF
        return import_pdf_native(path);
#else
        throw std::runtime_error("Importacao PDF indisponivel: MuPDF nao foi encontrado neste build.");
#endif
    }
    throw std::runtime_error("Formato não suportado: " + ext);
}

std::vector<cad::EntityPtr> import_svg(const std::filesystem::path& path) {
    QFile input(QString::fromStdWString(path.wstring()));
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("Não foi possível abrir o SVG: " + path.string());
    QXmlStreamReader xml(&input);
    std::vector<cad::EntityPtr> entities;
    double height = 0.0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto attrs = xml.attributes();
        const QString name = xml.name().toString().toLower();
        if (name == "svg") {
            height = svg_length(attrs.value("height").toString());
            if (height <= 0.0) {
                const QString viewbox = attrs.value("viewBox").toString().isEmpty()
                                            ? attrs.value("viewbox").toString()
                                            : attrs.value("viewBox").toString();
                const auto view = svg_numbers(viewbox);
                if (view.size() >= 4) height = view[3];
            }
        } else if (name == "path") {
            auto parts = path_entities(attrs.value("d").toString());
            entities.insert(entities.end(), parts.begin(), parts.end());
        } else if (name == "polyline" || name == "polygon") {
            const auto points = svg_points(attrs.value("points").toString());
            if (points.size() >= 2) {
                auto entity = std::make_shared<cad::Polyline>();
                entity->points = points;
                entity->is_closed = name == "polygon";
                entities.push_back(entity);
            }
        } else if (name == "line") {
            auto entity = std::make_shared<cad::Line>();
            entity->x1 = number_or(attrs.value("x1").toString()); entity->y1 = number_or(attrs.value("y1").toString());
            entity->x2 = number_or(attrs.value("x2").toString()); entity->y2 = number_or(attrs.value("y2").toString());
            entities.push_back(entity);
        } else if (name == "rect") {
            const double x = number_or(attrs.value("x").toString()), y = number_or(attrs.value("y").toString());
            const double w = number_or(attrs.value("width").toString()), h = number_or(attrs.value("height").toString());
            if (w > 0.0 && h > 0.0) {
                auto entity = std::make_shared<cad::Polyline>();
                entity->points = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
                entity->is_closed = true; entities.push_back(entity);
            }
        } else if (name == "circle") {
            auto entity = std::make_shared<cad::Circle>();
            entity->cx = number_or(attrs.value("cx").toString()); entity->cy = number_or(attrs.value("cy").toString());
            entity->radius = number_or(attrs.value("r").toString()); entities.push_back(entity);
        } else if (name == "ellipse") {
            auto entity = std::make_shared<cad::Ellipse>();
            entity->cx = number_or(attrs.value("cx").toString()); entity->cy = number_or(attrs.value("cy").toString());
            entity->rx = number_or(attrs.value("rx").toString()); entity->ry = number_or(attrs.value("ry").toString());
            entities.push_back(entity);
        }
    }
    if (xml.hasError()) throw std::runtime_error("SVG inválido: " + xml.errorString().toStdString());
    flip_svg_y(entities, height);
    normalize(entities);
    return entities;
}

std::vector<cad::EntityPtr> import_dxf(const std::filesystem::path& path) {
    const auto pairs = read_dxf_pairs(path);
    std::vector<cad::EntityPtr> entities;
    bool in_entities = false;
    for (size_t i = 0; i < pairs.size();) {
        if (pairs[i].code != 0) { ++i; continue; }
        const std::string type = pairs[i].value;
        if (type == "SECTION") {
            in_entities = i + 1 < pairs.size() && pairs[i + 1].code == 2 && pairs[i + 1].value == "ENTITIES";
            i += 2; continue;
        }
        if (type == "ENDSEC") { in_entities = false; ++i; continue; }
        if (!in_entities) { ++i; continue; }
        size_t end = i + 1;
        while (end < pairs.size() && pairs[end].code != 0) ++end;
        std::vector<DxfPair> record(pairs.begin() + static_cast<long>(i + 1), pairs.begin() + static_cast<long>(end));
        cad::EntityPtr entity;
        if (type == "LINE") {
            auto e = std::make_shared<cad::Line>(); e->x1 = dxf_value(record, 10); e->y1 = dxf_value(record, 20);
            e->x2 = dxf_value(record, 11); e->y2 = dxf_value(record, 21); entity = e;
        } else if (type == "CIRCLE") {
            auto e = std::make_shared<cad::Circle>(); e->cx = dxf_value(record, 10); e->cy = dxf_value(record, 20); e->radius = dxf_value(record, 40); entity = e;
        } else if (type == "ARC") {
            auto e = std::make_shared<cad::Arc>(); e->cx = dxf_value(record, 10); e->cy = dxf_value(record, 20); e->radius = dxf_value(record, 40); e->start_angle = dxf_value(record, 50); e->end_angle = dxf_value(record, 51); entity = e;
        } else if (type == "ELLIPSE") {
            auto e = std::make_shared<cad::Ellipse>(); e->cx = dxf_value(record, 10); e->cy = dxf_value(record, 20);
            const double mx = dxf_value(record, 11), my = dxf_value(record, 21); e->rx = std::hypot(mx, my); e->ry = e->rx * dxf_value(record, 40, 1.0);
            e->rotation = std::atan2(my, mx) * 180.0 / M_PI; entity = e;
        } else if (type == "LWPOLYLINE") {
            const auto xs = dxf_values(record, 10), ys = dxf_values(record, 20);
            if (xs.size() >= 2) {
                auto e = std::make_shared<cad::Polyline>();
                for (size_t n = 0; n < xs.size(); ++n) e->points.push_back({xs[n], n < ys.size() ? ys[n] : 0.0});
                e->is_closed = (static_cast<int>(dxf_value(record, 70)) & 1) != 0; entity = e;
            }
        } else if (type == "POLYLINE") {
            auto e = std::make_shared<cad::Polyline>();
            size_t cursor = end;
            while (cursor < pairs.size() && pairs[cursor].code == 0 && pairs[cursor].value == "VERTEX") {
                size_t vertex_end = cursor + 1;
                while (vertex_end < pairs.size() && pairs[vertex_end].code != 0) ++vertex_end;
                const std::vector<DxfPair> vertex(pairs.begin() + static_cast<long>(cursor + 1),
                                                  pairs.begin() + static_cast<long>(vertex_end));
                e->points.push_back({dxf_value(vertex, 10), dxf_value(vertex, 20)});
                cursor = vertex_end;
            }
            e->is_closed = (static_cast<int>(dxf_value(record, 70)) & 1) != 0;
            if (e->points.size() >= 2) entity = e;
            if (cursor < pairs.size() && pairs[cursor].code == 0 && pairs[cursor].value == "SEQEND") {
                size_t seq_end = cursor + 1;
                while (seq_end < pairs.size() && pairs[seq_end].code != 0) ++seq_end;
                end = seq_end;
            } else {
                end = cursor;
            }
        } else if (type == "SPLINE") {
            const auto xs = dxf_values(record, 10), ys = dxf_values(record, 20);
            if (xs.size() >= 2) {
                auto e = std::make_shared<cad::Polyline>();
                for (size_t n = 0; n < xs.size(); ++n)
                    e->points.push_back({xs[n], n < ys.size() ? ys[n] : 0.0});
                e->is_closed = (static_cast<int>(dxf_value(record, 70)) & 1) != 0;
                entity = e;
            }
        }
        if (entity) {
            const std::string lay = dxf_string(record, 8);
            if (!lay.empty()) entity->layer = lay;
            entities.push_back(entity);
        }
        i = end;
    }
    normalize(entities);
    return entities;
}

BatchImportResult import_folder(const std::filesystem::path& folder, bool recursive) {
    BatchImportResult result;
    std::vector<std::filesystem::path> files;
    const auto collect = [&](const auto& begin, const auto& end) {
        for (auto it = begin; it != end; ++it) {
            if (it->is_regular_file() && is_supported(it->path())) files.push_back(it->path());
        }
    };
    if (recursive) {
        collect(std::filesystem::recursive_directory_iterator(folder),
                std::filesystem::recursive_directory_iterator());
    } else {
        collect(std::filesystem::directory_iterator(folder), std::filesystem::directory_iterator());
    }
    std::sort(files.begin(), files.end());
    result.total_files = files.size();

    for (const auto& path : files) {
        try {
            auto entities = import_file(path);
            if (entities.empty()) continue;
            std::string layer = std::filesystem::relative(path, folder).replace_extension().generic_string();
            for (const auto& entity : entities) entity->layer = layer;
            result.entities_by_file[layer] = std::move(entities);
            ++result.imported;
        } catch (const std::exception& error) {
            result.failed.push_back(path.filename().string() + ": " + error.what());
        }
    }
    return result;
}

bool export_svg(const std::filesystem::path& path, const cnc::cad::Document& document) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    const auto& material = document.material;
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    output << std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{:.6f}mm\" "
                          "height=\"{:.6f}mm\" viewBox=\"0 0 {:.6f} {:.6f}\">\n",
                          material.width, material.height, material.width, material.height);
    for (const auto& entity : document.entities) {
        const Points points = entity->to_points(96);
        if (points.size() < 2) continue;
        output << "  <path d=\"";
        for (size_t i = 0; i < points.size(); ++i) {
            const char command = i == 0 ? 'M' : 'L';
            output << std::format("{} {:.6f},{:.6f} ", command, points[i].x,
                                  material.height - points[i].y);
        }
        if (entity->closed()) output << "Z ";
        output << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.2\"/>\n";
    }
    output << "</svg>\n";
    return static_cast<bool>(output);
}

std::shared_ptr<cad::Text> create_filename_label(const std::string& filename,
                                                 const std::vector<cad::EntityPtr>& entities,
                                                 const std::string& layer) {
    if (filename.empty() || entities.empty()) return nullptr;

    BBox bounds{};
    bool first = true;
    for (const auto& entity : entities) {
        if (!entity) continue;
        const BBox b = entity->bounding_box();
        if (first) {
            bounds = b;
            first = false;
        } else {
            bounds.x0 = std::min(bounds.x0, b.x0);
            bounds.y0 = std::min(bounds.y0, b.y0);
            bounds.x1 = std::max(bounds.x1, b.x1);
            bounds.y1 = std::max(bounds.y1, b.y1);
        }
    }
    if (first) return nullptr;

    auto text = std::make_shared<cad::Text>();
    text->content = filename;
    text->font_family = "Arial";
    text->align = "center";
    text->height = 20.0;
    text->layer = layer;
    text->color = {0.0, 0.0, 0.0};

    BBox tb = text->bounding_box();
    const double current_w = tb.x1 - tb.x0;
    if (current_w > 1e-4) {
        const double target_w = 200.0;
        text->height *= (target_w / current_w);
        text->invalidate();
        tb = text->bounding_box();
    }

    const double center_x = (bounds.x0 + bounds.x1) / 2.0;
    const double gap = 10.0;
    text->x = center_x;
    text->y = (bounds.y0 - gap) - tb.y1;
    text->invalidate();

    return text;
}

}  // namespace cnc::io
