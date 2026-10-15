// Implementacao das entidades. Porte de src/cad/entities.py: a ordem das contas
// em ponto flutuante e a mesma do Python, de proposito.
#include "cad/entities.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>

namespace cnc::cad {

Pt rot_pt(double x, double y, double cx, double cy, double a) {
    const double dx = x - cx, dy = y - cy;
    const double c = std::cos(a), s = std::sin(a);
    return {cx + dx * c - dy * s, cy + dx * s + dy * c};
}

double seg_dist(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double ll = dx * dx + dy * dy;
    if (ll < 1e-12) return hypot(px - ax, py - ay);
    const double t = std::max(0.0, std::min(1.0, ((px - ax) * dx + (py - ay) * dy) / ll));
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// ======================================================================
// Base
// ======================================================================

BBox Entity::bounding_box() const {
    const Points pts = to_points();
    if (pts.empty()) return {0.0, 0.0, 0.0, 0.0};
    BBox b{pts[0].x, pts[0].y, pts[0].x, pts[0].y};
    for (const Pt& p : pts) {
        b.x0 = std::min(b.x0, p.x);
        b.y0 = std::min(b.y0, p.y);
        b.x1 = std::max(b.x1, p.x);
        b.y1 = std::max(b.y1, p.y);
    }
    return b;
}

Pt Entity::center() const {
    const BBox b = bounding_box();
    return {(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2};
}

EntityPtr Entity::clone() const {
    EntityPtr c = copy();
    c->id = new_id();
    c->selected = false;
    return c;
}

double Entity::distance_to(double x, double y) const {
    const Points pts = to_points(96);
    if (pts.size() < 2) {
        if (!pts.empty()) return hypot(pts[0].x - x, pts[0].y - y);
        return std::numeric_limits<double>::infinity();
    }
    double best = std::numeric_limits<double>::infinity();
    const size_t n = pts.size();
    const size_t count = closed() ? n : n - 1;
    for (size_t i = 0; i < count; ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[(i + 1) % n];
        best = std::min(best, seg_dist(x, y, a.x, a.y, b.x, b.y));
    }
    return best;
}

// ======================================================================
// Linha
// ======================================================================

Points Line::to_points(int) const { return {{x1, y1}, {x2, y2}}; }

void Line::translate(double dx, double dy) {
    x1 += dx;
    y1 += dy;
    x2 += dx;
    y2 += dy;
    touch();
}

void Line::rotate(double deg, double cx, double cy) {
    const double a = radians(deg);
    const Pt p1 = rot_pt(x1, y1, cx, cy, a);
    const Pt p2 = rot_pt(x2, y2, cx, cy, a);
    x1 = p1.x;
    y1 = p1.y;
    x2 = p2.x;
    y2 = p2.y;
    touch();
}

void Line::scale(double sx, double sy, double cx, double cy) {
    x1 = cx + (x1 - cx) * sx;
    y1 = cy + (y1 - cy) * sy;
    x2 = cx + (x2 - cx) * sx;
    y2 = cy + (y2 - cy) * sy;
    touch();
}

double Line::length() const { return hypot(x2 - x1, y2 - y1); }
double Line::angle() const { return degrees(std::atan2(y2 - y1, x2 - x1)); }

// ======================================================================
// Ponto de furacao
// ======================================================================

Points DrillPoint::to_points(int) const { return {{x, y}}; }

void DrillPoint::translate(double dx, double dy) {
    x += dx;
    y += dy;
    touch();
}

void DrillPoint::rotate(double deg, double cx, double cy) {
    const Pt p = rot_pt(x, y, cx, cy, radians(deg));
    x = p.x;
    y = p.y;
    touch();
}

void DrillPoint::scale(double sx, double sy, double cx, double cy) {
    x = cx + (x - cx) * sx;
    y = cy + (y - cy) * sy;
    touch();
}

// ======================================================================
// Circulo
// ======================================================================

Points Circle::to_points(int seg) const {
    Points pts;
    pts.reserve(static_cast<size_t>(seg));
    for (int i = 0; i < seg; ++i) {
        pts.push_back({cx + radius * std::cos(2 * kPi * i / seg),
                       cy + radius * std::sin(2 * kPi * i / seg)});
    }
    return pts;
}

BBox Circle::bounding_box() const {
    const double r = radius;
    return {cx - r, cy - r, cx + r, cy + r};
}

void Circle::translate(double dx, double dy) {
    cx += dx;
    cy += dy;
    touch();
}

void Circle::rotate(double deg, double ox, double oy) {
    const Pt p = rot_pt(cx, cy, ox, oy, radians(deg));
    cx = p.x;
    cy = p.y;
    touch();
}

void Circle::scale(double sx, double sy, double ox, double oy) {
    cx = ox + (cx - ox) * sx;
    cy = oy + (cy - oy) * sy;
    radius *= std::sqrt(std::fabs(sx * sy));
    touch();
}

// ======================================================================
// Elipse
// ======================================================================

Points Ellipse::to_points(int seg) const {
    const double a = radians(rotation);
    const double ca = std::cos(a), sa = std::sin(a);
    Points pts;
    pts.reserve(static_cast<size_t>(seg));
    for (int i = 0; i < seg; ++i) {
        const double t = 2 * kPi * i / seg;
        const double ex = rx * std::cos(t), ey = ry * std::sin(t);
        pts.push_back({cx + ex * ca - ey * sa, cy + ex * sa + ey * ca});
    }
    return pts;
}

BBox Ellipse::bounding_box() const {
    if (std::fabs(rotation) < 1e-9) {
        return {cx - rx, cy - ry, cx + rx, cy + ry};
    }
    const double a = radians(rotation);
    const double ca = std::cos(a), sa = std::sin(a);
    const double ext_x = std::sqrt((rx * ca) * (rx * ca) + (ry * sa) * (ry * sa));
    const double ext_y = std::sqrt((rx * sa) * (rx * sa) + (ry * ca) * (ry * ca));
    return {cx - ext_x, cy - ext_y, cx + ext_x, cy + ext_y};
}

void Ellipse::translate(double dx, double dy) {
    cx += dx;
    cy += dy;
    touch();
}

void Ellipse::rotate(double deg, double ox, double oy) {
    const Pt p = rot_pt(cx, cy, ox, oy, radians(deg));
    cx = p.x;
    cy = p.y;
    rotation += deg;
    touch();
}

void Ellipse::scale(double sx, double sy, double ox, double oy) {
    cx = ox + (cx - ox) * sx;
    cy = oy + (cy - oy) * sy;
    if (std::fabs(rotation) < 1e-9) {
        rx *= std::fabs(sx);
        ry *= std::fabs(sy);
    } else {
        const double f = std::sqrt(std::fabs(sx * sy));
        rx *= f;
        ry *= f;
    }
    touch();
}

// ======================================================================
// Arco
// ======================================================================

Points Arc::to_points(int seg) const {
    const double a0 = radians(start_angle);
    double a1 = radians(end_angle);
    if (a1 <= a0) a1 += 2 * kPi;
    const int n = std::max(2, static_cast<int>(seg * (a1 - a0) / (2 * kPi)) + 1);
    Points pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        pts.push_back({cx + radius * std::cos(a0 + (a1 - a0) * i / n),
                       cy + radius * std::sin(a0 + (a1 - a0) * i / n)});
    }
    return pts;
}

BBox Arc::bounding_box() const {
    const double a0 = radians(start_angle);
    double a1 = radians(end_angle);
    if (a1 <= a0) a1 += 2 * kPi;

    const double p0x = cx + radius * std::cos(a0);
    const double p0y = cy + radius * std::sin(a0);
    const double p1x = cx + radius * std::cos(a1);
    const double p1y = cy + radius * std::sin(a1);

    double min_x = std::min(p0x, p1x);
    double max_x = std::max(p0x, p1x);
    double min_y = std::min(p0y, p1y);
    double max_y = std::max(p0y, p1y);

    const double span = a1 - a0;
    auto contains_angle = [&](double theta) {
        double d = std::fmod(theta - a0, 2 * kPi);
        if (d < 0.0) d += 2 * kPi;
        return d <= span;
    };

    if (contains_angle(0.0))          max_x = cx + radius;
    if (contains_angle(kPi * 0.5))    max_y = cy + radius;
    if (contains_angle(kPi))          min_x = cx - radius;
    if (contains_angle(kPi * 1.5))    min_y = cy - radius;

    return {min_x, min_y, max_x, max_y};
}

void Arc::translate(double dx, double dy) {
    cx += dx;
    cy += dy;
    touch();
}

void Arc::rotate(double deg, double ox, double oy) {
    const Pt p = rot_pt(cx, cy, ox, oy, radians(deg));
    cx = p.x;
    cy = p.y;
    start_angle += deg;
    end_angle += deg;
    touch();
}

void Arc::scale(double sx, double sy, double ox, double oy) {
    cx = ox + (cx - ox) * sx;
    cy = oy + (cy - oy) * sy;
    radius *= std::sqrt(std::fabs(sx * sy));
    if (sx < 0) {
        const double s = start_angle, e = end_angle;
        start_angle = 180 - e;
        end_angle = 180 - s;
    }
    if (sy < 0) {
        const double s = start_angle, e = end_angle;
        start_angle = -e;
        end_angle = -s;
    }
    touch();
}

// ======================================================================
// Retangulo
// ======================================================================

Points Rectangle::to_points(int seg) const {
    const double x1 = x, y1 = y;
    const double x2 = x + width, y2 = y + height;
    const double r = std::min({corner_radius, std::fabs(width) / 2, std::fabs(height) / 2});

    Points pts;
    if (r <= 1e-9) {
        pts = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
    } else {
        const int n = std::max(3, seg / 8);
        struct Corner {
            double ccx, ccy, a0;
        };
        const Corner corners[4] = {
            {x2 - r, y1 + r, -90.0},
            {x2 - r, y2 - r, 0.0},
            {x1 + r, y2 - r, 90.0},
            {x1 + r, y1 + r, 180.0},
        };
        for (const Corner& c : corners) {
            for (int i = 0; i <= n; ++i) {
                const double a = radians(c.a0 + 90.0 * i / n);
                pts.push_back({c.ccx + r * std::cos(a), c.ccy + r * std::sin(a)});
            }
        }
    }

    if (std::fabs(rotation) > 1e-9) {
        const double cx = x + width / 2, cy = y + height / 2;
        const double a = radians(rotation);
        for (Pt& p : pts) p = rot_pt(p.x, p.y, cx, cy, a);
    }
    return pts;
}

BBox Rectangle::bounding_box() const {
    if (std::fabs(rotation) < 1e-9) {
        const double x1 = x, x2 = x + width;
        const double y1 = y, y2 = y + height;
        return {std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)};
    }
    const double cx = x + width / 2.0;
    const double cy = y + height / 2.0;
    const double a = radians(rotation);
    const double c = std::cos(a), s = std::sin(a);
    const double hw = width / 2.0, hh = height / 2.0;
    const double ext_x = std::fabs(hw * c) + std::fabs(hh * s);
    const double ext_y = std::fabs(hw * s) + std::fabs(hh * c);
    return {cx - ext_x, cy - ext_y, cx + ext_x, cy + ext_y};
}

void Rectangle::translate(double dx, double dy) {
    x += dx;
    y += dy;
    touch();
}

void Rectangle::rotate(double deg, double cx, double cy) {
    const double mx = x + width / 2, my = y + height / 2;
    const Pt n = rot_pt(mx, my, cx, cy, radians(deg));
    x += n.x - mx;
    y += n.y - my;
    rotation += deg;
    touch();
}

void Rectangle::scale(double sx, double sy, double cx, double cy) {
    const double x2 = cx + (x + width - cx) * sx;
    const double y2 = cy + (y + height - cy) * sy;
    x = cx + (x - cx) * sx;
    y = cy + (y - cy) * sy;
    width = x2 - x;
    height = y2 - y;
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }
    corner_radius *= std::sqrt(std::fabs(sx * sy));
    touch();
}

// ======================================================================
// Polilinha
// ======================================================================

BBox Polyline::bounding_box() const {
    if (points.empty()) return {0.0, 0.0, 0.0, 0.0};
    BBox b{points[0].x, points[0].y, points[0].x, points[0].y};
    for (size_t i = 1; i < points.size(); ++i) {
        const Pt& p = points[i];
        b.x0 = std::min(b.x0, p.x);
        b.y0 = std::min(b.y0, p.y);
        b.x1 = std::max(b.x1, p.x);
        b.y1 = std::max(b.y1, p.y);
    }
    return b;
}

void Polyline::translate(double dx, double dy) {
    for (Pt& p : points) {
        p.x += dx;
        p.y += dy;
    }
    touch();
}

void Polyline::rotate(double deg, double cx, double cy) {
    const double a = radians(deg);
    for (Pt& p : points) p = rot_pt(p.x, p.y, cx, cy, a);
    touch();
}

void Polyline::scale(double sx, double sy, double cx, double cy) {
    for (Pt& p : points) {
        p = {cx + (p.x - cx) * sx, cy + (p.y - cy) * sy};
    }
    touch();
}

// ======================================================================
// Texto
// ======================================================================

const std::vector<Points>& Text::outline_polygons() const {
    if (cached_) return *cached_;

    QFont font(QString::fromStdString(font_family));
    font.setPixelSize(1000);
    font.setBold(bold);
    font.setItalic(italic);

    QPainterPath path;
    path.addText(0, 0, font, QString::fromStdString(content));

    const QFontMetricsF fm(font);
    const double scale_f = height / std::max(fm.ascent(), 1.0);

    std::vector<Points> polys;
    const QList<QPolygonF> subpaths = path.toSubpathPolygons();
    for (const QPolygonF& poly : subpaths) {
        Points pts;
        pts.reserve(static_cast<size_t>(poly.size()));
        for (const QPointF& p : poly) pts.push_back({p.x() * scale_f, -p.y() * scale_f});
        if (pts.size() >= 2) polys.push_back(std::move(pts));
    }

    if (!polys.empty()) {
        double min_x = std::numeric_limits<double>::infinity();
        double max_x = -std::numeric_limits<double>::infinity();
        for (const Points& poly : polys) {
            for (const Pt& p : poly) {
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
            }
        }
        const double w = max_x - min_x;
        const double off = align == "left" ? 0.0 : (align == "center" ? -w / 2 : -w);
        const double a = radians(rotation);
        const double ca = std::cos(a), sa = std::sin(a);
        for (Points& poly : polys) {
            for (Pt& p : poly) {
                const double px = p.x, py = p.y;
                p = {x + (px + off) * ca - py * sa, y + (px + off) * sa + py * ca};
            }
        }
    }

    cached_ = std::move(polys);
    return *cached_;
}

Points Text::to_points(int) const {
    Points out;
    for (const Points& poly : outline_polygons()) out.insert(out.end(), poly.begin(), poly.end());
    return out;
}

BBox Text::bounding_box() const {
    const auto& polys = outline_polygons();
    if (polys.empty()) return {x, y, x, y};
    double x0 = std::numeric_limits<double>::infinity();
    double y0 = std::numeric_limits<double>::infinity();
    double x1 = -std::numeric_limits<double>::infinity();
    double y1 = -std::numeric_limits<double>::infinity();
    for (const auto& poly : polys) {
        for (const auto& p : poly) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
    }
    if (std::isinf(x0)) return {x, y, x, y};
    return {x0, y0, x1, y1};
}

void Text::translate(double dx, double dy) {
    x += dx;
    y += dy;
    // Moving labels does not change the font or the glyphs. Reuse the existing
    // outlines instead of asking Qt to tessellate every letter on each frame.
    if (cached_) {
        for (auto& polygon : *cached_)
            for (auto& point : polygon) {
                point.x += dx;
                point.y += dy;
            }
    }
    touch();
}

void Text::rotate(double deg, double cx, double cy) {
    const Pt p = rot_pt(x, y, cx, cy, radians(deg));
    x = p.x;
    y = p.y;
    rotation += deg;
    invalidate();
}

void Text::scale(double sx, double sy, double cx, double cy) {
    x = cx + (x - cx) * sx;
    y = cy + (y - cy) * sy;
    height *= std::sqrt(std::fabs(sx * sy));
    invalidate();
}

// ======================================================================
// Cota / Dimensao
// ======================================================================

namespace {

// Varredura angular de a1 para a2, normalizada em (-pi, pi], como no Python:
// (a2 - a1 + pi) % (2 pi) - pi, com o resto de sinal do divisor.
double sweep_between(double a1, double a2) {
    return fmod_py(a2 - a1 + kPi, 2 * kPi) - kPi;
}

// f"{value:.{digits}f}" seguido de rstrip("0").rstrip(".") quando ha ponto.
std::string format_number(double value, int digits) {
    digits = std::max(0, std::min(6, digits));
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.*f", digits, value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        while (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace

double Dimension::measure() const {
    if (kind_name == "horizontal") return std::fabs(x2 - x1);
    if (kind_name == "vertical") return std::fabs(y2 - y1);
    if (kind_name == "angular") {
        const double ax = x2 - x1, ay = y2 - y1;
        const double bx = x3 - x1, by = y3 - y1;
        const double lengths = hypot(ax, ay) * hypot(bx, by);
        if (lengths < 1e-12) return 0.0;
        const double cosine = std::max(-1.0, std::min(1.0, (ax * bx + ay * by) / lengths));
        return degrees(std::acos(cosine));
    }
    if (kind_name == "diameter") return 2 * hypot(x2 - x1, y2 - y1);
    return hypot(x2 - x1, y2 - y1);
}

std::pair<Pt, Pt> Dimension::dimension_ends() const {
    if (kind_name == "horizontal") return {{x1, y1 + offset}, {x2, y1 + offset}};
    if (kind_name == "vertical") return {{x1 + offset, y1}, {x1 + offset, y2}};
    const double dx = x2 - x1, dy = y2 - y1;
    const double length = hypot(dx, dy);
    if (length < 1e-9) return {{x1, y1}, {x2, y2}};
    const double nx = -dy / length * offset, ny = dx / length * offset;
    return {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}};
}

double Dimension::linear_label_width(const std::string& units, double unit_factor) const {
    const std::string label = format_label(units, unit_factor);
    // len(label) do Python conta caracteres (pontos de codigo), nao bytes UTF-8.
    const QString q = QString::fromStdString(label);
    return static_cast<double>(q.toUcs4().size()) * style.text_height * 0.62;
}

std::vector<std::pair<Pt, Pt>> Dimension::arrow_placements() const {
    if (kind_name == "angular") {
        const double a1 = std::atan2(y2 - y1, x2 - x1);
        const double a2 = std::atan2(y3 - y1, x3 - x1);
        const double sweep = sweep_between(a1, a2);
        const double radius = std::max(std::fabs(offset), 1e-3);
        const Pt start{x1 + radius * std::cos(a1), y1 + radius * std::sin(a1)};
        const double end_angle = a1 + sweep;
        const Pt end{x1 + radius * std::cos(end_angle), y1 + radius * std::sin(end_angle)};
        const double tangent = sweep >= 0 ? 1.0 : -1.0;
        const Pt start_tail{x1 + radius * std::cos(a1 + tangent * 0.05),
                            y1 + radius * std::sin(a1 + tangent * 0.05)};
        const Pt end_tail{x1 + radius * std::cos(end_angle - tangent * 0.05),
                          y1 + radius * std::sin(end_angle - tangent * 0.05)};
        return {{start, start_tail}, {end, end_tail}};
    }
    if (kind_name == "radius") return {{{x2, y2}, {x3, y3}}};
    if (kind_name == "diameter") {
        const Pt opposite{2 * x1 - x2, 2 * y1 - y2};
        const Pt center_pt{x1, y1};
        return {{{x2, y2}, center_pt}, {opposite, center_pt}};
    }

    const auto [a, b] = dimension_ends();
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length = hypot(dx, dy);
    if (length < 1e-9) return {};
    const double ux = dx / length, uy = dy / length;
    const double label_width = linear_label_width();
    const bool arrows_outside =
        style.arrow_fit == "outside" ||
        (style.arrow_fit == "auto" &&
         length < (2 * style.arrow_size + label_width + 2 * style.text_gap));
    if (arrows_outside) {
        return {
            {a, {a.x - ux * style.arrow_size, a.y - uy * style.arrow_size}},
            {b, {b.x + ux * style.arrow_size, b.y + uy * style.arrow_size}},
        };
    }
    return {
        {a, {a.x + ux * style.arrow_size, a.y + uy * style.arrow_size}},
        {b, {b.x - ux * style.arrow_size, b.y - uy * style.arrow_size}},
    };
}

std::vector<Seg> Dimension::segments() const {
    if (kind_name == "angular") {
        const double a1 = std::atan2(y2 - y1, x2 - x1);
        const double a2 = std::atan2(y3 - y1, x3 - x1);
        const double sweep = sweep_between(a1, a2);
        const double r = std::max(std::fabs(offset), 1e-3);
        const int n = std::max(8, static_cast<int>(std::fabs(sweep) / (kPi / 48)));
        Points pts;
        for (int i = 0; i <= n; ++i) {
            pts.push_back({x1 + r * std::cos(a1 + sweep * i / n),
                           y1 + r * std::sin(a1 + sweep * i / n)});
        }
        std::vector<Seg> out = {{{x1, y1}, {x2, y2}}, {{x1, y1}, {x3, y3}}};
        for (size_t i = 0; i + 1 < pts.size(); ++i) out.push_back({pts[i], pts[i + 1]});
        return out;
    }
    if (kind_name == "radius" || kind_name == "diameter") {
        const Pt center_pt{x1, y1};
        const Pt rim{x2, y2};
        const Pt leader{x3, y3};
        if (kind_name == "diameter") {
            const Pt opposite{2 * x1 - x2, 2 * y1 - y2};
            return {{opposite, rim}, {rim, leader}};
        }
        return {{center_pt, rim}, {rim, leader}};
    }

    const auto [a, b] = dimension_ends();
    double dx = a.x - x1, dy = a.y - y1;
    double normal_length = hypot(dx, dy);
    if (normal_length < 1e-9) {
        if (kind_name == "horizontal") {
            dx = 0.0;
            dy = 1.0;
        } else if (kind_name == "vertical") {
            dx = 1.0;
            dy = 0.0;
        } else {
            dx = 0.0;
            dy = 1.0;
        }
        normal_length = 1.0;
    }
    const double nx = dx / normal_length, ny = dy / normal_length;
    const double gap = std::max(0.0, style.extension_gap);
    const double overshoot = std::max(0.0, style.extension_overshoot);
    const Pt first_extension_start{x1 + nx * gap, y1 + ny * gap};
    const Pt first_extension_end{a.x + nx * overshoot, a.y + ny * overshoot};
    const Pt second_extension_start{x2 + nx * gap, y2 + ny * gap};
    const Pt second_extension_end{b.x + nx * overshoot, b.y + ny * overshoot};

    std::vector<Seg> segs = {{first_extension_start, first_extension_end},
                             {second_extension_start, second_extension_end}};
    const double line_length = dist(a, b);
    const double label_half = linear_label_width() / 2 + std::max(0.0, style.text_gap);
    if (line_length > 2 * label_half) {
        const double ux = (b.x - a.x) / line_length, uy = (b.y - a.y) / line_length;
        const Pt midpoint{(a.x + b.x) / 2, (a.y + b.y) / 2};
        const Pt left{midpoint.x - ux * label_half, midpoint.y - uy * label_half};
        const Pt right{midpoint.x + ux * label_half, midpoint.y + uy * label_half};
        segs.push_back({a, left});
        segs.push_back({right, b});
    } else {
        segs.push_back({a, b});
    }
    return segs;
}

Pt Dimension::label_position() const {
    if (kind_name == "angular") {
        const double a1 = std::atan2(y2 - y1, x2 - x1);
        const double a2 = std::atan2(y3 - y1, x3 - x1);
        const double sweep = sweep_between(a1, a2);
        const double mid = a1 + sweep / 2;
        const double r = std::max(std::fabs(offset), 1e-3) + style.text_height * 1.2;
        return {x1 + r * std::cos(mid), y1 + r * std::sin(mid)};
    }
    if (kind_name == "radius" || kind_name == "diameter") return {x3, y3};
    const auto [a, b] = dimension_ends();
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

std::string Dimension::format_label(const std::string& units, double unit_factor) const {
    const double value = kind_name == "angular" ? measure() : measure() / unit_factor;
    const int precision = std::max(0, std::min(6, style.precision));

    const std::string number = format_number(value, precision);
    std::string label;
    if (kind_name == "angular") {
        label = number + "°";
    } else if (kind_name == "diameter") {
        label = "⌀" + number;
    } else if (kind_name == "radius") {
        label = "R" + number;
    } else if (style.show_unit) {
        label = number + " " + (units == "in" ? "pol" : "mm");
    } else {
        label = number;
    }

    if (style.tolerance_mode == "symmetric") {
        const std::string tolerance =
            format_number(std::fabs(style.tolerance_upper) / unit_factor, precision);
        label += " ±" + tolerance;
    } else if (style.tolerance_mode == "deviation") {
        const std::string upper = format_number(style.tolerance_upper / unit_factor, precision);
        const std::string lower =
            format_number(std::fabs(style.tolerance_lower) / unit_factor, precision);
        label += " +" + upper + "/−" + lower;
    }

    if (style.alternate_units && kind_name != "angular") {
        const double alternate_factor = isclose(unit_factor, 25.4) ? 1.0 : 25.4;
        const char* alternate_units = alternate_factor == 1.0 ? "mm" : "pol";
        const std::string alternate_value =
            format_number(measure() / alternate_factor, style.alternate_precision);
        label += " [" + alternate_value + " " + alternate_units + "]";
    }
    return label;
}

Points Dimension::to_points(int) const {
    Points out;
    for (const Seg& s : segments()) {
        out.push_back(s.first);
        out.push_back(s.second);
    }
    return out;
}

double Dimension::distance_to(double x, double y) const {
    double best = std::numeric_limits<double>::infinity();
    for (const Seg& s : segments()) {
        best = std::min(best, seg_dist(x, y, s.first.x, s.first.y, s.second.x, s.second.y));
    }
    return best;
}

void Dimension::translate(double dx, double dy) {
    x1 += dx;
    y1 += dy;
    x2 += dx;
    y2 += dy;
    x3 += dx;
    y3 += dy;
    touch();
}

void Dimension::rotate(double deg, double cx, double cy) {
    const double a = radians(deg);
    Pt p = rot_pt(x1, y1, cx, cy, a);
    x1 = p.x;
    y1 = p.y;
    p = rot_pt(x2, y2, cx, cy, a);
    x2 = p.x;
    y2 = p.y;
    p = rot_pt(x3, y3, cx, cy, a);
    x3 = p.x;
    y3 = p.y;
    touch();
}

void Dimension::scale(double sx, double sy, double cx, double cy) {
    x1 = cx + (x1 - cx) * sx;
    y1 = cy + (y1 - cy) * sy;
    x2 = cx + (x2 - cx) * sx;
    y2 = cy + (y2 - cy) * sy;
    x3 = cx + (x3 - cx) * sx;
    y3 = cy + (y3 - cy) * sy;
    offset *= std::sqrt(std::fabs(sx * sy));
    touch();
}

// ======================================================================
// Helpers de construcao
// ======================================================================

namespace {
void apply_style(Polyline& p, const EntityStyle& style) {
    if (style.layer) p.layer = *style.layer;
    if (style.color) p.color = *style.color;
}
}  // namespace

std::shared_ptr<Polyline> make_polygon(double cx, double cy, double radius, int sides,
                                       double rotation, const EntityStyle& style) {
    const double a0 = radians(rotation) + kPi / 2;
    auto poly = std::make_shared<Polyline>();
    for (int i = 0; i < sides; ++i) {
        poly->points.push_back({cx + radius * std::cos(a0 + 2 * kPi * i / sides),
                                cy + radius * std::sin(a0 + 2 * kPi * i / sides)});
    }
    poly->is_closed = true;
    apply_style(*poly, style);
    return poly;
}

std::shared_ptr<Polyline> make_star(double cx, double cy, double r_out, int points,
                                    double inner_ratio, double rotation,
                                    const EntityStyle& style) {
    const double a0 = radians(rotation) + kPi / 2;
    const double r_in = r_out * inner_ratio;
    auto poly = std::make_shared<Polyline>();
    for (int i = 0; i < points * 2; ++i) {
        const double r = i % 2 == 0 ? r_out : r_in;
        const double a = a0 + kPi * i / points;
        poly->points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    poly->is_closed = true;
    apply_style(*poly, style);
    return poly;
}

}  // namespace cnc::cad
