// Porte das ferramentas de forma de src/tools/draw_tools.py: circulo, elipse, retangulo, poligono,
// estrela, linha/polilinha, ponto de furacao, arco e desenho livre.
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "cad/geometry.hpp"
#include "tools/draw_tools.hpp"

namespace cnc::tools {

namespace {

bool has(Qt::KeyboardModifiers mods, Qt::KeyboardModifier flag) { return mods.testFlag(flag); }
bool shift_or_ctrl(Qt::KeyboardModifiers mods) { return has(mods, Qt::ShiftModifier) || has(mods, Qt::ControlModifier); }

}  // namespace

// ======================================================================
// Base para desenho por arraste
// ======================================================================

void DragTool::deactivate() {
    p0_.reset();
    BaseTool::deactivate();
}

bool DragTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    p0_ = Pt{wx, wy};
    return true;
}

bool DragTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (!p0_) return false;
    const Points pts = ghost(*p0_, {wx, wy}, mods);
    if (!pts.empty()) ctx->preview(pts, ghost_closed());
    return true;
}

bool DragTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!p0_ || button != Qt::LeftButton) return false;
    const Pt p0 = *p0_;
    p0_.reset();
    ctx->clear_preview();
    if (hypot(wx - p0.x, wy - p0.y) < 1e-6) return true;
    const cad::EntityPtr ent = build(p0, {wx, wy}, mods);
    if (ent) {
        ctx->add(ent, label());
        ctx->status(label() + " criado");
    }
    return true;
}

Points DragTool::ghost(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const {
    const cad::EntityPtr ent = build(p0, p1, mods);
    return ent ? ent->to_points(64) : Points{};
}

cad::EntityPtr CircleTool::build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers) const {
    const double r = hypot(p1.x - p0.x, p1.y - p0.y);
    if (!(r > 1e-6)) return nullptr;
    auto c = std::make_shared<cad::Circle>();
    c->cx = p0.x;
    c->cy = p0.y;
    c->radius = r;
    return c;
}

cad::EntityPtr EllipseTool::build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const {
    double rx = std::fabs(p1.x - p0.x) / 2;
    double ry = std::fabs(p1.y - p0.y) / 2;
    if (shift_or_ctrl(mods)) rx = ry = std::max(rx, ry);
    auto e = std::make_shared<cad::Ellipse>();
    if (has(mods, Qt::AltModifier)) {
        if (!(rx > 1e-6 && ry > 1e-6)) return nullptr;
        e->cx = p0.x;
        e->cy = p0.y;
        e->rx = rx * 2;
        e->ry = ry * 2;
        return e;
    }
    e->cx = p0.x + std::copysign(rx, p1.x - p0.x);
    e->cy = p0.y + std::copysign(ry, p1.y - p0.y);
    e->rx = rx;
    e->ry = ry;
    return (rx > 1e-6 && ry > 1e-6) ? cad::EntityPtr(e) : nullptr;
}

cad::EntityPtr RectangleTool::build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const {
    double x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    if (shift_or_ctrl(mods)) {
        const double s = std::max(std::fabs(x1 - x0), std::fabs(y1 - y0));
        x1 = x0 + std::copysign(s, x1 - x0);
        y1 = y0 + std::copysign(s, y1 - y0);
    }
    if (has(mods, Qt::AltModifier)) {
        const double dx = std::fabs(x1 - x0), dy = std::fabs(y1 - y0);
        const double ox = x0, oy = y0;
        x0 = ox - dx;
        x1 = ox + dx;
        y0 = oy - dy;
        y1 = oy + dy;
    }
    const double w = std::fabs(x1 - x0), h = std::fabs(y1 - y0);
    if (w < 1e-6 || h < 1e-6) return nullptr;
    auto r = std::make_shared<cad::Rectangle>();
    r->x = std::min(x0, x1);
    r->y = std::min(y0, y1);
    r->width = w;
    r->height = h;
    r->corner_radius = corner_radius;
    return r;
}

std::string PolygonTool::hint() const { return std::format("Clique no centro e arraste · {} lados", sides); }

cad::EntityPtr PolygonTool::build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers) const {
    const double r = hypot(p1.x - p0.x, p1.y - p0.y);
    if (r < 1e-6) return nullptr;
    const double rot = degrees(std::atan2(p1.y - p0.y, p1.x - p0.x)) - 90;
    return cad::make_polygon(p0.x, p0.y, r, std::max(3, sides), rot);
}

std::string StarTool::hint() const { return std::format("Clique no centro e arraste · {} pontas", points); }

cad::EntityPtr StarTool::build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers) const {
    const double r = hypot(p1.x - p0.x, p1.y - p0.y);
    if (r < 1e-6) return nullptr;
    const double rot = degrees(std::atan2(p1.y - p0.y, p1.x - p0.x)) - 90;
    return cad::make_star(p0.x, p0.y, r, std::max(3, points), inner_ratio, rot);
}

// ======================================================================
// Linha / Polilinha
// ======================================================================

namespace {

Points build_tangent_arc(const Pt& p0, const Pt& p1, const Pt& tangent, int seg_count = 24) {
    const double vx = p1.x - p0.x;
    const double vy = p1.y - p0.y;
    const double v_len_sq = vx * vx + vy * vy;
    if (v_len_sq < 1e-8) return {p0};

    const double t_len = std::hypot(tangent.x, tangent.y);
    if (t_len < 1e-8) return {p0, p1};
    const double tx = tangent.x / t_len;
    const double ty = tangent.y / t_len;
    const double nx = -ty;
    const double ny = tx;

    const double n_dot_v = nx * vx + ny * vy;
    if (std::fabs(n_dot_v) < 1e-6) return {p0, p1};

    const double r = v_len_sq / (2.0 * n_dot_v);
    const double cx = p0.x + r * nx;
    const double cy = p0.y + r * ny;
    const double radius = std::fabs(r);

    const double a0 = std::atan2(p0.y - cy, p0.x - cx);
    const double a1 = std::atan2(p1.y - cy, p1.x - cx);

    double sweep = a1 - a0;
    if (r > 0) {
        while (sweep < 0) sweep += 2.0 * std::numbers::pi;
        while (sweep > 2.0 * std::numbers::pi) sweep -= 2.0 * std::numbers::pi;
    } else {
        while (sweep > 0) sweep -= 2.0 * std::numbers::pi;
        while (sweep < -2.0 * std::numbers::pi) sweep += 2.0 * std::numbers::pi;
    }

    Points arc;
    arc.reserve(seg_count + 1);
    for (int i = 0; i <= seg_count; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(seg_count);
        const double angle = a0 + frac * sweep;
        arc.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
    }
    return arc;
}

}  // namespace

std::string PolylineTool::hint() const {
    if (single_segment()) return "Clique no início e depois no fim da linha";
    return "Clique para adicionar pontos · 'A' alterna Arco Tangente (SolidWorks) · Duplo-clique/Enter finaliza · C fecha";
}

Pt PolylineTool::ortho(const Pt& a, const Pt& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    return std::fabs(dx) >= std::fabs(dy) ? Pt{b.x, a.y} : Pt{a.x, b.y};
}

bool PolylineTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button == Qt::RightButton) {
        finish_shape(false);
        ctx->finish();
        return true;
    }
    if (button != Qt::LeftButton) return false;

    if (has(mods, Qt::ShiftModifier) && !pts_.empty() && !arc_mode_) {
        const Pt o = ortho(pts_.back(), {wx, wy});
        wx = o.x;
        wy = o.y;
    }

    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * kPickPx;
    if (pts_.size() >= 3 && dist(pts_.front(), Pt{wx, wy}) <= tol) {
        finish_shape(true);
        return true;
    }
    if (!pts_.empty() && dist(pts_.back(), Pt{wx, wy}) < 1e-9) return true;

    if (arc_mode_ && has_tangent_ && !pts_.empty()) {
        const Points arc_pts = build_tangent_arc(pts_.back(), {wx, wy}, last_tangent_);
        if (arc_pts.size() >= 2) {
            const Pt p_penult = arc_pts[arc_pts.size() - 2];
            last_tangent_ = {wx - p_penult.x, wy - p_penult.y};
            has_tangent_ = true;
            pts_.insert(pts_.end(), arc_pts.begin() + 1, arc_pts.end());
        } else {
            pts_.push_back({wx, wy});
        }
        // No SolidWorks, ao clicar no fim do arco a ferramenta volta automaticamente para linha reta
        arc_mode_ = false;
        ctx->status("Arco inserido · De volta ao modo Linha (pressione 'A' para novo arco)");
    } else {
        if (!pts_.empty()) {
            last_tangent_ = {wx - pts_.back().x, wy - pts_.back().y};
            has_tangent_ = true;
        }
        pts_.push_back({wx, wy});
        ctx->status(std::format("{} ponto(s)", pts_.size()));
    }

    if (single_segment() && pts_.size() == 2) {
        finish_shape(false);
        return true;
    }
    return true;
}

bool PolylineTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (pts_.empty()) return false;
    if (has(mods, Qt::ShiftModifier) && !arc_mode_) {
        const Pt o = ortho(pts_.back(), {wx, wy});
        wx = o.x;
        wy = o.y;
    }
    cursor_ = {wx, wy};

    // Autotransitioning por mouse: se o cursor retornar ao ultimo vertice e se afastar, alterna arco/linha
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * kPickPx;
    const double d_back = dist(pts_.back(), cursor_);
    if (d_back <= tol * 0.75) {
        returned_to_vertex_ = true;
    } else if (returned_to_vertex_ && d_back > tol * 1.5) {
        returned_to_vertex_ = false;
        arc_mode_ = !arc_mode_;
        ctx->status(arc_mode_ ? "Modo Arco Tangente (SolidWorks) ativado" : "Modo Linha Reta ativado");
    }

    Points preview = pts_;
    if (arc_mode_ && has_tangent_) {
        const Points arc_pts = build_tangent_arc(pts_.back(), cursor_, last_tangent_);
        if (arc_pts.size() >= 2) {
            preview.insert(preview.end(), arc_pts.begin() + 1, arc_pts.end());
        } else {
            preview.push_back(cursor_);
        }
    } else {
        preview.push_back(cursor_);
    }
    ctx->preview(preview, false);
    return true;
}

bool PolylineTool::on_double_click(double, double) {
    finish_shape(false);
    return true;
}

bool PolylineTool::on_key(int key) {
    if (key == Qt::Key_A && !pts_.empty()) {
        arc_mode_ = !arc_mode_;
        ctx->status(arc_mode_ ? "Modo Arco Tangente (SolidWorks) ativado (Pressione 'A' para Linha)"
                              : "Modo Linha Reta ativado (Pressione 'A' para Arco)");
        on_move(cursor_.x, cursor_.y, Qt::NoModifier);
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        finish_shape(false);
        return true;
    }
    if (key == Qt::Key_C || key == Qt::Key_Tab) {
        finish_shape(true);
        return true;
    }
    if (key == Qt::Key_Backspace && !pts_.empty()) {
        pts_.pop_back();
        Points preview = pts_;
        preview.push_back(cursor_);
        ctx->preview(preview, false);
        return true;
    }
    if (key == Qt::Key_Escape) {
        finish_shape(false);
        ctx->finish();
        return true;
    }
    return false;
}

void PolylineTool::deactivate() {
    pts_.clear();
    arc_mode_ = false;
    has_tangent_ = false;
    returned_to_vertex_ = false;
    BaseTool::deactivate();
}

void PolylineTool::finish_shape(bool close) {
    Points pts = std::move(pts_);
    pts_.clear();
    ctx->clear_preview();
    if (pts.size() < 2) return;
    if (close && pts.size() < 3) close = false;
    cad::EntityPtr ent;
    if (pts.size() == 2 && !close) {
        auto line = std::make_shared<cad::Line>();
        line->x1 = pts[0].x;
        line->y1 = pts[0].y;
        line->x2 = pts[1].x;
        line->y2 = pts[1].y;
        ent = line;
    } else {
        auto poly = std::make_shared<cad::Polyline>();
        poly->points = pts;
        poly->is_closed = close;
        ent = poly;
    }
    ctx->add(ent, label());
    ctx->status(std::format("{} criada ({} pontos)", label(), pts.size()));
}

std::string ConstructionLineTool::hint() const {
    return "Clique no início e no fim · Shift mantém horizontal ou vertical · duplo clique depois permite liberar "
           "para CAM";
}

void ConstructionLineTool::finish_shape(bool) {
    Points points = std::move(pts_);
    pts_.clear();
    ctx->clear_preview();
    if (points.size() < 2 || dist(points[0], points[1]) < 1e-9) return;
    auto line = std::make_shared<cad::ConstructionLine>();
    line->x1 = points[0].x;
    line->y1 = points[0].y;
    line->x2 = points[1].x;
    line->y2 = points[1].y;
    ctx->add(line, label());
    ctx->status("Linha de construção criada");
}

bool DrillPointTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    auto point = std::make_shared<cad::DrillPoint>();
    point->x = wx;
    point->y = wy;
    ctx->add(point, label());
    ctx->status(std::format("Ponto de furação em X {:.2f}, Y {:.2f} mm", wx, wy));
    return true;
}

// ======================================================================
// Arco (3 pontos)
// ======================================================================

bool ArcTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    pts_.push_back({wx, wy});
    if (pts_.size() == 3) build();
    return true;
}

bool ArcTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (pts_.size() == 1) {
        ctx->preview({pts_[0], {wx, wy}}, false);
        return true;
    }
    if (pts_.size() == 2) {
        if (const auto arc = arc_through(pts_[0], pts_[1], {wx, wy})) ctx->preview(arc->to_points(96), false);
        return true;
    }
    return false;
}

void ArcTool::deactivate() {
    pts_.clear();
    BaseTool::deactivate();
}

void ArcTool::build() {
    const Pt a = pts_[0], b = pts_[1], c = pts_[2];
    pts_.clear();
    ctx->clear_preview();
    if (const auto arc = arc_through(a, b, c)) {
        ctx->add(arc, label());
        ctx->status("Arco criado");
    } else {
        ctx->status("Pontos colineares — arco não criado");
    }
}

std::shared_ptr<cad::Arc> ArcTool::arc_through(const Pt& p1, const Pt& p2, const Pt& p3) {
    const double ax = p1.x, ay = p1.y;
    const double bx = p3.x, by = p3.y;
    const double cx_ = p2.x, cy_ = p2.y;
    const double d = 2 * (ax * (by - cy_) + bx * (cy_ - ay) + cx_ * (ay - by));
    if (std::fabs(d) < 1e-9) return nullptr;
    const double ux = (((std::pow(ax, 2.0) + std::pow(ay, 2.0)) * (by - cy_) + (std::pow(bx, 2.0) + std::pow(by, 2.0)) * (cy_ - ay) +
                        (std::pow(cx_, 2.0) + std::pow(cy_, 2.0)) * (ay - by))) / d;
    const double uy = (((std::pow(ax, 2.0) + std::pow(ay, 2.0)) * (cx_ - bx) + (std::pow(bx, 2.0) + std::pow(by, 2.0)) * (ax - cx_) +
                        (std::pow(cx_, 2.0) + std::pow(cy_, 2.0)) * (bx - ax))) / d;
    const double r = hypot(ax - ux, ay - uy);
    const double a1 = fmod_py(degrees(std::atan2(p1.y - uy, p1.x - ux)), 360.0);
    const double a2 = fmod_py(degrees(std::atan2(p2.y - uy, p2.x - ux)), 360.0);
    const double a3 = fmod_py(degrees(std::atan2(p3.y - uy, p3.x - ux)), 360.0);
    const bool ccw = fmod_py(a3 - a1, 360.0) < fmod_py(a2 - a1, 360.0);
    auto arc = std::make_shared<cad::Arc>();
    arc->cx = ux;
    arc->cy = uy;
    arc->radius = r;
    arc->start_angle = ccw ? a1 : a2;
    arc->end_angle = ccw ? a2 : a1;
    return arc;
}

// ======================================================================
// Desenho livre
// ======================================================================

void FreehandTool::deactivate() {
    active_ = false;
    pts_.clear();
    BaseTool::deactivate();
}

bool FreehandTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    active_ = true;
    pts_ = {{wx, wy}};
    return true;
}

bool FreehandTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (!active_) return false;
    const Pt last = pts_.back();
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 2;
    if (hypot(wx - last.x, wy - last.y) >= tol) {
        pts_.push_back({wx, wy});
        ctx->preview(pts_, false);
    }
    return true;
}

bool FreehandTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!active_ || button != Qt::LeftButton) return false;
    on_move(wx, wy, mods);
    active_ = false;
    ctx->clear_preview();
    Points pts = std::move(pts_);
    pts_.clear();
    if (pts.size() < 2) return true;
    if (smooth && pts.size() >= 4) pts = cad::fit_curve(pts, false, 0.5, 4);
    auto poly = std::make_shared<cad::Polyline>();
    poly->points = pts;
    poly->is_closed = false;
    ctx->add(poly, label());
    ctx->status(std::format("Traço criado ({} pontos)", pts.size()));
    return true;
}

}  // namespace cnc::tools
