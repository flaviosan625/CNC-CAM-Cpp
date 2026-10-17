#include "cad/relations.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "cad/geometry.hpp"
#include "tools/base.hpp"

namespace cnc::cad {

namespace {

Pt get_entity_center(const EntityPtr& e) {
    if (!e) return {0.0, 0.0};
    if (auto c = std::dynamic_pointer_cast<Circle>(e)) {
        return {c->cx, c->cy};
    }
    if (auto a = std::dynamic_pointer_cast<Arc>(e)) {
        return {a->cx, a->cy};
    }
    return e->center();
}

class RelationSnapshotCommand : public Command {
public:
    RelationSnapshotCommand(Document& doc, std::vector<EntityPtr> entities,
                            std::function<void()> apply_fn, std::string label)
        : doc_(doc), ents_(std::move(entities)), apply_(std::move(apply_fn)), label_(std::move(label)) {
        before_.reserve(ents_.size());
        for (const auto& e : ents_) {
            before_.push_back(e->copy());
        }
    }

    void execute() override {
        if (!has_after_) {
            apply_();
            after_.reserve(ents_.size());
            for (const auto& e : ents_) {
                after_.push_back(e->copy());
            }
            has_after_ = true;
        } else {
            restore(after_);
        }
        doc_.modified = true;
    }

    void undo() override {
        restore(before_);
        doc_.modified = true;
    }

    std::string description() const override { return label_; }

private:
    void restore(const std::vector<EntityPtr>& states) {
        for (size_t i = 0; i < ents_.size() && i < states.size(); ++i) {
            ents_[i]->restore_from(*states[i]);
        }
    }

    Document& doc_;
    std::vector<EntityPtr> ents_;
    std::vector<EntityPtr> before_;
    std::vector<EntityPtr> after_;
    std::function<void()> apply_;
    std::string label_;
    bool has_after_ = false;
};

}  // namespace

bool align_concentric(const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return false;
    const Pt c0 = get_entity_center(selection.front());

    for (size_t i = 1; i < selection.size(); ++i) {
        const auto& e = selection[i];
        if (!e) continue;
        const Pt ce = get_entity_center(e);
        e->translate(c0.x - ce.x, c0.y - ce.y);
        e->touch();
    }
    return true;
}

bool make_equal_size(const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return false;
    const auto& primary = selection.front();
    if (!primary) return false;

    if (auto c0 = std::dynamic_pointer_cast<Circle>(primary)) {
        const double r = c0->radius;
        for (size_t i = 1; i < selection.size(); ++i) {
            auto& e = selection[i];
            if (auto c = std::dynamic_pointer_cast<Circle>(e)) {
                c->radius = r;
                c->touch();
            } else if (auto a = std::dynamic_pointer_cast<Arc>(e)) {
                a->radius = r;
                a->touch();
            } else if (auto rect = std::dynamic_pointer_cast<Rectangle>(e)) {
                const double old_w = rect->width;
                const double old_h = rect->height;
                rect->x += (old_w - r * 2.0) / 2.0;
                rect->y += (old_h - r * 2.0) / 2.0;
                rect->width = r * 2.0;
                rect->height = r * 2.0;
                rect->touch();
            } else {
                const BBox b = e->bounding_box();
                const double max_dim = std::max(b.x1 - b.x0, b.y1 - b.y0);
                if (max_dim > 1e-6) {
                    const double s = (r * 2.0) / max_dim;
                    const Pt cen = e->center();
                    e->scale(s, s, cen.x, cen.y);
                }
            }
        }
        return true;
    }

    if (auto a0 = std::dynamic_pointer_cast<Arc>(primary)) {
        const double r = a0->radius;
        for (size_t i = 1; i < selection.size(); ++i) {
            auto& e = selection[i];
            if (auto c = std::dynamic_pointer_cast<Circle>(e)) {
                c->radius = r;
                c->touch();
            } else if (auto a = std::dynamic_pointer_cast<Arc>(e)) {
                a->radius = r;
                a->touch();
            } else if (auto rect = std::dynamic_pointer_cast<Rectangle>(e)) {
                const double old_w = rect->width;
                const double old_h = rect->height;
                rect->x += (old_w - r * 2.0) / 2.0;
                rect->y += (old_h - r * 2.0) / 2.0;
                rect->width = r * 2.0;
                rect->height = r * 2.0;
                rect->touch();
            } else {
                const BBox b = e->bounding_box();
                const double max_dim = std::max(b.x1 - b.x0, b.y1 - b.y0);
                if (max_dim > 1e-6) {
                    const double s = (r * 2.0) / max_dim;
                    const Pt cen = e->center();
                    e->scale(s, s, cen.x, cen.y);
                }
            }
        }
        return true;
    }

    if (auto r0 = std::dynamic_pointer_cast<Rectangle>(primary)) {
        const double w = r0->width;
        const double h = r0->height;
        for (size_t i = 1; i < selection.size(); ++i) {
            auto& e = selection[i];
            if (auto rect = std::dynamic_pointer_cast<Rectangle>(e)) {
                const double old_w = rect->width;
                const double old_h = rect->height;
                rect->x += (old_w - w) / 2.0;
                rect->y += (old_h - h) / 2.0;
                rect->width = w;
                rect->height = h;
                rect->touch();
            } else if (auto c = std::dynamic_pointer_cast<Circle>(e)) {
                c->radius = std::min(w, h) / 2.0;
                c->touch();
            } else {
                const BBox b = e->bounding_box();
                const double cur_w = b.x1 - b.x0;
                const double cur_h = b.y1 - b.y0;
                if (cur_w > 1e-6 && cur_h > 1e-6) {
                    const Pt cen = e->center();
                    e->scale(w / cur_w, h / cur_h, cen.x, cen.y);
                }
            }
        }
        return true;
    }

    // Caso geral: igualar à bounding box da primária
    const BBox b0 = primary->bounding_box();
    const double w0 = b0.x1 - b0.x0;
    const double h0 = b0.y1 - b0.y0;
    for (size_t i = 1; i < selection.size(); ++i) {
        auto& e = selection[i];
        if (auto c = std::dynamic_pointer_cast<Circle>(e)) {
            c->radius = std::min(w0, h0) / 2.0;
            c->touch();
        } else {
            const BBox be = e->bounding_box();
            const double we = be.x1 - be.x0;
            const double he = be.y1 - be.y0;
            if (we > 1e-6 && he > 1e-6) {
                const Pt cen = e->center();
                e->scale(w0 / we, h0 / he, cen.x, cen.y);
            }
        }
    }
    return true;
}

bool align_center_horizontal(const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return false;
    const double c0_x = get_entity_center(selection.front()).x;

    for (size_t i = 1; i < selection.size(); ++i) {
        const auto& e = selection[i];
        if (!e) continue;
        const double ce_x = get_entity_center(e).x;
        e->translate(c0_x - ce_x, 0.0);
        e->touch();
    }
    return true;
}

bool align_center_vertical(const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return false;
    const double c0_y = get_entity_center(selection.front()).y;

    for (size_t i = 1; i < selection.size(); ++i) {
        const auto& e = selection[i];
        if (!e) continue;
        const double ce_y = get_entity_center(e).y;
        e->translate(0.0, c0_y - ce_y);
        e->touch();
    }
    return true;
}

bool make_tangent(const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return false;
    const auto& e1 = selection.front();

    for (size_t i = 1; i < selection.size(); ++i) {
        const auto& e2 = selection[i];
        if (!e1 || !e2) continue;

        auto c1 = std::dynamic_pointer_cast<Circle>(e1);
        auto c2 = std::dynamic_pointer_cast<Circle>(e2);
        auto l1 = std::dynamic_pointer_cast<Line>(e1);
        auto l2 = std::dynamic_pointer_cast<Line>(e2);

        // Caso A: Círculo + Linha
        if ((c1 && l2) || (l1 && c2)) {
            auto circ = c1 ? c1 : c2;
            auto ln = l1 ? l1 : l2;
            const double lx = ln->x2 - ln->x1;
            const double ly = ln->y2 - ln->y1;
            const double len = std::hypot(lx, ly);
            if (len > 1e-9) {
                const double nx = -ly / len;
                const double ny = lx / len;
                const double dist = (circ->cx - ln->x1) * nx + (circ->cy - ln->y1) * ny;
                const double target = (dist >= 0.0 ? circ->radius : -circ->radius);
                const double delta = target - dist;
                if (c2) {
                    circ->translate(nx * delta, ny * delta);
                    circ->touch();
                } else {
                    ln->translate(-nx * delta, -ny * delta);
                    ln->touch();
                }
            }
            continue;
        }

        // Caso B: Círculo + Círculo
        if (c1 && c2) {
            double vx = c2->cx - c1->cx;
            double vy = c2->cy - c1->cy;
            double d = std::hypot(vx, vy);
            if (d < 1e-9) {
                vx = 1.0;
                vy = 0.0;
                d = 1.0;
            }
            const double ux = vx / d;
            const double uy = vy / d;
            const double target_d = c1->radius + c2->radius;
            const double delta = target_d - d;
            c2->translate(ux * delta, uy * delta);
            c2->touch();
            continue;
        }

        // Caso C: Geral (bounding boxes)
        const Pt p1 = e1->center();
        const Pt p2 = e2->center();
        const BBox b1 = e1->bounding_box();
        const BBox b2 = e2->bounding_box();
        double vx = p2.x - p1.x;
        double vy = p2.y - p1.y;
        double d = std::hypot(vx, vy);
        if (d < 1e-9) {
            vx = 1.0;
            vy = 0.0;
            d = 1.0;
        }
        const double ux = vx / d;
        const double uy = vy / d;
        const double r1 = std::max(b1.x1 - b1.x0, b1.y1 - b1.y0) / 2.0;
        const double r2 = std::max(b2.x1 - b2.x0, b2.y1 - b2.y0) / 2.0;
        const double delta = (r1 + r2) - d;
        e2->translate(ux * delta, uy * delta);
        e2->touch();
    }
    return true;
}

bool quick_group_or_join(Document& doc, const std::vector<EntityPtr>& selection,
                         UndoStack* stack, double tolerance) {
    if (selection.size() < 2) return false;

    // Verificar se há ao menos 2 vetores abertos que possam ser unidos
    std::vector<EntityPtr> open_vecs;
    for (const auto& e : selection) {
        if (!e->closed()) {
            open_vecs.push_back(e);
        }
    }

    if (open_vecs.size() >= 2 && open_vecs.size() == selection.size()) {
        const auto joined = join_open_vectors(open_vecs, tolerance);
        if (!joined.empty() && joined.size() < open_vecs.size()) {
            for (const auto& e : joined) e->selected = true;
            if (stack) {
                stack->push(std::make_shared<tools::ReplaceCommand>(doc, open_vecs, joined, "Unir vetores"));
            } else {
                for (const auto& e : open_vecs) doc.remove_entity(e->id);
                for (const auto& e : joined) doc.add_entity(e);
            }
            return true;
        }
    }

    // Caso não una, agrupa os vetores selecionados
    const std::string gid = new_uuid_hex();
    if (stack) {
        stack->push(std::make_shared<tools::TransformCommand>(
            doc, selection, [gid](Entity& e) { e.group = gid; }, "Agrupar"));
    } else {
        for (const auto& e : selection) {
            e->group = gid;
            e->touch();
        }
    }
    return true;
}

void apply_concentric_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return;
    auto apply_fn = [selection]() { align_concentric(selection); };
    if (stack) {
        stack->push(std::make_shared<RelationSnapshotCommand>(doc, selection, apply_fn, "Concêntrico"));
    } else {
        apply_fn();
    }
}

void apply_equal_size_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return;
    auto apply_fn = [selection]() { make_equal_size(selection); };
    if (stack) {
        stack->push(std::make_shared<RelationSnapshotCommand>(doc, selection, apply_fn, "Igualar tamanho"));
    } else {
        apply_fn();
    }
}

void apply_align_center_h_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return;
    auto apply_fn = [selection]() { align_center_horizontal(selection); };
    if (stack) {
        stack->push(std::make_shared<RelationSnapshotCommand>(doc, selection, apply_fn, "Alinhar centro horizontal"));
    } else {
        apply_fn();
    }
}

void apply_align_center_v_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return;
    auto apply_fn = [selection]() { align_center_vertical(selection); };
    if (stack) {
        stack->push(std::make_shared<RelationSnapshotCommand>(doc, selection, apply_fn, "Alinhar centro vertical"));
    } else {
        apply_fn();
    }
}

void apply_tangent_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection) {
    if (selection.size() < 2) return;
    auto apply_fn = [selection]() { make_tangent(selection); };
    if (stack) {
        stack->push(std::make_shared<RelationSnapshotCommand>(doc, selection, apply_fn, "Tornar tangente"));
    } else {
        apply_fn();
    }
}

void apply_quick_group_or_join_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection,
                                       double tolerance) {
    if (selection.size() < 2) return;
    quick_group_or_join(doc, selection, stack, tolerance);
}

}  // namespace cnc::cad
