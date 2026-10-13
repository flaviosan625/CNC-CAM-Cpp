// Porte de src/viewport/canvas_2d.py: entidades, avisos de encaixe, snap e alcas.
#include <algorithm>
#include <format>

#include <QBrush>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QPainterPath>
#include <QPen>

#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

// -- entidades --

void Canvas2D::refresh_entities() {
    clear_layout_preview();
    clear_fit_warning_items();
    fit_warning_ids_.clear();
    for (EntityItem* item : ent_items_) remove_item(item);
    ent_items_.clear();
    if (!document_) return;
    document_->sync_dimensions();
    const double mh = material_height;
    for (const cad::EntityPtr& e : document_->entities) {
        if (!show_guides && cad::is_a<cad::ConstructionLine>(*e)) continue;
        const cad::Layer* lay = document_->get_layer(e->layer);
        if (lay && !lay->visible) continue;
        const cad::MaterialSetup& mat = document_->material;
        const Rgb layer_color = lay ? lay->color : e->color;
        auto* item = new EntityItem(e, mh, mat.unit_factor(), mat.units, layer_color);
        scene_->addItem(item);
        ent_items_.push_back(item);
    }
    rebuild_fit_warning_items();
    refresh_selection_handles();
}

// Contorna com neon pecas que o otimizador nao conseguiu encaixar.
void Canvas2D::set_fit_warning_entities(const std::set<std::string>& entity_ids) {
    fit_warning_ids_ = entity_ids;
    clear_fit_warning_items();
    rebuild_fit_warning_items();
}

void Canvas2D::clear_fit_warning_items() { clear_items(fit_warning_items_); }

void Canvas2D::rebuild_fit_warning_items() {
    if (!document_ || fit_warning_ids_.empty()) return;
    for (const cad::EntityPtr& entity : document_->entities) {
        if (!fit_warning_ids_.count(entity->id)) continue;
        auto* item = new QGraphicsPathItem(build_path(*entity, material_height));
        QPen pen(QColor("#b6ff00"), 2.6);
        pen.setCosmetic(true);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        item->setPen(pen);
        item->setBrush(Qt::NoBrush);
        item->setZValue(25);
        auto* glow = new QGraphicsDropShadowEffect();
        glow->setBlurRadius(20);
        glow->setOffset(0, 0);
        glow->setColor(QColor(182, 255, 0, 210));
        item->setGraphicsEffect(glow);
        item->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(item);
        fit_warning_items_.push_back(item);
    }
}

// Mostra o resultado proposto sem mexer nos vetores do documento.
void Canvas2D::show_layout_preview(const std::vector<cad::Placement>& placements,
                                   const std::set<std::string>& unfit_ids) {
    clear_layout_preview();
    for (const cad::Placement& placement : placements) {
        for (const cad::EntityPtr& entity : placement.entities) {
            const cad::EntityPtr preview = entity->clone();
            placement.apply(*preview);
            auto* item = new QGraphicsPathItem(build_path(*preview, material_height));
            QPen pen(QColor("#35c8ff"), 1.8);
            pen.setCosmetic(true);
            item->setPen(pen);
            item->setBrush(Qt::NoBrush);
            item->setZValue(30);
            scene_->addItem(item);
            layout_preview_items_.push_back(item);
        }
    }
    set_fit_warning_entities(unfit_ids);
}

void Canvas2D::clear_layout_preview() { clear_items(layout_preview_items_); }

// Vetor sob o ponto, de cima para baixo - o mesmo criterio do clique de selecao, para dois cliques
// nunca pegarem outro vetor.
cad::EntityPtr Canvas2D::entity_at(double wx, double wy) const {
    if (!document_) return nullptr;
    const double tol = pick_tolerance() * kPickPx;
    for (auto it = document_->entities.rbegin(); it != document_->entities.rend(); ++it) {
        if (!document_->is_editable(**it)) continue;
        if (hit_test(**it, wx, wy, tol)) return *it;
    }
    return nullptr;
}

QPoint Canvas2D::entity_anchor(const cad::Entity& entity) const {
    const BBox b = entity.bounding_box();
    const QPointF s = world_to_scene(b.x1, (b.y0 + b.y1) / 2.0);
    return viewport()->mapToGlobal(mapFromScene(s));
}

// -- alcas da selecao --

std::optional<BBox> Canvas2D::selection_bbox() const {
    if (!document_) return std::nullopt;
    const auto sel = document_->selected_entities();
    if (sel.empty()) return std::nullopt;
    BBox out = sel.front()->bounding_box();
    for (size_t i = 1; i < sel.size(); ++i) {
        const BBox c = sel[i]->bounding_box();
        out.x0 = std::min(out.x0, c.x0);
        out.y0 = std::min(out.y0, c.y0);
        out.x1 = std::max(out.x1, c.x1);
        out.y1 = std::max(out.y1, c.y1);
    }
    return out;
}

// Mostra as alcas ao redor da selecao, ou apaga se nao houver. Chamado apos qualquer mudanca de
// selecao, de ferramenta ativa ou de geometria. As alcas so aparecem com a ferramenta de selecao -
// em qualquer outra, atrapalhariam o desenho em andamento.
void Canvas2D::refresh_selection_handles() {
    const std::optional<BBox> bbox = selection_bbox();
    const std::string tool_name = tool_ ? tool_->name() : std::string();
    std::vector<cad::EntityPtr> selected;
    if (document_) selected = document_->selected_entities();
    const bool all_drill = !selected.empty() && std::all_of(selected.begin(), selected.end(), [](const cad::EntityPtr& e) {
        return cad::is_a<cad::DrillPoint>(*e);
    });
    if (!bbox || tool_name != "select" || all_drill) {
        handles.clear();
        return;
    }
    handles.show(*bbox);
}

// Rotulo com o angulo atual, flutuando perto do cursor. Em pixels locais, com
// ItemIgnoresTransformations: fica legivel em qualquer zoom, como as cotas da regua.
void Canvas2D::show_angle_label(double wx, double wy, double graus) {
    clear_angle_label();
    angle_label_ = scene_->addSimpleText(QString::fromStdString(std::format("{:+.1f}°", graus)));
    angle_label_->setBrush(QBrush(QColor("#ff9f43")));
    angle_label_->setZValue(210);
    angle_label_->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    const QPointF s = world_to_scene(wx, wy);
    angle_label_->setPos(s.x() + 14, s.y() - 20);
}

void Canvas2D::clear_angle_label() {
    if (angle_label_) {
        remove_item(angle_label_);
        angle_label_ = nullptr;
    }
}

}  // namespace cnc::vp
