// Porte de SelectTool (src/tools/draw_tools.py).
#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "tools/draw_tools.hpp"
#include "viewport/entity_items.hpp"

namespace cnc::tools {

namespace {

bool has(Qt::KeyboardModifiers mods, Qt::KeyboardModifier flag) { return mods.testFlag(flag); }

}  // namespace

std::string SelectTool::hint() const {
    return "Arraste por um ponto branco para encostar na peça ao lado · Ctrl: solta cópia · Alt: trava no eixo · "
           "Shift: soma/remove e desliga o ímã";
}

void SelectTool::deactivate() {
    if (ctx->canvas) ctx->canvas->clear_translation_preview();
    mode_.clear();
    cmd_targets_.clear();
    BaseTool::deactivate();
}

std::vector<cad::EntityPtr> SelectTool::members(const cad::EntityPtr& entity) const {
    if (!entity->group || entity->group->empty()) return {entity};
    std::vector<cad::EntityPtr> out;
    for (const cad::EntityPtr& e : ctx->document->entities) {
        if (e->group == entity->group && ctx->document->is_editable(*e)) out.push_back(e);
    }
    return out;
}

std::vector<cad::EntityPtr> SelectTool::snap_targets() const {
    return mode_ == "move" ? cmd_targets_ : std::vector<cad::EntityPtr>{};
}

std::optional<vp::HandleKind> SelectTool::active_handle() const {
    if (handle_) return handle_->kind;
    return std::nullopt;
}

vp::SelectionHandles* SelectTool::handles() const {
    vp::SelectionHandles* h = ctx->canvas ? ctx->canvas->selection_handles() : nullptr;
    return (h && h->visible()) ? h : nullptr;
}

cad::EntityPtr SelectTool::pick(double wx, double wy, double tol) const {
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it)) continue;
        if (vp::hit_test(**it, wx, wy, tol * kPickPx)) return *it;
    }
    return nullptr;
}

bool SelectTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return false;

    vp::SelectionHandles* alcas = handles();
    if (alcas) {
        if (const auto alca = alcas->hit(wx, wy)) return press_handle(*alca);
        // Agarrar por uma ancora da selecao: aquele ponto vira a referencia do encaixe, entao
        // encostar canto com canto e exato.
        if (const auto ancora = alcas->anchor_at(wx, wy); ancora && !ctx->selection().empty()) {
            return press_move(wx, wy, mods, ancora);
        }
    }

    const double tol = ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0;
    const cad::EntityPtr hit = pick(wx, wy, tol);
    // So Shift soma/remove da selecao: o Ctrl fica livre para arrastar copia, que e o que ele faz
    // no Aspire.
    const bool add = has(mods, Qt::ShiftModifier);

    if (!hit) {
        if (!add) {
            ctx->document->clear_selection();
            ctx->refresh();
        }
        mode_ = "rubber";
        start_ = {wx, wy};
        last_ = {wx, wy};
        return true;
    }

    const auto group = members(hit);
    const bool all_selected = std::all_of(group.begin(), group.end(), [](const cad::EntityPtr& e) { return e->selected; });
    if (add) {
        for (const cad::EntityPtr& e : group) ctx->document->set_selected(e, !all_selected);
        ctx->refresh();
    } else if (!all_selected) {
        ctx->document->clear_selection();
        for (const cad::EntityPtr& e : group) ctx->document->set_selected(e, true);
        ctx->refresh();
    }

    return press_move(wx, wy, mods, std::nullopt);
}

bool SelectTool::press_move(double wx, double wy, Qt::KeyboardModifiers mods, const std::optional<vp::AnchorHit>& grab) {
    mode_ = "move";
    start_ = {wx, wy};
    last_ = {wx, wy};
    moved_ = false;
    grab_ = grab ? std::optional<Pt>(grab->point) : std::nullopt;
    copying_ = has(mods, Qt::ControlModifier);
    cmd_targets_ = ctx->selection();
    // O modelo fica intacto durante o arrasto; o canvas mostra uma translacao temporaria.
    if (grab) ctx->status("Agarrado pelo " + grab->label + " — encoste na peça ao lado");
    return true;
}

bool SelectTool::press_handle(const vp::HandleHit& alca) {
    const auto sel = ctx->selection();
    if (sel.empty()) return false;
    vp::SelectionHandles* h = ctx->canvas->selection_handles();
    cmd_targets_ = sel;
    handle_ = alca;
    begin_live_transform(sel);
    moved_ = false;
    acc_ = 0.0;
    if (vp::is_rotate(alca.kind)) {
        mode_ = "handle-rotate";
        pivot_ = h->rotate_pivot();
        a0_ = degrees(std::atan2(alca.world_y - pivot_.y, alca.world_x - pivot_.x));
        ctx->status("0.0°");
    } else {
        mode_ = "handle-scale";
        pivot_ = h->scale_pivot(alca.kind);
        ctx->status("100% × 100%");
    }
    return true;
}

// Fatores de escala para a posicao atual do mouse. A alca e desenhada afastada da caixa, entao o
// que conta e o DESLOCAMENTO do mouse desde o clique, aplicado ao canto real da caixa - arrastar
// 10 mm cresce a peca 10 mm, nao um fator ligado a distancia ate a alca.
std::pair<double, double> SelectTool::scale_factors(double wx, double wy, Qt::KeyboardModifiers mods) const {
    const vp::HandleHit& alca = *handle_;
    const auto [em_x, em_y] = vp::SelectionHandles::axes(alca.kind);
    const double base_x = alca.bbox_x - pivot_.x;
    const double base_y = alca.bbox_y - pivot_.y;
    double sx = (em_x && std::fabs(base_x) > 1e-9) ? (base_x + (wx - alca.world_x)) / base_x : 1.0;
    double sy = (em_y && std::fabs(base_y) > 1e-9) ? (base_y + (wy - alca.world_y)) / base_y : 1.0;
    // Arraste livre por padrao. Shift/Ctrl mantem a proporcao quando o operador quiser restringir o
    // gesto temporariamente.
    if ((has(mods, Qt::ShiftModifier) || has(mods, Qt::ControlModifier)) && em_x && em_y) sx = sy = (sx + sy) / 2.0;
    // Escala <= 0 espelha a peca e da no: trava num minimo positivo.
    return {sx > 0 ? std::max(sx, 1e-6) : 1e-6, sy > 0 ? std::max(sy, 1e-6) : 1e-6};
}

bool SelectTool::move_scale(double wx, double wy, Qt::KeyboardModifiers mods) {
    const auto [sx, sy] = scale_factors(wx, wy, mods);
    // Escalamos sempre a partir do estado original, entao e preciso desfazer o passo anterior
    // antes de aplicar o novo.
    restore_live_transform(false, true);
    for (const cad::EntityPtr& e : cmd_targets_) e->scale(sx, sy, pivot_.x, pivot_.y);
    moved_ = true;
    ctx->refresh();
    ctx->status(std::format("{:.1f}% × {:.1f}%", sx * 100, sy * 100));
    return true;
}

bool SelectTool::on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
    if (mode_ == "handle-rotate") return move_rotate(wx, wy, mods);
    if (mode_ == "handle-scale") return move_scale(wx, wy, mods);
    if (mode_ == "rubber") {
        const double x0 = start_.x, y0 = start_.y;
        const Points pts = {{x0, y0}, {wx, y0}, {wx, wy}, {x0, wy}};
        if (ctx->canvas) {
            ctx->canvas->set_selection_preview(pts, wx < x0);
        } else {
            ctx->preview(pts, true);
        }
        last_ = {wx, wy};
        return true;
    }

    if (mode_ == "move") {
        const double tol = ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0;
        if (!moved_ && hypot(wx - start_.x, wy - start_.y) < 3 * tol) return true;
        // O deslocamento e absoluto desde o clique. A geometria CAD so muda ao soltar o mouse.
        const auto [ddx, ddy] = move_delta(wx, wy, mods);
        if (ctx->canvas) ctx->canvas->preview_translation(cmd_targets_, ddx, ddy);
        last_ = {start_.x + ddx, start_.y + ddy};
        moved_ = true;
        return true;
    }
    return false;
}

// Deslocamento total desde o clique, com trava de eixo e encaixe. Exige a peca na posicao do
// clique: quem chama restaura antes.
std::pair<double, double> SelectTool::move_delta(double wx, double wy, Qt::KeyboardModifiers mods) {
    double ddx = wx - start_.x;
    double ddy = wy - start_.y;
    // Alt trava no eixo dominante, como no Aspire.
    if (has(mods, Qt::AltModifier)) {
        if (std::fabs(ddx) >= std::fabs(ddy)) {
            ddy = 0.0;
        } else {
            ddx = 0.0;
        }
    }
    ICanvas* canvas = ctx->canvas;
    const vp::SnapEngine* motor = canvas ? canvas->snap_engine() : nullptr;
    // Shift desliga o ima, para posicionar livre.
    if (!motor || has(mods, Qt::ShiftModifier)) return {ddx, ddy};
    const vp::MovingSnap moving = motor->snap_moving(cmd_targets_, ddx, ddy, canvas->px_per_mm(), grab_);
    if (moving.hit) {
        canvas->show_snap_marker(moving.hit);
        ctx->status(std::format("Encostado em {} ({:.2f}, {:.2f})", moving.hit->label(), moving.hit->x, moving.hit->y));
    }
    return {moving.dx, moving.dy};
}

bool SelectTool::move_rotate(double wx, double wy, Qt::KeyboardModifiers mods) {
    const double a = degrees(std::atan2(wy - pivot_.y, wx - pivot_.x));
    double total = a - a0_;
    if (has(mods, Qt::ShiftModifier) || has(mods, Qt::AltModifier)) total = py_round(total / 15.0) * 15.0;
    const double delta = total - acc_;
    if (std::fabs(delta) > 1e-9) {
        for (const cad::EntityPtr& e : cmd_targets_) e->rotate(delta, pivot_.x, pivot_.y);
        acc_ = total;
        moved_ = true;
        ctx->refresh();
        ctx->status(std::format("{:+.1f}°", acc_));
    }
    return true;
}

bool SelectTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return false;
    if (mode_ == "handle-rotate" || mode_ == "handle-scale") {
        const auto targets = cmd_targets_;
        const Pt pivot = pivot_;
        if (mode_ == "handle-rotate") {
            move_rotate(wx, wy, mods);
            const double total = acc_;
            restore_live_transform(false);
            if (std::fabs(total) > 1e-9) {
                ctx->transform(targets, [total, pivot](cad::Entity& e) { e.rotate(total, pivot.x, pivot.y); }, "Rotacionar");
            } else ctx->refresh();
        } else {
            const auto [sx, sy] = scale_factors(wx, wy, mods);
            restore_live_transform(false);
            if (std::fabs(sx - 1.0) > 1e-6 || std::fabs(sy - 1.0) > 1e-6) {
                ctx->transform(targets, [sx, sy, pivot](cad::Entity& e) { e.scale(sx, sy, pivot.x, pivot.y); }, "Redimensionar");
            } else ctx->refresh();
        }
        mode_.clear();
        handle_.reset();
        return true;
    }
    if (mode_ == "rubber") {
        ctx->clear_preview();
        const double rx0 = std::min(start_.x, wx), rx1 = std::max(start_.x, wx);
        const double ry0 = std::min(start_.y, wy), ry1 = std::max(start_.y, wy);
        if (rx1 - rx0 > 1e-6 && ry1 - ry0 > 1e-6) {
            std::set<std::string> checked;
            const bool crossing = wx < start_.x;
            for (const cad::EntityPtr& e : ctx->document->entities) {
                if (!ctx->document->is_editable(*e) || checked.count(e->id)) continue;
                const auto group = members(e);
                for (const cad::EntityPtr& m : group) checked.insert(m->id);
                std::vector<bool> matches;
                for (const cad::EntityPtr& m : group) matches.push_back(vp::in_selection_window(*m, rx0, ry0, rx1, ry1, crossing));
                const bool any = std::any_of(matches.begin(), matches.end(), [](bool b) { return b; });
                const bool all = std::all_of(matches.begin(), matches.end(), [](bool b) { return b; });
                if (crossing ? any : all) {
                    for (const cad::EntityPtr& member : group) ctx->document->set_selected(member, true);
                }
            }
            ctx->refresh();
        }
        ctx->status(std::format("{} objeto(s) selecionado(s)", ctx->selection().size()));
        mode_.clear();
        return true;
    }

    if (mode_ == "move") {
        on_move(wx, wy, mods);
        const bool moveu = moved_, copiar = copying_;
        const double tx = last_.x - start_.x, ty = last_.y - start_.y;
        const auto targets = cmd_targets_;
        if (moveu && copiar) {
            if (ctx->canvas) ctx->canvas->clear_translation_preview();
            std::vector<cad::EntityPtr> copias;
            for (const cad::EntityPtr& e : targets) copias.push_back(e->clone());
            for (const cad::EntityPtr& c : copias) c->translate(tx, ty);
            ctx->add(copias, "Arrastar cópia", true);
        } else if (moveu) {
            ctx->translate(targets, tx, ty, "Mover", false);
            if (ctx->canvas) ctx->canvas->commit_translation_preview();
            ctx->refresh();
        } else {
            if (ctx->canvas) ctx->canvas->clear_translation_preview();
            ctx->refresh();
        }
        mode_.clear();
        grab_.reset();
        copying_ = false;
        return true;
    }
    return false;
}

bool SelectTool::on_key(int key) {
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        const auto sel = ctx->selection();
        if (!sel.empty()) {
            ctx->remove(sel);
            ctx->status(std::format("{} objeto(s) excluído(s)", sel.size()));
        }
        return true;
    }
    if (key == Qt::Key_Escape) {
        deactivate();
        ctx->document->clear_selection();
        ctx->refresh();
        return true;
    }
    return false;
}

}  // namespace cnc::tools
