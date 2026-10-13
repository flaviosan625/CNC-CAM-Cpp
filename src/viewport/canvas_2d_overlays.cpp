// Porte de src/viewport/canvas_2d.py: snap, percursos, pontes, preview e medicao.
#include <algorithm>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPen>
#include <QTimer>

#include "ui/theme.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

// -- snap --

// Corrige o ponto para o ponto notavel mais proximo, se houver. Segurar Shift desliga o snap
// momentaneamente, como no Aspire - e o escape para quando o ima atrapalha em vez de ajudar.
Pt Canvas2D::apply_snap(double wx, double wy, std::optional<Qt::KeyboardModifiers> mods,
                        const std::set<std::string>& exclude) {
    if (mods && (*mods & Qt::ShiftModifier)) {
        clear_snap_marker();
        return {wx, wy};
    }
    const std::optional<SnapResult> resultado = snap.snap(wx, wy, px_per_mm(), exclude);
    show_snap_marker(resultado);
    if (!resultado) return {wx, wy};
    // Selecionar nao e desenhar. A marca com o nome do ponto - "no", "centro", "ponto medio" -
    // continua aparecendo, porque e por ela que se enxerga onde pegar a peca. O que nao pode e
    // puxar o clique para la: isso desloca o ponto de partida do arrasto e faz errar a peca que
    // esta logo embaixo do cursor.
    if (tool_ && !tool_->cursor_snap()) return {wx, wy};
    return {resultado->x, resultado->y};
}

// Desenha a marca no ponto capturado, com o nome do tipo.
void Canvas2D::show_snap_marker(const std::optional<SnapResult>& resultado) {
    clear_snap_marker();
    if (!resultado) return;

    const QPointF cena = world_to_scene(resultado->x, resultado->y);
    // O caminho usa pixels locais: a marca permanece pequena em qualquer zoom.
    const double x = 0.0, y = 0.0, lado = 4.0;
    QPainterPath caminho;
    switch (resultado->kind) {
        case SnapKind::Node:
            caminho.addRect(x - lado, y - lado, lado * 2, lado * 2);
            break;
        case SnapKind::Center:
            caminho.addEllipse(QPointF(x, y), lado, lado);
            caminho.moveTo(x - lado, y);
            caminho.lineTo(x + lado, y);
            caminho.moveTo(x, y - lado);
            caminho.lineTo(x, y + lado);
            break;
        case SnapKind::Midpoint:
            caminho.moveTo(x, y - lado);
            caminho.lineTo(x + lado, y + lado);
            caminho.lineTo(x - lado, y + lado);
            caminho.closeSubpath();
            break;
        case SnapKind::Intersection:
            caminho.moveTo(x - lado, y - lado);
            caminho.lineTo(x + lado, y + lado);
            caminho.moveTo(x + lado, y - lado);
            caminho.lineTo(x - lado, y + lado);
            break;
        case SnapKind::OnSpan:
            caminho.moveTo(x, y - lado);
            caminho.lineTo(x + lado, y);
            caminho.lineTo(x, y + lado);
            caminho.lineTo(x - lado, y);
            caminho.closeSubpath();
            break;
        default:  // grelha e material
            caminho.moveTo(x - lado, y);
            caminho.lineTo(x + lado, y);
            caminho.moveTo(x, y - lado);
            caminho.lineTo(x, y + lado);
            break;
    }

    const QColor cor(kSnapColor);
    QPen caneta(cor, 1.5);
    caneta.setCosmetic(true);  // tamanho fixo em qualquer zoom
    snap_item_ = new QGraphicsPathItem(caminho);
    snap_item_->setPen(caneta);
    snap_item_->setBrush(QBrush(QColor(255, 159, 67, 60)));
    snap_item_->setZValue(100);
    snap_item_->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    snap_item_->setPos(cena);
    scene_->addItem(snap_item_);

    if (snap.settings.show_label) {
        snap_text_ = scene_->addSimpleText(QString::fromUtf8(snap_label(resultado->kind)));
        snap_text_->setBrush(QBrush(cor));
        snap_text_->setZValue(101);
        snap_text_->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        snap_text_->setPos(cena);
        snap_text_->setTransform(snap_text_->transform().translate(7, -16));
    }
}

void Canvas2D::clear_snap_marker() {
    remove_item(snap_item_);
    remove_item(snap_text_);
    snap_item_ = nullptr;
    snap_text_ = nullptr;
}

// -- percursos de usinagem --

QPen Canvas2D::tp_pen(const QColor& color, double width, bool dashed) {
    QPen pen(color, width);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (dashed) {
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({4, 4});
    }
    return pen;
}

// Desenha os percursos: corte em cor cheia, deslocamento tracejado. Os percursos ficam atras dos
// vetores para nao atrapalhar o clique de selecao, e o deslocamento rapido fica visivel de
// proposito - e onde se enxerga a fresa atravessando onde nao devia.
void Canvas2D::refresh_toolpaths(const std::vector<const cam::Toolpath*>& toolpaths) {
    clear_items(tp_items_);
    for (size_t index = 0; index < toolpaths.size(); ++index) {
        const cam::Toolpath& tp = *toolpaths[index];
        if (!tp.visible || tp.moves.empty()) continue;
        const QColor color(kToolpathColors[index % kToolpathColors.size()]);
        if (tp.kind == cam::ToolpathType::Drill) {
            std::vector<Pt> centers;
            for (const cam::Move& move : tp.moves) {
                const Pt point{py_round(move.x, 7), py_round(move.y, 7)};
                if (move.kind == cam::MoveKind::Plunge && std::find(centers.begin(), centers.end(), point) == centers.end()) {
                    centers.push_back(point);
                }
            }
            for (const Pt& c : centers) {
                auto* marker = new QGraphicsEllipseItem(-5, -5, 10, 10);
                marker->setPen(tp_pen(color, 2.0, false));
                marker->setBrush(Qt::NoBrush);
                marker->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                marker->setPos(world_to_scene(c.x, c.y));
                marker->setZValue(16);
                marker->setAcceptedMouseButtons(Qt::NoButton);
                marker->setToolTip("Centro do percurso de furação");
                scene_->addItem(marker);
                tp_items_.push_back(marker);
            }
        }
        QPainterPath cut, rapid;
        std::optional<QPointF> prev;
        for (const cam::Move& m : tp.moves) {
            if (m.kind == cam::MoveKind::Dwell) continue;
            const QPointF target = world_to_scene(m.x, m.y);
            if (!prev) {
                cut.moveTo(target);
                rapid.moveTo(target);
            } else {
                QPainterPath& path = m.kind == cam::MoveKind::Rapid ? rapid : cut;
                path.moveTo(*prev);
                path.lineTo(target);
            }
            prev = target;
        }

        const std::pair<const QPainterPath*, QPen> layers[] = {
            {&rapid, tp_pen(QColor("#ef5350"), 1.1, true)},
            {&cut, tp_pen(color, 2.2, false)},
        };
        for (const auto& [path, pen] : layers) {
            if (path->isEmpty()) continue;
            auto* item = new QGraphicsPathItem(*path);
            item->setPen(pen);
            // Canais, faca e keyhole percorrem o proprio vetor. Atras da entidade o percurso
            // existia, mas ficava totalmente oculto. Acima do desenho e abaixo das alcas ele fica
            // legivel sem atrapalhar selecao ou edicao.
            item->setZValue(15);
            item->setAcceptedMouseButtons(Qt::NoButton);
            scene_->addItem(item);
            tp_items_.push_back(item);
        }
    }
}

void Canvas2D::set_manual_tab_markers(const std::vector<Pt>& points) {
    std::vector<TabPlacement> placements;
    for (const Pt& p : points) placements.push_back({"", p.x, p.y, true});
    set_profile_tab_markers(placements, false);
}

void Canvas2D::set_profile_tab_markers(const std::vector<TabPlacement>& placements, bool movable) {
    retire_items(manual_tab_items_);
    retire_items(automatic_tab_items_);

    auto moved = [this](int index, const QPointF& scene_point) {
        const Pt p = scene_to_world(scene_point);
        emit tab_marker_moved(index, p.x, p.y);
    };
    for (size_t index = 0; index < placements.size(); ++index) {
        const TabPlacement& pl = placements[index];
        auto* marker = new TabMarker(static_cast<int>(index), movable ? TabMarker::MovedFn(moved) : TabMarker::MovedFn(), pl.manual);
        marker->setPos(world_to_scene(pl.x, pl.y));
        scene_->addItem(marker);
        (pl.manual ? manual_tab_items_ : automatic_tab_items_).push_back(marker);
    }
}

// A ponte pode estar tratando o proprio evento de mouse quando o app reconstroi os marcadores:
// apagar na hora derrubaria o objeto no meio do metodo. Sai da cena agora e e apagada depois.
void Canvas2D::retire_items(std::vector<QGraphicsItem*>& items) {
    for (QGraphicsItem* item : items) {
        if (item->scene() == scene_) scene_->removeItem(item);
        QTimer::singleShot(0, this, [item] { delete item; });
    }
    items.clear();
}

// -- preview (rubber band) --

void Canvas2D::set_preview(const Points& pts, bool closed, bool nodes) {
    clear_preview();
    if (pts.size() < 2) return;
    QPainterPath path;
    path.moveTo(world_to_scene(pts[0].x, pts[0].y));
    for (size_t i = 1; i < pts.size(); ++i) path.lineTo(world_to_scene(pts[i].x, pts[i].y));
    if (closed) path.closeSubpath();

    preview_ = new QGraphicsPathItem(path);
    QPen pen(QColor(0, 170, 255), 1.4, Qt::DashLine);
    pen.setCosmetic(true);
    preview_->setPen(pen);
    preview_->setBrush(Qt::NoBrush);
    preview_->setZValue(50);
    scene_->addItem(preview_);

    if (nodes) {
        const double r = pick_tolerance() * 3.5;
        QPen np(QColor(0, 120, 215), 1.0);
        np.setCosmetic(true);
        const QBrush nb{QColor(255, 255, 255)};
        for (const Pt& p : pts) {
            const QPointF s = world_to_scene(p.x, p.y);
            QGraphicsEllipseItem* dot = scene_->addEllipse(s.x() - r, s.y() - r, 2 * r, 2 * r, np, nb);
            dot->setZValue(51);
            node_items_.push_back(dot);
        }
    }
}

void Canvas2D::show_dimension_preview(const std::shared_ptr<cad::Dimension>& dimension) {
    clear_preview();
    const cad::MaterialSetup& mat = document_->material;
    const cad::Layer* layer = document_->get_layer(dimension->layer);
    const Rgb layer_color = layer ? layer->color : dimension->color;
    auto* item = new EntityItem(dimension, material_height, mat.unit_factor(), mat.units, layer_color);

    const QColor color("#00aaff");
    QPen pen(color, 1.5);
    pen.setCosmetic(true);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    item->setPen(pen);
    item->setBrush(dimension->style.arrow_style == "closed" ? QBrush(color) : QBrush(Qt::NoBrush));
    if (item->dimension_label()) item->dimension_label()->setBrush(QBrush(color));
    if (item->extension_item()) {
        QPen extension_pen(color, 1.5);
        extension_pen.setCosmetic(true);
        item->extension_item()->setPen(extension_pen);
    }
    item->setZValue(50);
    scene_->addItem(item);
    dimension_preview_ = item;
}

void Canvas2D::clear_preview() {
    remove_item(preview_);
    preview_ = nullptr;
    remove_item(dimension_preview_);
    dimension_preview_ = nullptr;
    clear_items(node_items_);
}

void Canvas2D::set_selection_preview(const Points& pts, bool crossing) {
    set_preview(pts, true);
    if (preview_) {
        QColor color(crossing ? "#159b52" : "#0078d4");
        QPen pen(color, 1.2, crossing ? Qt::DashLine : Qt::SolidLine);
        pen.setCosmetic(true);
        preview_->setPen(pen);
        color.setAlpha(28);
        preview_->setBrush(QBrush(color));
    }
}

Points Canvas2D::construction_line_points(const QString& axis, double position) const {
    const QRectF bounds = scene_->sceneRect();
    if (axis == "vertical") {
        const double y0 = material_height - bounds.bottom();
        const double y1 = material_height - bounds.top();
        return {{position, y0}, {position, y1}};
    }
    return {{bounds.left(), position}, {bounds.right(), position}};
}

void Canvas2D::show_construction_preview(const Points& points) {
    clear_construction_preview();
    QPainterPath path(world_to_scene(points[0].x, points[0].y));
    path.lineTo(world_to_scene(points[1].x, points[1].y));
    construction_preview_ = new QGraphicsPathItem(path);
    QPen pen(QColor("#e0a000"), 1.2, Qt::DashLine);
    pen.setCosmetic(true);
    construction_preview_->setPen(pen);
    construction_preview_->setZValue(50);
    scene_->addItem(construction_preview_);
}

void Canvas2D::clear_construction_preview() {
    remove_item(construction_preview_);
    construction_preview_ = nullptr;
}

// Chamada de medicao do tamanho da tela, sem criar entidade CAD.
void Canvas2D::show_measurement(const Pt& start, const Pt& end, const std::string& text) {
    clear_measurement();
    const QPointF anchor_point = world_to_scene(end.x, end.y);
    QPainterPath line_path(world_to_scene(start.x, start.y));
    line_path.lineTo(anchor_point);
    measurement_line_ = new QGraphicsPathItem(line_path);
    QPen pen(QColor("#0078c8"), 1.4, Qt::DashLine);
    pen.setCosmetic(true);
    measurement_line_->setPen(pen);
    measurement_line_->setZValue(52);
    scene_->addItem(measurement_line_);

    auto* label = new QGraphicsSimpleTextItem(QString::fromStdString(text));
    label->setFont(QFont("Segoe UI", 9));
    label->setBrush(QBrush(QColor("#ffffff")));
    const double padding = 8.0;
    const QRectF size = label->boundingRect();
    const double box_width = size.width() + 2 * padding;
    const double box_height = size.height() + 2 * padding;
    const QPoint view_point = mapFromScene(anchor_point);
    const double left = view_point.x() + box_width + 12 <= viewport()->width() ? 12.0 : -box_width - 12.0;
    const double top = view_point.y() >= box_height + 12 ? -box_height - 12.0 : 12.0;

    QPainterPath box_path;
    box_path.addRoundedRect(QRectF(left, top, box_width, box_height), 5.0, 5.0);
    measurement_box_ = new QGraphicsPathItem(box_path);
    measurement_box_->setPen(QPen(QColor("#4fc3f7"), 1.0));
    measurement_box_->setBrush(QBrush(QColor("#203247")));
    measurement_box_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    measurement_box_->setZValue(53);
    measurement_box_->setPos(anchor_point);
    label->setParentItem(measurement_box_);
    label->setPos(left + padding, top + padding);
    scene_->addItem(measurement_box_);
}

void Canvas2D::clear_measurement() {
    remove_item(measurement_line_);
    remove_item(measurement_box_);  // leva o texto junto: e filho da caixa
    measurement_line_ = nullptr;
    measurement_box_ = nullptr;
}

}  // namespace cnc::vp
