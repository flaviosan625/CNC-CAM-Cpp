// Porte de src/viewport/canvas_2d.py: configuracao, fundo (material e grade) e zoom.
#include "viewport/canvas_2d.hpp"

#include <algorithm>
#include <cmath>

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPen>

#include "ui/theme.hpp"

namespace cnc::vp {

Canvas2D::Canvas2D(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);

    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Atualiza so a regiao alterada; redesenhar a chapa inteira a cada no
    // movimentado fica caro em desenhos importados grandes.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setStyleSheet("border: none;");
    setFocusPolicy(Qt::StrongFocus);
    scene_->setBackgroundBrush(QBrush(QColor(ui::color("canvas"))));

    shortcut_table = shortcuts::load();
    // O ponto azul fica a X pixels do canto: muda com o zoom.
    connect(this, &Canvas2D::view_changed, this, [this] { refresh_selection_handles(false); });

    setMouseTracking(true);
    setAcceptDrops(true);
}

void Canvas2D::remove_item(QGraphicsItem* item) {
    if (!item) return;
    if (item->scene() == scene_) scene_->removeItem(item);
    delete item;
}

void Canvas2D::clear_items(std::vector<QGraphicsItem*>& items) {
    for (QGraphicsItem* item : items) remove_item(item);
    items.clear();
}

// -- configuracao --

void Canvas2D::set_document(cad::Document* doc) {
    clear_measurement();
    clear_layout_preview();
    fit_warning_ids_.clear();
    clear_fit_warning_items();
    document_ = doc;
    snap.document = doc;
    material_ = doc ? &doc->material : nullptr;
    if (doc) {
        material_width = doc->material.width;
        material_height = doc->material.height;
        const double margin = std::max(material_width, material_height) * 3;
        scene_->setSceneRect(-margin, -margin, material_width + 2 * margin,
                             material_height + 2 * margin);
    }
    rebuild_background();
    refresh_entities();
}

void Canvas2D::set_tool(tools::BaseTool* tool) {
    tool_ = tool;
    setCursor(tool ? tool->cursor() : QCursor(Qt::ArrowCursor));
    refresh_selection_handles();
}

void Canvas2D::setup_material(double width, double height) {
    if (document_ == nullptr) {
        own_material_.width = width;
        own_material_.height = height;
    }
    material_width = width;
    material_height = height;
    const double margin = std::max(width, height) * 3;
    scene_->setSceneRect(-margin, -margin, width + 2 * margin, height + 2 * margin);
    rebuild_background();
    refresh_entities();
    fit_view();
}

void Canvas2D::fit_view() {
    const double m = std::max(material_width, material_height) * 0.08;
    fitInView(-m, -m, material_width + 2 * m, material_height + 2 * m, Qt::KeepAspectRatio);
    emit view_changed();
}

void Canvas2D::zoom_by(double factor) {
    scale(factor, factor);
    emit view_changed();
}

void Canvas2D::zoom_selection() {
    if (!document_) return;
    const auto sel = document_->selected_entities();
    if (sel.empty()) {
        fit_view();
        return;
    }
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool first = true;
    for (const cad::EntityPtr& e : sel) {
        const BBox b = e->bounding_box();
        if (first) {
            x0 = b.x0; y0 = b.y0; x1 = b.x1; y1 = b.y1;
            first = false;
        } else {
            x0 = std::min(x0, b.x0); y0 = std::min(y0, b.y0);
            x1 = std::max(x1, b.x1); y1 = std::max(y1, b.y1);
        }
    }
    const double pad = std::max(x1 - x0, y1 - y0) * 0.15 + 1.0;
    const double mh = material_height;
    fitInView(QRectF(x0 - pad, mh - y1 - pad, (x1 - x0) + 2 * pad, (y1 - y0) + 2 * pad), Qt::KeepAspectRatio);
    emit view_changed();
}

double Canvas2D::pick_tolerance() const {
    const double m = transform().m11();
    return m > 1e-9 ? 1.0 / m : 1.0;
}

double Canvas2D::px_per_mm() const {
    const double escala = transform().m11();
    return escala != 0.0 ? std::fabs(escala) : 1.0;
}

void Canvas2D::set_grid_visible(bool visible) {
    if (show_grid == visible) return;
    show_grid = visible;
    rebuild_background();
}

void Canvas2D::reload_shortcuts() { shortcut_table = shortcuts::load(); }

// -- desenho do fundo (material + grade) --

void Canvas2D::rebuild_background() {
    clear_items(bg_items_);

    const double w = material_width, h = material_height;
    const double off = std::max(w, h) * 0.006;

    QGraphicsRectItem* sombra = scene_->addRect(off, off, w, h, QPen(Qt::NoPen), QBrush(QColor(0, 0, 0, 110)));
    sombra->setZValue(-101);
    bg_items_.push_back(sombra);

    QGraphicsRectItem* chapa = scene_->addRect(0, 0, w, h, QPen(QColor(60, 60, 60), 1.0), QBrush(QColor("#252526")));
    chapa->setZValue(-100);
    bg_items_.push_back(chapa);

    if (show_grid && w > 0 && h > 0) {
        const double rough = std::max(w, h) / 16;
        const double power = std::pow(10.0, std::floor(std::log10(rough)));
        // min() do Python fica com o primeiro em caso de empate.
        double step = power * 1;
        for (double n : {2.0, 5.0, 10.0}) {
            const double candidate = power * n;
            if (std::fabs(candidate - rough) < std::fabs(step - rough)) step = candidate;
        }
        const QPen grid_pen(QColor(255, 255, 255, 20), 0);
        const int nx = static_cast<int>(std::ceil(w / step));
        for (int i = 1; i < nx; ++i) {
            QGraphicsLineItem* item = scene_->addLine(i * step, 0, i * step, h, grid_pen);
            item->setZValue(-99.5);
            bg_items_.push_back(item);
        }
        const int ny = static_cast<int>(std::ceil(h / step));
        for (int i = 1; i < ny; ++i) {
            QGraphicsLineItem* item = scene_->addLine(0, i * step, w, i * step, grid_pen);
            item->setZValue(-99.5);
            bg_items_.push_back(item);
        }
    }

    add_margin_rect();

    const double cs = std::max(w, h) * 0.015;
    const Pt origin = material().origin_xy();
    const double origin_y = h - origin.y;
    QPen op(QColor(255, 60, 60), 1.0);
    op.setCosmetic(true);
    for (QGraphicsLineItem* a : {scene_->addLine(origin.x - cs, origin_y, origin.x + cs, origin_y, op),
                                 scene_->addLine(origin.x, origin_y - cs, origin.x, origin_y + cs, op)}) {
        a->setZValue(-98);
        bg_items_.push_back(a);
    }
}

// Linha cinza fraca marcando a folga ate a borda da chapa. Serve para nao programar rente ao corte
// da chapa - a borda real nunca e confiavel. Fica no fundo e nao e selecionavel: e referencia, nao
// desenho, e nao vai parar em nenhum percurso.
void Canvas2D::add_margin_rect() {
    const std::optional<BBox> retangulo = material().margin_rect();
    if (!retangulo) return;
    QPen caneta{QColor(kMarginColor), 1.0};
    caneta.setCosmetic(true);  // 1 px em qualquer zoom: nunca engorda
    QGraphicsRectItem* item = scene_->addRect(retangulo->x0, material_height - retangulo->y1,
                                              retangulo->x1 - retangulo->x0, retangulo->y1 - retangulo->y0, caneta,
                                              QBrush(Qt::NoBrush));
    item->setZValue(-99);
    bg_items_.push_back(item);
}

}  // namespace cnc::vp
