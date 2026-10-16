// Porte de src/viewport/canvas_2d.py: configuracao, fundo (material e grade) e zoom.
#include "viewport/canvas_2d.hpp"

#include <algorithm>
#include <cmath>

#include <QBrush>
#include <QBackingStore>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QScrollBar>

#include "ui/theme.hpp"

namespace cnc::vp {

namespace {

QImage last_painted_viewport(QWidget* viewport) {
    QBackingStore* backing = viewport->backingStore();
    if (!backing) return {};
    auto* frame = dynamic_cast<QImage*>(backing->paintDevice());
    if (!frame || frame->isNull()) return {};

    const qreal dpr = frame->devicePixelRatio();
    const QPoint origin = viewport->mapTo(viewport->window(), QPoint(0, 0));
    const QRect pixels(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
                       qRound(viewport->width() * dpr), qRound(viewport->height() * dpr));
    if (!frame->rect().contains(pixels)) return {};
    return frame->copy(pixels);
}

}  // namespace

Canvas2D::Canvas2D(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    // Em chapas com milhares de pecas, subcontornos menores que um pixel nao
    // contribuem para a imagem. Permanecem na cena para selecao e reaparecem
    // automaticamente ao aproximar.
    scene_->setMinimumRenderSize(0.75);

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

    navigation_preview_timer_ = new QTimer(this);
    navigation_preview_timer_->setSingleShot(true);
    connect(navigation_preview_timer_, &QTimer::timeout, this, &Canvas2D::finish_navigation_preview);

    shortcut_table = shortcuts::load();
    // O ponto azul fica a X pixels do canto: muda com o zoom.
    connect(this, &Canvas2D::view_changed, this, [this] {
        if (navigation_preview_active_) {
            if (navigation_preview_viewport_size_ != viewport()->size()) {
                finish_navigation_preview();
            } else {
                update_navigation_preview_transform();
                navigation_preview_timer_->start(90);
            }
        }
        const qreal scale = transform().m11();
        if (!navigation_preview_active_ && translation_preview_origins_.empty() && bulk_selection_proxy_persistent_ &&
            std::fabs(scale - bulk_selection_proxy_scale_) > 1e-6) {
            clear_bulk_selection_proxy();
        }
        if (!navigation_preview_active_) refresh_selection_handles(false);
    });

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
        const double margin = std::max({material_width, material_height, 2000.0}) * 5000.0;
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
    const double margin = std::max({width, height, 2000.0}) * 5000.0;
    scene_->setSceneRect(-margin, -margin, width + 2 * margin, height + 2 * margin);
    rebuild_background();
    refresh_entities();
    fit_view();
}

void Canvas2D::ensure_infinite_scene_rect() {
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const QRectF current = scene_->sceneRect();
    const double pad = std::max({visible.width() * 8.0, visible.height() * 8.0, 5000000.0});
    const double margin_check = pad * 0.25;

    if (!current.isValid() ||
        visible.left() <= current.left() + margin_check ||
        visible.right() >= current.right() - margin_check ||
        visible.top() <= current.top() + margin_check ||
        visible.bottom() >= current.bottom() - margin_check) {
        const QPointF scene_center = mapToScene(viewport()->rect().center());
        QRectF new_rect = current.isValid() ? current.united(visible) : visible;
        new_rect.adjust(-pad, -pad, pad, pad);
        scene_->setSceneRect(new_rect);
        centerOn(scene_center);
    }
}

void Canvas2D::pan_by(const QPointF& delta) {
    if (delta.isNull()) return;
    pan_accumulator_ += delta;
    const int dx = static_cast<int>(pan_accumulator_.x());
    const int dy = static_cast<int>(pan_accumulator_.y());
    if (dx == 0 && dy == 0) return;
    pan_accumulator_.rx() -= dx;
    pan_accumulator_.ry() -= dy;

    ensure_infinite_scene_rect();

    QScrollBar* hs = horizontalScrollBar();
    QScrollBar* vs = verticalScrollBar();
    hs->setValue(hs->value() - dx);
    vs->setValue(vs->value() - dy);
    emit view_changed();
}

void Canvas2D::fit_view() {
    const double m = std::max(material_width, material_height) * 0.08;
    begin_navigation_preview();
    ensure_infinite_scene_rect();
    fitInView(-m, -m, material_width + 2 * m, material_height + 2 * m, Qt::KeepAspectRatio);
    emit view_changed();
}

void Canvas2D::zoom_by(double factor) {
    begin_navigation_preview();
    ensure_infinite_scene_rect();
    scale(factor, factor);
    ensure_infinite_scene_rect();
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
    begin_navigation_preview();
    fitInView(QRectF(x0 - pad, mh - y1 - pad, (x1 - x0) + 2 * pad, (y1 - y0) + 2 * pad), Qt::KeepAspectRatio);
    emit view_changed();
}

void Canvas2D::begin_navigation_preview() {
    if (navigation_preview_active_ || !viewport()->isVisible()) return;
    constexpr size_t kMinPreviewEntities = 128;
    constexpr int kMinPreviewPathElements = 4096;
    if (ent_items_.size() < kMinPreviewEntities) {
        int path_elements = 0;
        for (const EntityItem* item : ent_items_) {
            if (item) path_elements += item->path().elementCount();
            if (path_elements >= kMinPreviewPathElements) break;
        }
        for (const QGraphicsItem* item : tp_items_) {
            if (path_elements >= kMinPreviewPathElements) break;
            if (const auto* path = dynamic_cast<const QGraphicsPathItem*>(item))
                path_elements += path->path().elementCount();
        }
        // Um unico vetor ou percurso pode conter milhares de trechos.
        if (path_elements < kMinPreviewPathElements) return;
    }
    QImage image = last_painted_viewport(viewport());
    if (image.isNull()) image = viewport()->grab().toImage();
    if (image.isNull()) return;

    navigation_preview_image_ = std::move(image);
    navigation_preview_viewport_size_ = viewport()->size();
    navigation_preview_capture_viewport_transform_ = viewportTransform();
    navigation_preview_transform_.reset();
    navigation_preview_active_ = true;
    // O modo indireto chama drawItems(), que ignora a pintura dos vetores
    // enquanto a imagem anterior acompanha a roda. Evita alterar a visibilidade
    // de milhares de itens e reconstruir o indice da cena ao terminar o zoom.
    setOptimizationFlag(QGraphicsView::IndirectPainting, true);
    navigation_preview_timer_->start(90);
    viewport()->update();
}

void Canvas2D::finish_navigation_preview() {
    if (navigation_preview_timer_) navigation_preview_timer_->stop();
    if (!navigation_preview_active_) return;

    const bool zoom_changed = bulk_selection_proxy_persistent_ &&
                              std::fabs(transform().m11() - bulk_selection_proxy_scale_) > 1e-6;
    if (zoom_changed) clear_bulk_selection_proxy();
    navigation_preview_image_ = {};
    navigation_preview_active_ = false;
    setOptimizationFlag(QGraphicsView::IndirectPainting, false);
    viewport()->update();
}

void Canvas2D::drawItems(QPainter* painter, int numItems, QGraphicsItem* items[],
                         const QStyleOptionGraphicsItem options[]) {
    if (!navigation_preview_active_)
        QGraphicsView::drawItems(painter, numItems, items, options);
}

void Canvas2D::update_navigation_preview_transform() {
    bool invertible = false;
    const QTransform screen_to_scene = navigation_preview_capture_viewport_transform_.inverted(&invertible);
    if (!invertible) return;

    const QSize size = navigation_preview_viewport_size_;
    const QPolygonF source{QPointF(0, 0), QPointF(size.width(), 0),
                           QPointF(size.width(), size.height()), QPointF(0, size.height())};
    QPolygonF destination;
    destination.reserve(source.size());
    const QTransform current = viewportTransform();
    for (const QPointF& point : source)
        destination.push_back(current.map(screen_to_scene.map(point)));

    QTransform mapped;
    if (QTransform::quadToQuad(source, destination, mapped)) navigation_preview_transform_ = mapped;
}

void Canvas2D::drawForeground(QPainter* painter, const QRectF& rect) {
    if (!navigation_preview_active_) {
        QGraphicsView::drawForeground(painter, rect);
        return;
    }

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setWorldTransform(navigation_preview_transform_);
    painter->drawImage(QRectF(QPointF(0, 0), QSizeF(navigation_preview_viewport_size_)),
                       navigation_preview_image_, QRectF(navigation_preview_image_.rect()));
    painter->restore();
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
