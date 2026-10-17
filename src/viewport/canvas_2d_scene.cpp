// Porte de src/viewport/canvas_2d.py: entidades, avisos de encaixe, snap e alcas.
#include <algorithm>
#include <cmath>
#include <format>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QTransform>
#include <QGraphicsDropShadowEffect>

#include "cad/relations.hpp"
#include "ui/contextual_hud.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

namespace {

}  // namespace

// -- entidades --

void Canvas2D::preview_translation(const std::vector<cad::EntityPtr>& entities, double dx, double dy) {
    if (translation_preview_origins_.empty()) {
        translation_preview_reusing_persistent_proxy_ = false;
        translation_preview_origins_.reserve(entities.size());
        for (const auto& entity : entities) {
            const auto found = ent_item_index_.find(entity.get());
            if (found == ent_item_index_.end()) continue;
            translation_preview_origins_.emplace_back(found->second, found->second->pos());
        }
        if (translation_preview_origins_.empty()) return;
        handles.clear();

        translation_preview_reusing_persistent_proxy_ =
            bulk_selection_proxy_persistent_ && translation_preview_pixmap_ &&
            translation_preview_origins_.size() == bulk_selection_proxy_items_.size();
        if (translation_preview_reusing_persistent_proxy_) {
            for (const auto& [item, origin] : translation_preview_origins_) {
                (void)origin;
                if (std::find(bulk_selection_proxy_items_.begin(), bulk_selection_proxy_items_.end(), item) ==
                    bulk_selection_proxy_items_.end()) {
                    translation_preview_reusing_persistent_proxy_ = false;
                    break;
                }
            }
        }
        if (bulk_selection_proxy_persistent_ && !translation_preview_reusing_persistent_proxy_)
            clear_bulk_selection_proxy();

        size_t path_elements = 0;
        for (const auto& [item, origin] : translation_preview_origins_) {
            (void)origin;
            path_elements += static_cast<size_t>(std::max(0, item->path().elementCount()));
        }
        // Em selecoes massivas, uma imagem pequena de caixas mantem o arrasto leve. A geometria
        // vetorial real permanece no documento e volta a aparecer quando a selecao ou o zoom muda.
        translation_preview_proxy_active_ = translation_preview_reusing_persistent_proxy_ ||
                                             translation_preview_origins_.size() >= 128 || path_elements >= 24000;
        if (translation_preview_proxy_active_ && !translation_preview_reusing_persistent_proxy_) {
            QRectF scene_bounds;
            bool first = true;
            for (const auto& [item, origin] : translation_preview_origins_) {
                (void)origin;
                const QRectF bounds = item->mapRectToScene(item->boundingRect());
                scene_bounds = first ? bounds : scene_bounds.united(bounds);
                first = false;
            }

            constexpr int kMaxPreviewSide = 512;
            const double width = std::max(1e-9, scene_bounds.width());
            const double height = std::max(1e-9, scene_bounds.height());
            const double extent = std::max(width, height);
            const int image_width = std::max(1, static_cast<int>(std::lround(kMaxPreviewSide * width / extent)));
            const int image_height = std::max(1, static_cast<int>(std::lround(kMaxPreviewSide * height / extent)));
            QImage preview_image(image_width, image_height, QImage::Format_ARGB32_Premultiplied);
            preview_image.fill(Qt::transparent);
            QPainter painter(&preview_image);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(QColor(255, 140, 0, 220), 1.0));
            painter.setBrush(QColor(255, 140, 0, 64));
            const double scale_x = image_width / width;
            const double scale_y = image_height / height;
            for (const auto& [item, origin] : translation_preview_origins_) {
                (void)origin;
                const QRectF bounds = item->mapRectToScene(item->boundingRect());
                const double x = (bounds.left() - scene_bounds.left()) * scale_x;
                const double y = (bounds.top() - scene_bounds.top()) * scale_y;
                const double w = std::max(1.0, bounds.width() * scale_x);
                const double h = std::max(1.0, bounds.height() * scale_y);
                painter.drawRect(QRectF(x, y, w, h));
            }
            painter.end();

            translation_preview_pixmap_ = new QGraphicsPixmapItem(QPixmap::fromImage(preview_image));
            translation_preview_pixmap_->setTransform(QTransform::fromScale(width / image_width, height / image_height));
            translation_preview_pixmap_->setZValue(10.5);
            translation_preview_pixmap_->setAcceptedMouseButtons(Qt::NoButton);
            translation_preview_pixmap_origin_ = scene_bounds.topLeft();
            translation_preview_pixmap_->setPos(translation_preview_pixmap_origin_);
            scene_->addItem(translation_preview_pixmap_);
            for (const auto& [item, origin] : translation_preview_origins_) {
                (void)origin;
                item->setOpacity(0.0);
            }
        }
    }
    const QPointF offset(dx, -dy);
    translation_preview_offset_ = offset;
    if (translation_preview_proxy_active_) {
        if (translation_preview_pixmap_)
            translation_preview_pixmap_->setPos(translation_preview_pixmap_origin_ + offset);
    } else {
        for (const auto& [item, origin] : translation_preview_origins_) item->setPos(origin + offset);
    }
}

void Canvas2D::clear_translation_preview() {
    if (translation_preview_origins_.empty()) return;
    if (translation_preview_pixmap_ && !translation_preview_reusing_persistent_proxy_) {
        remove_item(translation_preview_pixmap_);
        translation_preview_pixmap_ = nullptr;
    } else if (translation_preview_pixmap_) {
        translation_preview_pixmap_->setPos(translation_preview_pixmap_origin_);
    }
    for (const auto& [item, origin] : translation_preview_origins_) {
        if (!translation_preview_reusing_persistent_proxy_) item->setOpacity(1.0);
        item->setPos(origin);
    }
    if (!bulk_selection_proxy_persistent_) translation_preview_pixmap_origin_ = {};
    translation_preview_origins_.clear();
    translation_preview_offset_ = {};
    translation_preview_proxy_active_ = false;
    translation_preview_reusing_persistent_proxy_ = false;
    refresh_selection_handles(false);
}

void Canvas2D::commit_translation_preview() {
    if (translation_preview_origins_.empty()) return;
    if (translation_preview_proxy_active_) {
        if (!translation_preview_reusing_persistent_proxy_) {
            const QGraphicsScene::ItemIndexMethod index_method = scene_->itemIndexMethod();
            scene_->setItemIndexMethod(QGraphicsScene::NoIndex);
            for (const auto& [item, origin] : translation_preview_origins_) {
                item->setOpacity(0.0);
                item->setPos(origin + translation_preview_offset_);
            }
            scene_->setItemIndexMethod(index_method);
            bulk_selection_proxy_items_.clear();
            bulk_selection_proxy_items_.reserve(translation_preview_origins_.size());
            for (const auto& [item, origin] : translation_preview_origins_) {
                (void)origin;
                bulk_selection_proxy_items_.push_back(item);
            }
        }
        if (translation_preview_pixmap_) {
            translation_preview_pixmap_origin_ += translation_preview_offset_;
            translation_preview_pixmap_->setPos(translation_preview_pixmap_origin_);
        }
        bulk_selection_proxy_persistent_ = translation_preview_pixmap_ != nullptr;
        bulk_selection_proxy_scale_ = bulk_selection_proxy_persistent_ ? transform().m11() : 0.0;
        preserve_bulk_selection_proxy_on_refresh_ = bulk_selection_proxy_persistent_;
    } else if (translation_preview_pixmap_) {
        remove_item(translation_preview_pixmap_);
        translation_preview_pixmap_ = nullptr;
    }
    for (const auto& [item, origin] : translation_preview_origins_) {
        (void)origin;
        if (!translation_preview_proxy_active_ || !translation_preview_reusing_persistent_proxy_)
            item->adopt_preview_translation();
    }
    if (!bulk_selection_proxy_persistent_) translation_preview_pixmap_origin_ = {};
    translation_preview_origins_.clear();
    translation_preview_offset_ = {};
    translation_preview_proxy_active_ = false;
    translation_preview_reusing_persistent_proxy_ = false;
}

void Canvas2D::clear_bulk_selection_proxy() {
    if (translation_preview_pixmap_) {
        remove_item(translation_preview_pixmap_);
        translation_preview_pixmap_ = nullptr;
    }
    for (EntityItem* item : bulk_selection_proxy_items_) {
        if (item && item->scene() == scene_) {
            item->setOpacity(1.0);
            item->refresh();
        }
    }
    bulk_selection_proxy_items_.clear();
    bulk_selection_proxy_scale_ = 0.0;
    bulk_selection_proxy_persistent_ = false;
    preserve_bulk_selection_proxy_on_refresh_ = false;
}

void Canvas2D::refresh_entities() {
    finish_navigation_preview();
    clear_translation_preview();
    if (preserve_bulk_selection_proxy_on_refresh_)
        preserve_bulk_selection_proxy_on_refresh_ = false;
    else
        clear_bulk_selection_proxy();
    clear_layout_preview();
    clear_fit_warning_items();
    fit_warning_ids_.clear();
    if (!document_) {
        for (EntityItem* item : ent_items_) remove_item(item);
        ent_items_.clear();
        ent_item_index_.clear();
        return;
    }
    document_->sync_dimensions();

    std::vector<cad::EntityPtr> visible;
    visible.reserve(document_->entities.size());
    for (const cad::EntityPtr& e : document_->entities) {
        if (!show_guides && cad::is_a<cad::ConstructionLine>(*e)) continue;
        const cad::Layer* lay = document_->get_layer(e->layer);
        if (lay && !lay->visible) continue;
        visible.push_back(e);
    }
    const size_t selected_count = static_cast<size_t>(std::count_if(visible.begin(), visible.end(), [](const auto& entity) {
        return entity->selected;
    }));
    const bool highlight_each_selected_item = selected_count < 128;

    if (visible.size() == ent_items_.size()) {
        bool match = true;
        for (size_t i = 0; i < visible.size(); ++i) {
            if (ent_items_[i]->entity() != visible[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            constexpr Rgb kDefColor{0.1, 0.1, 0.1};
            auto resolve_col = [&](const cad::EntityPtr& ent, const cad::Layer* lay) -> Rgb {
                const bool custom = (std::fabs(ent->color[0] - kDefColor[0]) > 1e-3 ||
                                     std::fabs(ent->color[1] - kDefColor[1]) > 1e-3 ||
                                     std::fabs(ent->color[2] - kDefColor[2]) > 1e-3);
                if (custom) return ent->color;
                return lay ? lay->color : ent->color;
            };
            for (EntityItem* item : ent_items_) {
                const auto& entity = item->entity();
                const cad::Layer* layer = document_->get_layer(entity->layer);
                item->set_selection_highlight(highlight_each_selected_item);
                item->update_context(material_height, document_->material.unit_factor(),
                                     document_->material.units, resolve_col(entity, layer));
                if (item->opacity() > 0.0) item->refresh();
            }
            rebuild_fit_warning_items();
            refresh_selection_handles();
            return;
        }
    }

    // Keep existing paths and scene items when only one piece is added/deleted.
    // Recreating every item also discarded the cached text outlines and shapes.
    auto reusable = std::move(ent_item_index_);
    ent_item_index_.clear();
    ent_item_index_.reserve(visible.size());
    ent_items_.clear();
    ent_items_.reserve(visible.size());

    const double mh = material_height;
    const cad::MaterialSetup& mat = document_->material;
    constexpr Rgb kDefColor{0.1, 0.1, 0.1};
    auto resolve_col = [&](const cad::EntityPtr& ent, const cad::Layer* lay) -> Rgb {
        const bool custom = (std::fabs(ent->color[0] - kDefColor[0]) > 1e-3 ||
                             std::fabs(ent->color[1] - kDefColor[1]) > 1e-3 ||
                             std::fabs(ent->color[2] - kDefColor[2]) > 1e-3);
        if (custom) return ent->color;
        return lay ? lay->color : ent->color;
    };
    for (const cad::EntityPtr& e : visible) {
        const cad::Layer* lay = document_->get_layer(e->layer);
        const Rgb layer_color = resolve_col(e, lay);
        EntityItem* item = nullptr;
        if (const auto found = reusable.find(e.get()); found != reusable.end()) {
            item = found->second;
            reusable.erase(found);
            item->set_selection_highlight(highlight_each_selected_item);
            item->update_context(mh, mat.unit_factor(), mat.units, layer_color);
            if (item->opacity() > 0.0) item->refresh();
        } else {
            item = new EntityItem(e, mh, mat.unit_factor(), mat.units, layer_color);
            item->set_selection_highlight(highlight_each_selected_item);
            item->refresh();
            scene_->addItem(item);
        }
        ent_items_.push_back(item);
        ent_item_index_.emplace(e.get(), item);
    }
    for (const auto& [entity, item] : reusable) remove_item(item);
    // Preserve document stacking order even after an undo inserts an earlier item.
    for (size_t i = ent_items_.size(); i > 1; --i)
        ent_items_[i - 2]->stackBefore(ent_items_[i - 1]);
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
        const auto cached = ent_item_index_.find(it->get());
        if (cached != ent_item_index_.end()) {
            if (cached->second->hit_test_world(wx, wy, tol)) return *it;
        } else if (hit_test(**it, wx, wy, tol)) {
            return *it;
        }
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
    return selection_bbox_for(document_->selected_entities());
}

std::optional<BBox> Canvas2D::selection_bbox_for(const std::vector<cad::EntityPtr>& sel) const {
    if (sel.empty()) return std::nullopt;
    const auto bounds = [this](const cad::EntityPtr& entity) {
        const auto item = ent_item_index_.find(entity.get());
        return item != ent_item_index_.end() ? item->second->world_bounds() : entity->bounding_box();
    };
    BBox out = bounds(sel.front());
    for (size_t i = 1; i < sel.size(); ++i) {
        const BBox c = bounds(sel[i]);
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
void Canvas2D::refresh_selection_handles(bool notify) {
    const std::string tool_name = tool_ ? tool_->name() : std::string();
    std::vector<cad::EntityPtr> selected;
    if (document_) selected = document_->selected_entities();
    const std::optional<BBox> bbox = selection_bbox_for(selected);
    const bool all_drill = !selected.empty() && std::all_of(selected.begin(), selected.end(), [](const cad::EntityPtr& e) {
        return cad::is_a<cad::DrillPoint>(*e);
    });
    if (!bbox || tool_name != "select" || all_drill) {
        handles.clear();
        hide_contextual_hud();
        if (notify) emit selection_changed();
        return;
    }
    handles.show(*bbox, selected.size() >= 128);
    update_contextual_hud(selected);
    // Also notify previews after edits, undo and programmatic selection changes.
    if (notify) emit selection_changed();
}

void Canvas2D::setup_hud_callbacks() {
    if (!contextual_hud_) return;
    contextual_hud_->set_actions(
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_concentric_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        },
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_equal_size_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        },
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_align_center_h_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        },
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_align_center_v_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        },
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_tangent_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        },
        [this] {
            if (!document_) return;
            const auto sel = document_->selected_entities();
            if (sel.size() >= 2) {
                UndoStack* stack = undo_stack_ ? undo_stack_ : (tool_ && tool_->ctx ? tool_->ctx->undo_stack : nullptr);
                cad::apply_quick_group_or_join_command(*document_, stack, sel);
                refresh_entities();
                refresh_selection_handles();
            }
        }
    );
}

void Canvas2D::update_contextual_hud(const std::vector<cad::EntityPtr>& selection) {
    if (selection.size() < 2 || !tool_ || tool_->name() != "select") {
        hide_contextual_hud();
        return;
    }
    if (!contextual_hud_) {
        contextual_hud_ = new ui::ContextualHudBar(viewport());
        setup_hud_callbacks();
    }
    QPoint cursor_vp = mapFromGlobal(QCursor::pos());
    if (!viewport()->rect().contains(cursor_vp)) {
        const auto bbox = selection_bbox_for(selection);
        if (bbox) {
            const QPointF scene_pt = world_to_scene(bbox->x1, bbox->y1);
            cursor_vp = mapFromScene(scene_pt);
        } else {
            cursor_vp = viewport()->rect().center();
        }
    }
    contextual_hud_->show_at(cursor_vp, selection);
}

void Canvas2D::hide_contextual_hud() {
    if (contextual_hud_) {
        contextual_hud_->hide_hud();
    }
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
