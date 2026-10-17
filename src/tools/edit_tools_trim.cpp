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
    return "Aparagem Inteligente (Power Trim): arraste o mouse cruzando os vetores para aparar, ou clique no trecho desejado.";
}

QCursor InteractiveTrimTool::cursor() const { return cursors::scissors(false); }

void InteractiveTrimTool::activate() {
    TrimTool::activate();
    hover_.reset();
    down_ = false;
    trail_.clear();
}

void InteractiveTrimTool::deactivate() {
    down_ = false;
    trail_.clear();
    ctx->clear_preview();
    hover_.reset();
    set_cursor_open(false);
    TrimTool::deactivate();
}

void InteractiveTrimTool::apply_form() {
    ctx->status("O Aparar interativo é por arraste ou clique: arraste cruzando as linhas que deseja cortar.");
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
    trail_ = {{wx, wy}};
    return trim_at_click(wx, wy, button, mods);
}

bool InteractiveTrimTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (down_) {
        const Pt p_cur{wx, wy};
        if (trail_.empty()) {
            trail_.push_back(p_cur);
        } else {
            const Pt p_prev = trail_.back();
            const double move_d = std::hypot(p_cur.x - p_prev.x, p_cur.y - p_prev.y);
            if (move_d >= 0.2) {
                trail_.push_back(p_cur);
                ctx->preview(trail_, false);

                // Power Trim: cortar qualquer vetor cruzado pelo segmento de rastro (p_prev, p_cur)
                auto& entities = ctx->document->entities;
                bool sliced_any = false;
                for (size_t k = 0; k < entities.size(); ++k) {
                    cad::EntityPtr target = entities[k];
                    if (!ctx->document->is_editable(*target) || (target->group && !target->group->empty())) continue;

                    const Points pts = target->to_points(cad::kSeg);
                    const size_t n = pts.size();
                    const size_t seg_count = target->closed() ? n : (n > 0 ? n - 1 : 0);

                    std::optional<Pt> hit_pt;
                    for (size_t i = 0; i < seg_count; ++i) {
                        const auto inter = cad::segment_intersection(p_prev, p_cur, pts[i], pts[(i + 1) % n]);
                        if (inter) {
                            hit_pt = inter;
                            break;
                        }
                    }

                    if (hit_pt) {
                        std::vector<cad::EntityPtr> others;
                        for (const cad::EntityPtr& e : entities) {
                            if (e == target) continue;
                            const cad::Layer* layer = ctx->document->get_layer(e->layer);
                            if (!layer || layer->visible) others.push_back(e);
                        }
                        const auto parts = cad::trim_at_point(*target, hit_pt->x, hit_pt->y, others);
                        if (!parts.empty()) {
                            ctx->replace({target}, parts, "Aparar (Power Trim)");
                            ctx->status("Trecho aparado (Power Trim)");
                            sliced_any = true;
                            break;
                        }
                    }
                }
                if (!sliced_any && move_d > 1.0) {
                    trim_at_click(wx, wy, Qt::LeftButton, mods);
                }
            }
        }
        return true;
    }
    set_cursor_open(trimmable_at(wx, wy) != nullptr);
    return false;
}

bool InteractiveTrimTool::on_release(double, double, Qt::MouseButton, Qt::KeyboardModifiers) {
    down_ = false;
    trail_.clear();
    ctx->clear_preview();
    return true;
}

// ======================================================================
// Estender (SolidWorks Smart 1-Click Extend)
// ======================================================================

std::string ExtendTool::hint() const {
    return "Estender Entidades (SolidWorks): aponte para a ponta de uma linha para ver o limite e clique para estender (botão direito sai).";
}

void ExtendTool::deactivate() {
    target_.reset();
    endpoint_.clear();
    ctx->clear_preview();
    BaseTool::deactivate();
}

std::optional<ExtendTool::ExtendCandidate> ExtendTool::find_candidate(double wx, double wy) const {
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 12;
    std::shared_ptr<cad::Line> best_line;
    std::string best_endpoint;
    double best_dist = tol;
    Pt best_tip{};

    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it)) continue;
        auto line = std::dynamic_pointer_cast<cad::Line>(*it);
        if (!line) continue;
        if (line->group && !line->group->empty()) continue;

        const double d_start = std::hypot(wx - line->x1, wy - line->y1);
        const double d_end = std::hypot(wx - line->x2, wy - line->y2);
        if (d_start < best_dist) {
            best_dist = d_start;
            best_line = line;
            best_endpoint = "start";
            best_tip = {line->x1, line->y1};
        }
        if (d_end < best_dist) {
            best_dist = d_end;
            best_line = line;
            best_endpoint = "end";
            best_tip = {line->x2, line->y2};
        }
    }

    if (!best_line) return std::nullopt;

    std::vector<cad::EntityPtr> targets;
    for (const auto& e : entities) {
        if (e != best_line) {
            const cad::Layer* layer = ctx->document->get_layer(e->layer);
            if (!layer || layer->visible) targets.push_back(e);
        }
    }

    cad::Line probe = *best_line;
    if (!cad::extend_line_to(probe, targets, best_endpoint)) {
        return std::nullopt;
    }

    const Pt target_pt = (best_endpoint == "start") ? Pt{probe.x1, probe.y1} : Pt{probe.x2, probe.y2};
    return ExtendCandidate{best_line, best_endpoint, best_tip, target_pt};
}

bool ExtendTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    const auto cand = find_candidate(wx, wy);
    if (cand) {
        ctx->preview({cand->tip, cand->target_pt}, false);
        ctx->status("Clique para estender até o limite mais próximo (SolidWorks Smart Extend)");
        return true;
    }
    ctx->clear_preview();
    return false;
}

bool ExtendTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button == Qt::RightButton) {
        deactivate();
        ctx->status("Estender cancelado");
        return true;
    }
    if (button != Qt::LeftButton) return false;

    const auto cand = find_candidate(wx, wy);
    if (!cand) {
        ctx->status("Aproxime o cursor da ponta de uma linha apontando para outro vetor limite");
        return true;
    }

    const auto line = cand->line;
    const std::string endpoint = cand->endpoint;
    std::vector<cad::EntityPtr> targets;
    for (const auto& e : ctx->document->entities) {
        if (e != line) {
            const cad::Layer* layer = ctx->document->get_layer(e->layer);
            if (!layer || layer->visible) targets.push_back(e);
        }
    }

    ctx->transform({line}, [&targets, endpoint](cad::Entity& e) {
        cad::extend_line_to(static_cast<cad::Line&>(e), targets, endpoint);
    }, "Estender linha");

    ctx->status("Linha estendida até o limite (1 clique)");

    // Refresh candidate immediately for next extension
    const auto next_cand = find_candidate(wx, wy);
    if (next_cand) {
        ctx->preview({next_cand->tip, next_cand->target_pt}, false);
    } else {
        ctx->clear_preview();
    }
    return true;
}

}  // namespace cnc::tools
