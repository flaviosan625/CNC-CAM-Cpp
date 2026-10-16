// Porte de src/tools/transform_tools.py: mover, rotacionar, espelhar, escalar, distorcer e copias.
#include "tools/transform_tools.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "cad/job_commands.hpp"

namespace cnc::tools {

namespace {

bool has(Qt::KeyboardModifiers mods, Qt::KeyboardModifier flag) { return mods.testFlag(flag); }

}  // namespace

BBox sel_bbox(const std::vector<cad::EntityPtr>& ents) {
    if (ents.empty()) return {0.0, 0.0, 0.0, 0.0};
    BBox out = ents.front()->bounding_box();
    for (size_t i = 1; i < ents.size(); ++i) {
        const BBox b = ents[i]->bounding_box();
        out.x0 = std::min(out.x0, b.x0);
        out.y0 = std::min(out.y0, b.y0);
        out.x1 = std::max(out.x1, b.x1);
        out.y1 = std::max(out.y1, b.y1);
    }
    return out;
}

Pt anchor_point(const std::vector<cad::EntityPtr>& ents, const std::string& anchor) {
    const BBox b = sel_bbox(ents);
    const double mx = (b.x0 + b.x1) / 2, my = (b.y0 + b.y1) / 2;
    static const std::map<std::string, std::pair<int, int>> table = {
        {"bottom-left", {0, 0}}, {"bottom-center", {1, 0}}, {"bottom-right", {2, 0}},
        {"middle-left", {0, 1}}, {"center", {1, 1}},        {"middle-right", {2, 1}},
        {"top-left", {0, 2}},    {"top-center", {1, 2}},    {"top-right", {2, 2}},
    };
    const auto it = table.find(anchor);
    if (it == table.end()) return {mx, my};
    const double xs[3] = {b.x0, mx, b.x1};
    const double ys[3] = {b.y0, my, b.y1};
    return {xs[it->second.first], ys[it->second.second]};
}

// ======================================================================
// Mover
// ======================================================================

std::string MoveTool::hint() const {
    return "Arraste a seleção — ela encosta sozinha nos pontos do desenho. Shift mantém no eixo e desliga o imã.";
}

void MoveTool::deactivate() {
    drag_ = false;
    BaseTool::deactivate();
}

bool MoveTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return true;
    }
    targets_ = sel;
    begin_live_transform(sel);
    drag_ = true;
    start_ = last_ = {wx, wy};
    return true;
}

bool MoveTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (!drag_) return false;
    double ddx = wx - last_.x, ddy = wy - last_.y;
    if (has(mods, Qt::ShiftModifier) || has(mods, Qt::AltModifier)) {
        if (std::fabs(wx - start_.x) >= std::fabs(wy - start_.y)) {
            ddy = start_.y - last_.y;
        } else {
            ddx = start_.x - last_.x;
        }
    }

    // Corrige o deslocamento para a peca grudar no que esta parado. E o que impede a fresta de
    // 0,3 mm entre duas pecas encostadas, que so aparece depois, no corte.
    std::optional<vp::SnapResult> encaixe;
    ICanvas* canvas = ctx->canvas;
    const vp::SnapEngine* motor = canvas ? canvas->snap_engine() : nullptr;
    if (canvas && !has(mods, Qt::ShiftModifier) && motor) {
        const vp::MovingSnap moving = motor->snap_moving(targets_, ddx, ddy, canvas->px_per_mm());
        ddx = moving.dx;
        ddy = moving.dy;
        encaixe = moving.hit;
        if (encaixe) canvas->show_snap_marker(encaixe);
    }

    for (const cad::EntityPtr& e : targets_) e->translate(ddx, ddy);
    last_ = {last_.x + ddx, last_.y + ddy};
    ctx->refresh();
    std::string texto = std::format("ΔX: {:.2f}  ΔY: {:.2f}", last_.x - start_.x, last_.y - start_.y);
    if (encaixe) texto += std::format("   ▸ encostado em {} ({:.2f}, {:.2f})", encaixe->label(), encaixe->x, encaixe->y);
    ctx->status(texto);
    return true;
}

bool MoveTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!drag_ || button != Qt::LeftButton) return false;
    on_move(wx, wy, mods);
    drag_ = false;
    const double tx = last_.x - start_.x, ty = last_.y - start_.y;
    if (std::fabs(tx) < 1e-9 && std::fabs(ty) < 1e-9) {
        restore_live_transform();
        return true;
    }
    const auto targets = targets_;
    restore_live_transform();
    ctx->translate(targets, tx, ty, "Mover");
    return true;
}

void MoveTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    double ddx, ddy;
    if (absolute) {
        const Pt a = anchor_point(sel, anchor);
        const Pt o = ctx->document->material.origin_xy();
        ddx = dx + o.x - a.x;
        ddy = dy + o.y - a.y;
    } else {
        ddx = dx;
        ddy = dy;
    }

    if (make_copy) {
        std::vector<cad::EntityPtr> copies;
        for (const cad::EntityPtr& e : sel) copies.push_back(e->clone());
        for (const cad::EntityPtr& c : copies) c->translate(ddx, ddy);
        ctx->add(copies, "Mover cópia", true);
    } else {
        ctx->translate(sel, ddx, ddy, "Mover");
    }
    ctx->status(std::format("Movido ΔX {:.2f} · ΔY {:.2f}", ddx, ddy));
}

// ======================================================================
// Rotacionar
// ======================================================================

void RotateTool::deactivate() {
    pivot_.reset();
    BaseTool::deactivate();
}

bool RotateTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return true;
    }
    targets_ = sel;
    begin_live_transform(sel);
    pivot_ = anchor_point(sel, anchor);
    a0_ = degrees(std::atan2(wy - pivot_->y, wx - pivot_->x));
    acc_ = 0.0;
    return true;
}

bool RotateTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (!pivot_) return false;
    const double a = degrees(std::atan2(wy - pivot_->y, wx - pivot_->x));
    double target = a - a0_;
    if (has(mods, Qt::ShiftModifier) || has(mods, Qt::AltModifier)) target = py_round(target / 15.0) * 15.0;
    const double delta = target - acc_;
    if (std::fabs(delta) > 1e-9) {
        for (const cad::EntityPtr& e : targets_) e->rotate(delta, pivot_->x, pivot_->y);
        acc_ = target;
        ctx->refresh();
        ctx->status(std::format("Ângulo: {:.1f}°", acc_));
    }
    return true;
}

bool RotateTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!pivot_ || button != Qt::LeftButton) return false;
    on_move(wx, wy, mods);
    const Pt pivot = *pivot_;
    const double total = acc_;
    pivot_.reset();
    if (std::fabs(total) < 1e-9) {
        restore_live_transform();
        return true;
    }
    const auto targets = targets_;
    restore_live_transform();
    ctx->transform(targets, [total, pivot](cad::Entity& e) { e.rotate(total, pivot.x, pivot.y); }, "Rotacionar");
    return true;
}

void RotateTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    const Pt p = anchor_point(sel, anchor);
    const double ang = angle;
    if (make_copy) {
        std::vector<cad::EntityPtr> copies;
        for (const cad::EntityPtr& e : sel) copies.push_back(e->clone());
        for (const cad::EntityPtr& c : copies) c->rotate(ang, p.x, p.y);
        ctx->add(copies, "Rotacionar cópia", true);
    } else {
        ctx->transform(sel, [ang, p](cad::Entity& e) { e.rotate(ang, p.x, p.y); }, "Rotacionar");
    }
    ctx->status(std::format("Rotacionado {:.2f}°", ang));
}

// ======================================================================
// Espelhar
// ======================================================================

void MirrorTool::apply(const std::string& axis) {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }

    const cad::MaterialSetup& mat = ctx->document->material;
    double cx, cy;
    if (about_material) {
        cx = mat.width / 2;
        cy = mat.height / 2;
    } else {
        const BBox b = sel_bbox(sel);
        cx = (b.x0 + b.x1) / 2;
        cy = (b.y0 + b.y1) / 2;
    }

    const double sx = axis == "h" ? -1 : 1;
    const double sy = axis == "h" ? 1 : -1;
    const std::string lbl = axis == "h" ? "Espelhar horizontal" : "Espelhar vertical";

    if (make_copy) {
        const auto copies = cad::scaled_entities(clone_entities(sel), sx, sy, mat.curve_segments(), cx, cy);
        ctx->add(copies, lbl + " (cópia)", true);
    } else {
        const auto mirrored = cad::scaled_entities(sel, sx, sy, mat.curve_segments(), cx, cy);
        ctx->replace(sel, mirrored, lbl);
    }
    ctx->status(lbl);
}

bool MirrorTool::on_key(int key) {
    if (key == Qt::Key_H) {
        apply("h");
        return true;
    }
    if (key == Qt::Key_V) {
        apply("v");
        return true;
    }
    return BaseTool::on_key(key);
}

// ======================================================================
// Redimensionar
// ======================================================================

void ScaleTool::activate() {
    if (!ctx->selection().empty()) {
        const BBox b = sel_bbox(ctx->selection());
        width = b.x1 - b.x0;
        height = b.y1 - b.y0;
    }
    BaseTool::activate();
}

void ScaleTool::deactivate() {
    pivot_.reset();
    BaseTool::deactivate();
}

bool ScaleTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return true;
    }
    targets_ = sel;
    begin_live_transform(sel);
    pivot_ = anchor_point(sel, anchor);
    d0_ = std::max(hypot(wx - pivot_->x, wy - pivot_->y), 1e-6);
    acc_ = 1.0;
    return true;
}

bool ScaleTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (!pivot_) return false;
    const double d = std::max(hypot(wx - pivot_->x, wy - pivot_->y), 1e-6);
    const double target = std::max(d / d0_, 1e-4);
    const double f = target / acc_;
    if (std::fabs(f - 1.0) > 1e-9) {
        for (const cad::EntityPtr& e : targets_) e->scale(f, f, pivot_->x, pivot_->y);
        acc_ = target;
        ctx->refresh();
        ctx->status(std::format("Escala: {:.1f}%", acc_ * 100));
    }
    return true;
}

bool ScaleTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!pivot_ || button != Qt::LeftButton) return false;
    on_move(wx, wy, mods);
    const Pt pivot = *pivot_;
    const double total = acc_;
    pivot_.reset();
    if (std::fabs(total - 1.0) < 1e-9) {
        restore_live_transform();
        return true;
    }
    const auto targets = targets_;
    restore_live_transform();
    ctx->transform(targets, [total, pivot](cad::Entity& e) { e.scale(total, total, pivot.x, pivot.y); }, "Redimensionar");
    return true;
}

void ScaleTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    const BBox b = sel_bbox(sel);
    const double cw = std::max(b.x1 - b.x0, 1e-9), ch = std::max(b.y1 - b.y0, 1e-9);

    double sx, sy;
    if (use_percent) {
        sx = sy = percent / 100.0;
    } else {
        sx = width > 0 ? width / cw : 1.0;
        sy = height > 0 ? height / ch : 1.0;
        if (link_xy) {
            if (width > 0) {
                sy = sx;
            } else if (height > 0) {
                sx = sy;
            }
        }
    }

    if (std::fabs(sx - 1) < 1e-9 && std::fabs(sy - 1) < 1e-9) return;
    const Pt p = anchor_point(sel, anchor);
    const cad::MaterialSetup& mat = ctx->document->material;
    if (make_copy) {
        const auto copies = cad::scaled_entities(clone_entities(sel), sx, sy, mat.curve_segments(), p.x, p.y);
        ctx->add(copies, "Escalar cópia", true);
    } else if (!isclose(sx, sy)) {
        const auto scaled = cad::scaled_entities(sel, sx, sy, mat.curve_segments(), p.x, p.y);
        ctx->replace(sel, scaled, "Redimensionar");
    } else {
        ctx->transform(sel, [sx, sy, p](cad::Entity& e) { e.scale(sx, sy, p.x, p.y); }, "Redimensionar");
    }
    ctx->status(std::format("Escala X {:.1f}% · Y {:.1f}%", sx * 100, sy * 100));
}

// ======================================================================
// Distorcer
// ======================================================================

namespace {

Pt bilinear(double x, double y, const BBox& box, const Points& dst) {
    const double u = (x - box.x0) / std::max(box.x1 - box.x0, 1e-9);
    const double v = (y - box.y0) / std::max(box.y1 - box.y0, 1e-9);
    const Pt p00 = dst[0], p10 = dst[1], p11 = dst[2], p01 = dst[3];
    const double bx = (1 - u) * (1 - v) * p00.x + u * (1 - v) * p10.x + u * v * p11.x + (1 - u) * v * p01.x;
    const double by = (1 - u) * (1 - v) * p00.y + u * (1 - v) * p10.y + u * v * p11.y + (1 - u) * v * p01.y;
    return {bx, by};
}

}  // namespace

void DistortTool::deactivate() {
    corner_.reset();
    dst_.clear();
    BaseTool::deactivate();
}

void DistortTool::activate() {
    BaseTool::activate();
    const auto sel = ctx->selection();
    if (!sel.empty()) show_box(sel);
}

void DistortTool::show_box(const std::vector<cad::EntityPtr>& sel) {
    box_ = sel_bbox(sel);
    dst_ = {{box_.x0, box_.y0}, {box_.x1, box_.y0}, {box_.x1, box_.y1}, {box_.x0, box_.y1}};
    ctx->preview(dst_, true);
}

bool DistortTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return true;
    }
    if (dst_.empty()) show_box(sel);

    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 12;
    for (size_t i = 0; i < dst_.size(); ++i) {
        if (hypot(wx - dst_[i].x, wy - dst_[i].y) <= tol) {
            corner_ = i;
            targets_ = sel;
            return true;
        }
    }
    show_box(sel);
    return true;
}

bool DistortTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (!corner_) return false;
    dst_[*corner_] = {wx, wy};
    ctx->preview(dst_, true);
    return true;
}

bool DistortTool::on_release(double, double, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!corner_ || button != Qt::LeftButton) return false;
    corner_.reset();
    const BBox box = box_;
    const Points dst = dst_;
    const auto targets = targets_;

    std::vector<cad::EntityPtr> new_ents;
    for (const cad::EntityPtr& e : targets) {
        auto poly = std::make_shared<cad::Polyline>();
        for (const Pt& p : e->to_points(96)) poly->points.push_back(bilinear(p.x, p.y, box, dst));
        poly->is_closed = e->closed();
        poly->layer = e->layer;
        poly->color = e->color;
        new_ents.push_back(poly);
    }
    ctx->replace(targets, new_ents, "Distorcer");
    ctx->clear_preview();
    dst_.clear();
    ctx->status("Distorção aplicada");
    return true;
}

// ======================================================================
// Copia em array
// ======================================================================

void ArrayCopyTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    const BBox b = sel_bbox(sel);
    const double w = b.x1 - b.x0, h = b.y1 - b.y0;
    const double step_x = use_gap ? (w + gap_x) : gap_x;
    const double step_y = use_gap ? (h + gap_y) : gap_y;

    std::vector<cad::EntityPtr> out;
    for (int r = 0; r < std::max(1, rows); ++r) {
        for (int c = 0; c < std::max(1, cols); ++c) {
            if (r == 0 && c == 0) continue;
            for (cad::EntityPtr& cp : clone_entities(sel)) {
                cp->translate(c * step_x, r * step_y);
                out.push_back(std::move(cp));
            }
        }
    }
    if (!out.empty()) {
        const size_t count = out.size();
        ctx->add(out, "Cópia em array", true);
        ctx->status(std::format("{} cópia(s) criada(s)", count));
    }
}

// ======================================================================
// Copia circular
// ======================================================================

namespace {

Pt rot(double x, double y, double cx, double cy, double deg) {
    const double a = radians(deg);
    const double dx = x - cx, dy = y - cy;
    return {cx + dx * std::cos(a) - dy * std::sin(a), cy + dx * std::sin(a) + dy * std::cos(a)};
}

Points cross(double x, double y, double r) { return {{x - r, y}, {x + r, y}, {x, y}, {x, y - r}, {x, y + r}}; }

}  // namespace

bool CircularCopyTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    center_x = wx;
    center_y = wy;
    ctx->preview(cross(wx, wy, 5.0), false);
    ctx->status(std::format("Centro: X {:.2f}  Y {:.2f}", wx, wy));
    return true;
}

void CircularCopyTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    double cx, cy;
    if (!center_x) {
        const Pt c = anchor_point(sel, "center");
        cx = c.x;
        cy = c.y;
    } else {
        cx = *center_x;
        cy = center_y.value_or(0.0);
    }

    const int n = std::max(2, count);
    const double step = use_total ? (total_angle / n) : step_angle;

    std::vector<cad::EntityPtr> out;
    for (int i = 1; i < n; ++i) {
        const double a = -step * i;  // Vectric: angulos positivos copiam no sentido horario.
        auto copies = clone_entities(sel);
        for (size_t k = 0; k < sel.size(); ++k) {
            const cad::EntityPtr& e = sel[k];
            cad::EntityPtr& cp = copies[k];
            if (rotate_copies) {
                cp->rotate(a, cx, cy);
            } else {
                const Pt ec = e->center();
                const Pt n2 = rot(ec.x, ec.y, cx, cy, a);
                cp->translate(n2.x - ec.x, n2.y - ec.y);
            }
            out.push_back(cp);
        }
    }
    if (!out.empty()) {
        const size_t added = out.size();
        ctx->add(out, "Cópia circular", true);
        ctx->clear_preview();
        ctx->status(std::format("{} cópia(s) em círculo", added));
    }
}

}  // namespace cnc::tools
