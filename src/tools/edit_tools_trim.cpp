// Porte de TrimTool, InteractiveTrimTool e ExtendTool (src/tools/edit_tools.py).
#include <algorithm>
#include <cmath>
#include <format>

#include "cad/geometry.hpp"
#include "tools/cursors.hpp"
#include "tools/edit_tools.hpp"

namespace cnc::tools {

// ======================================================================
// Aparar objetos
// ======================================================================

std::string TrimTool::hint() const {
    return "Selecione os objetos e, por último, o contorno fechado; escolha a região e aplique";
}

void TrimTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.size() < 2) {
        ctx->status("Selecione os objetos e, por último, um contorno fechado");
        return;
    }
    const cad::EntityPtr boundary = sel.back();
    if (!boundary->closed()) {
        ctx->status("O último vetor selecionado precisa ser um contorno fechado");
        return;
    }
    const geo::Geom clipping = cad::to_shapely(*boundary);
    if (!clipping) {
        ctx->status("Contorno de corte inválido");
        return;
    }

    std::vector<cad::EntityPtr> old, neu;
    size_t deleted = 0;
    for (size_t i = 0; i + 1 < sel.size(); ++i) {
        const cad::EntityPtr& entity = sel[i];
        const geo::Geom geom = cad::to_shapely(*entity);
        if (!geom) continue;
        const geo::Geom clipped = clear_outside ? geom.intersection(clipping) : geom.difference(clipping);
        if (!clipped) continue;
        if (geom.equals(clipped)) continue;
        old.push_back(entity);
        auto pieces = cad::from_shapely(clipped, entity.get());
        if (!pieces.empty()) {
            for (cad::EntityPtr& p : pieces) neu.push_back(std::move(p));
        } else {
            ++deleted;
        }
    }

    if (old.empty()) {
        ctx->status("Nenhum objeto precisa ser aparado");
        return;
    }

    if (deleted && !confirm(deleted, sel.size() - 1, *boundary)) {
        ctx->status("Aparar cancelado");
        return;
    }

    const size_t touched = old.size();
    ctx->replace(old, neu, "Aparar objetos");
    ctx->status(summary(touched, deleted));
}

bool TrimTool::confirm(size_t deleted, size_t total, const cad::Entity& boundary) const {
    const BBox b = boundary.bounding_box();
    const char* lado = clear_outside ? "fora" : "dentro";
    const std::string nl = "\n\n";
    return ctx->confirm_fn(
        "Aparar objetos",
        std::format("Isto vai apagar por completo {} de {} objeto(s) selecionado(s), porque está {} do contorno.", deleted,
                    total, lado) +
            nl +
            std::format("O contorno usado é o ÚLTIMO vetor que você selecionou, e ele mede {:.0f} × {:.0f} mm.", b.x1 - b.x0,
                        b.y1 - b.y0) +
            nl + "Continuar? (dá para desfazer com Ctrl+Z)");
}

std::string TrimTool::summary(size_t touched, size_t deleted) {
    if (deleted) {
        return std::format("{} objeto(s) aparado(s), {} apagado(s) por completo — Ctrl+Z desfaz", touched, deleted);
    }
    return std::format("{} objeto(s) aparado(s)", touched);
}

bool TrimTool::trim_at_click(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;

    cad::EntityPtr target;
    double best = tol;
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it)) continue;
        const double d = (*it)->distance_to(wx, wy);
        if (d < best) {
            target = *it;
            best = d;
        }
    }
    if (!target) {
        ctx->status("Clique sobre um vetor");
        return true;
    }

    if (target->group && !target->group->empty()) {
        ctx->status("Desagrupe o vetor antes de aparar seus trechos");
        return true;
    }

    std::vector<cad::EntityPtr> others;
    for (const cad::EntityPtr& e : entities) {
        if (e == target) continue;
        const cad::Layer* layer = ctx->document->get_layer(e->layer);
        if (!layer || layer->visible) others.push_back(e);
    }
    const auto parts = cad::trim_at_point(*target, wx, wy, others);
    if (parts.empty()) {
        ctx->status("Nenhum cruzamento encontrado nesse trecho");
        return true;
    }
    ctx->replace({target}, parts, "Aparar");
    ctx->status("Trecho removido");
    return true;
}

// ======================================================================
// Aparar interativo (a tesoura)
// ======================================================================

std::string InteractiveTrimTool::hint() const {
    return "Clique no trecho a remover, ou arraste sobre vários. A tesoura abre quando há trecho aparável sob o cursor.";
}

QCursor InteractiveTrimTool::cursor() const { return cursors::scissors(false); }

void InteractiveTrimTool::activate() {
    TrimTool::activate();
    hover_.reset();
}

void InteractiveTrimTool::deactivate() {
    down_ = false;
    hover_.reset();
    set_cursor_open(false);
    TrimTool::deactivate();
}

void InteractiveTrimTool::apply_form() {
    ctx->status("O Aparar interativo é por clique: clique no trecho a remover.");
}

cad::EntityPtr InteractiveTrimTool::trimmable_at(double wx, double wy) const {
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;
    cad::EntityPtr best;
    double d_best = tol;
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it) || ((*it)->group && !(*it)->group->empty())) continue;
        const double d = (*it)->distance_to(wx, wy);
        if (d < d_best) {
            best = *it;
            d_best = d;
        }
    }
    return best;
}

void InteractiveTrimTool::set_cursor_open(bool open) {
    if (hover_ == open || ctx->canvas == nullptr) return;
    hover_ = open;
    ctx->canvas->set_cursor(cursors::scissors(open));
}

bool InteractiveTrimTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return false;
    down_ = true;
    return trim_at_click(wx, wy, button, mods);
}

bool InteractiveTrimTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (down_) {
        trim_at_click(wx, wy, Qt::LeftButton, mods);
        return true;
    }
    set_cursor_open(trimmable_at(wx, wy) != nullptr);
    return false;
}

bool InteractiveTrimTool::on_release(double, double, Qt::MouseButton, Qt::KeyboardModifiers) {
    down_ = false;
    return true;
}

// ======================================================================
// Estender
// ======================================================================

std::string ExtendTool::hint() const {
    return "Clique perto da ponta de uma linha e depois no vetor limite · botão direito reinicia";
}

void ExtendTool::deactivate() {
    target_.reset();
    endpoint_.clear();
    BaseTool::deactivate();
}

bool ExtendTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (!target_) return false;
    const cad::Line& e = *target_;
    const Pt start{e.x1, e.y1}, end{e.x2, e.y2};
    const Pt tip = endpoint_ == "end" ? end : start;
    const Pt tail = endpoint_ == "end" ? start : end;
    const double dx = tip.x - tail.x, dy = tip.y - tail.y;
    const double length_sq = dx * dx + dy * dy;
    if (length_sq > 1e-12) {
        const double t = std::max(0.0, ((wx - tip.x) * dx + (wy - tip.y) * dy) / length_sq);
        ctx->preview({tip, {tip.x + t * dx, tip.y + t * dy}});
    }
    return true;
}

bool ExtendTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button == Qt::RightButton) {
        deactivate();
        ctx->status(hint());
        return true;
    }
    if (button != Qt::LeftButton) return false;
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;
    cad::EntityPtr hit;
    double best = tol;
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it)) continue;
        if (!target_ && !cad::is_a<cad::Line>(**it)) continue;
        if (it->get() == target_.get()) continue;
        const double d = (*it)->distance_to(wx, wy);
        if (d < best) {
            hit = *it;
            best = d;
        }
    }
    if (!hit) {
        ctx->status(target_ ? "Clique no vetor limite" : "Clique perto da ponta de uma linha reta");
        return true;
    }
    if (!target_) {
        if (hit->group && !hit->group->empty()) {
            ctx->status("Desagrupe a linha antes de estendê-la");
            return true;
        }
        target_ = std::dynamic_pointer_cast<cad::Line>(hit);
        endpoint_ = dist(Pt{wx, wy}, Pt{target_->x1, target_->y1}) < dist(Pt{wx, wy}, Pt{target_->x2, target_->y2}) ? "start" : "end";
        ctx->status("Agora clique no vetor que limita a extensão");
        return true;
    }
    const std::shared_ptr<cad::Line> target = target_;
    const std::string endpoint = endpoint_;
    cad::Line probe;
    probe.x1 = target->x1;
    probe.y1 = target->y1;
    probe.x2 = target->x2;
    probe.y2 = target->y2;
    if (!cad::extend_line_to(probe, {hit}, endpoint)) {
        ctx->status("Esse vetor não cruza o prolongamento da ponta escolhida");
        return true;
    }
    ctx->transform({target}, [hit, endpoint](cad::Entity& e) { cad::extend_line_to(static_cast<cad::Line&>(e), {hit}, endpoint); },
                   "Estender");
    deactivate();
    ctx->status("Linha estendida");
    return true;
}

}  // namespace cnc::tools
