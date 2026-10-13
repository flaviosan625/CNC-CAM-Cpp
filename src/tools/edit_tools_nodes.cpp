// Porte de NodeEditTool (src/tools/edit_tools.py).
#include <algorithm>
#include <cmath>
#include <limits>

#include "tools/edit_tools.hpp"

namespace cnc::tools {

namespace {

// Distancia do ponto (px, py) ao segmento a-b.
double seg_d(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double ll = dx * dx + dy * dy;
    if (ll < 1e-12) return hypot(px - ax, py - ay);
    const double t = std::max(0.0, std::min(1.0, ((px - ax) * dx + (py - ay) * dy) / ll));
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

}  // namespace

std::string NodeEditTool::hint() const {
    return "Clique num vetor para ver os nós · arraste para mover · duplo-clique insere · Del remove";
}

void NodeEditTool::activate() {
    BaseTool::activate();
    const auto sel = ctx->selection();
    if (!sel.empty()) load(sel.front());
}

void NodeEditTool::deactivate() {
    if (idx_) pts_ = clean_;
    commit();
    target_.reset();
    pts_.clear();
    idx_.reset();
    selected_node_.reset();
    BaseTool::deactivate();
}

void NodeEditTool::load(const cad::EntityPtr& e) {
    commit();
    if (cad::is_a<cad::Text>(*e)) {
        ctx->status("Converta o texto em curvas antes de editar seus nós");
        return;
    }
    target_ = e;
    pts_ = e->to_points(96);
    clean_ = pts_;
    idx_.reset();
    selected_node_.reset();
    show();
}

bool NodeEditTool::grab_near(double wx, double wy, double tol) {
    for (size_t i = 0; i < pts_.size(); ++i) {
        if (hypot(wx - pts_[i].x, wy - pts_[i].y) <= tol) {
            idx_ = i;
            selected_node_ = i;
            return true;
        }
    }
    return false;
}

void NodeEditTool::show() {
    if (!pts_.empty()) {
        const bool closed = target_ ? target_->closed() : false;
        ctx->preview(pts_, closed, true);
    }
}

bool NodeEditTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;

    if (!pts_.empty() && grab_near(wx, wy, tol)) return true;

    cad::EntityPtr hit;
    double best = tol;
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!ctx->document->is_editable(**it) || cad::is_a<cad::Text>(**it)) continue;
        const double d = (*it)->distance_to(wx, wy);
        if (d < best) {
            hit = *it;
            best = d;
        }
    }
    if (hit) {
        ctx->document->clear_selection();
        hit->selected = true;
        ctx->refresh();
        load(hit);
        grab_near(wx, wy, tol);
    } else {
        commit();
        ctx->clear_preview();
        target_.reset();
        pts_.clear();
    }
    return true;
}

bool NodeEditTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    hover_ = {wx, wy};
    if (!idx_) return false;
    pts_[*idx_] = {wx, wy};
    show();
    return true;
}

bool NodeEditTool::on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!idx_ || button != Qt::LeftButton) return false;
    pts_[*idx_] = {wx, wy};
    idx_.reset();
    commit();
    return true;
}

bool NodeEditTool::on_double_click(double wx, double wy) {
    if (pts_.empty() || !target_) return false;
    std::optional<size_t> best_i;
    double best_d = std::numeric_limits<double>::infinity();
    const size_t n = pts_.size();
    const size_t count = target_->closed() ? n : n - 1;
    for (size_t i = 0; i < count; ++i) {
        const Pt a = pts_[i];
        const Pt b = pts_[(i + 1) % n];
        const double d = seg_d(wx, wy, a.x, a.y, b.x, b.y);
        if (d < best_d) {
            best_d = d;
            best_i = i;
        }
    }
    const double tolerance = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;
    if (best_i && best_d <= tolerance) {
        pts_.insert(pts_.begin() + static_cast<long>(*best_i) + 1, Pt{wx, wy});
        selected_node_ = *best_i + 1;
        show();
        commit();
        ctx->status("Nó inserido");
    }
    return true;
}

bool NodeEditTool::on_key(int key) {
    if ((key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_D) && !pts_.empty()) {
        std::optional<size_t> idx = selected_node_;
        if (key == Qt::Key_D) {
            const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 8;
            size_t nearest = 0;
            double nearest_d = dist(pts_[0], hover_);
            for (size_t i = 1; i < pts_.size(); ++i) {
                const double d = dist(pts_[i], hover_);
                if (d < nearest_d) {
                    nearest_d = d;
                    nearest = i;
                }
            }
            idx = nearest;
            if (nearest_d > tol) idx.reset();
        }
        const size_t minimum = (target_ && target_->closed()) ? 3 : 2;
        if (idx && pts_.size() > minimum) {
            pts_.erase(pts_.begin() + static_cast<long>(*idx));
            selected_node_.reset();
            show();
            commit();
            ctx->status("Nó removido");
        } else {
            ctx->status("Selecione um nó removível");
        }
        return true;
    }
    return BaseTool::on_key(key);
}

void NodeEditTool::commit() {
    if (!target_ || pts_.empty()) return;
    if (pts_ == clean_) return;
    const cad::EntityPtr old = target_;
    auto neu = std::make_shared<cad::Polyline>();
    neu->points = pts_;
    neu->is_closed = old->closed();
    neu->layer = old->layer;
    neu->color = old->color;
    neu->group = old->group;
    neu->selected = true;
    ctx->replace({old}, {neu}, "Editar nós");
    target_ = neu;
    pts_ = neu->points;
    clean_ = pts_;
}

}  // namespace cnc::tools
