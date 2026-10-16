// Porte de src/viewport/entity_items.py.
#include "viewport/entity_items.hpp"

#include <cmath>

#include <QFont>
#include <QPainterPathStroker>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

#include "ui/theme.hpp"

namespace cnc::vp {

namespace {

const QColor kSelPen(255, 140, 0);
constexpr Rgb kDefaultLayerColor{0.1, 0.1, 0.1};

bool dense_contour(const cad::Entity& entity) {
    const auto* polyline = cad::as<cad::Polyline>(entity);
    return cad::is_a<cad::Text>(entity) || (polyline && polyline->points.size() >= 64);
}

void poly_to_path(QPainterPath& p, const Points& pts, double mh, bool close) {
    if (pts.size() < 2) return;
    p.moveTo(pts[0].x, mh - pts[0].y);
    for (size_t i = 1; i < pts.size(); ++i) p.lineTo(pts[i].x, mh - pts[i].y);
    if (close) p.closeSubpath();
}

QPainterPath segments_path(const std::vector<cad::Seg>& segments, double mh) {
    QPainterPath path;
    for (const auto& [start, end] : segments) {
        path.moveTo(start.x, mh - start.y);
        path.lineTo(end.x, mh - end.y);
    }
    return path;
}

void arrow(QPainterPath& p, Pt tip, Pt tail, double mh, double size, const std::string& style) {
    const double dx = tail.x - tip.x, dy = tail.y - tip.y;
    const double ln = hypot(dx, dy);
    if (ln < 1e-9) return;
    const double ux = dx / ln, uy = -dy / ln;
    const double px = -uy, py = ux;
    const double tx = tip.x, ty = mh - tip.y;
    if (style == "tick") {
        const double along = (ux + px) * size * 0.35;
        const double across = (uy + py) * size * 0.35;
        p.moveTo(tx - along, ty - across);
        p.lineTo(tx + along, ty + across);
    } else if (style == "open") {
        p.moveTo(tx, ty);
        p.lineTo(tx + ux * size + px * size * 0.3, ty + uy * size + py * size * 0.3);
        p.moveTo(tx, ty);
        p.lineTo(tx + ux * size - px * size * 0.3, ty + uy * size - py * size * 0.3);
    } else {
        p.moveTo(tx, ty);
        p.lineTo(tx + ux * size + px * size * 0.28, ty + uy * size + py * size * 0.28);
        p.lineTo(tx + ux * size - px * size * 0.28, ty + uy * size - py * size * 0.28);
        p.closeSubpath();
    }
}

}  // namespace

bool hit_test(const cad::Entity& e, double x, double y, double tolerance) {
    const BBox b = e.bounding_box();
    if (x < b.x0 - tolerance || x > b.x1 + tolerance ||
        y < b.y0 - tolerance || y > b.y1 + tolerance) {
        return false;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(2 * tolerance);
    return stroker.createStroke(build_path(e, 0)).contains(QPointF(x, -y));
}

bool in_selection_window(const cad::Entity& e, double x0, double y0, double x1, double y1, bool crossing) {
    const double min_x = std::min(x0, x1);
    const double max_x = std::max(x0, x1);
    const double min_y = std::min(y0, y1);
    const double max_y = std::max(y0, y1);
    const BBox b = e.bounding_box();

    if (b.x1 < min_x || b.x0 > max_x || b.y1 < min_y || b.y0 > max_y) {
        return false;
    }

    if (!crossing) {
        return b.x0 >= min_x && b.x1 <= max_x && b.y0 >= min_y && b.y1 <= max_y;
    }

    if (b.x0 >= min_x && b.x1 <= max_x && b.y0 >= min_y && b.y1 <= max_y) {
        return true;
    }

    const QPainterPath path = build_path(e, 0.0);
    if (path.isEmpty()) return false;
    QRectF window(min_x, -max_y, max_x - min_x, max_y - min_y);
    const double eps = 1e-7;
    window = window.adjusted(-eps, -eps, eps, eps);

    // O preenchimento de um vetor fechado nao conta como contato com o vetor.
    QPainterPathStroker stroker;
    stroker.setWidth(eps);
    return stroker.createStroke(path).intersects(window);
}

QPainterPath build_path(const cad::Entity& e, double mh) {
    QPainterPath p;

    if (auto* d = cad::as<cad::DrillPoint>(e)) {
        const double radius = 1.2;
        p.addEllipse(QPointF(d->x, mh - d->y), radius * .45, radius * .45);
        p.moveTo(d->x - radius, mh - d->y);
        p.lineTo(d->x + radius, mh - d->y);
        p.moveTo(d->x, mh - d->y - radius);
        p.lineTo(d->x, mh - d->y + radius);
    } else if (auto* l = cad::as<cad::Line>(e)) {
        p.moveTo(l->x1, mh - l->y1);
        p.lineTo(l->x2, mh - l->y2);
    } else if (auto* c = cad::as<cad::Circle>(e)) {
        p.addEllipse(QPointF(c->cx, mh - c->cy), c->radius, c->radius);
    } else if (auto* el = cad::as<cad::Ellipse>(e)) {
        if (std::fabs(el->rotation) < 1e-9) {
            p.addEllipse(QPointF(el->cx, mh - el->cy), el->rx, el->ry);
        } else {
            poly_to_path(p, el->to_points(96), mh, true);
        }
    } else if (auto* r = cad::as<cad::Rectangle>(e)) {
        if (std::fabs(r->rotation) < 1e-9 && r->corner_radius <= 1e-9) {
            p.addRect(QRectF(r->x, mh - r->y - r->height, r->width, r->height));
        } else if (std::fabs(r->rotation) < 1e-9) {
            p.addRoundedRect(QRectF(r->x, mh - r->y - r->height, r->width, r->height), r->corner_radius,
                             r->corner_radius);
        } else {
            poly_to_path(p, r->to_points(96), mh, true);
        }
    } else if (auto* a = cad::as<cad::Arc>(e)) {
        const double rad = a->radius;
        const QRectF rect(a->cx - rad, mh - a->cy - rad, 2 * rad, 2 * rad);
        double span = a->end_angle - a->start_angle;
        if (span <= 0) span += 360.0;
        p.arcMoveTo(rect, a->start_angle);
        p.arcTo(rect, a->start_angle, span);
    } else if (auto* t = cad::as<cad::Text>(e)) {
        for (const Points& poly : t->outline_polygons()) poly_to_path(p, poly, mh, true);
    } else if (auto* dim = cad::as<cad::Dimension>(e)) {
        const auto [main_path, extension_path] = build_dimension_paths(*dim, mh);
        p.addPath(main_path);
        p.addPath(extension_path);
    } else if (auto* pl = cad::as<cad::Polyline>(e)) {
        poly_to_path(p, pl->points, mh, pl->is_closed);
    }
    return p;
}

std::pair<QPainterPath, QPainterPath> build_dimension_paths(const cad::Dimension& dimension, double mh) {
    const std::vector<cad::Seg> segments = dimension.segments();
    std::vector<cad::Seg> extension_segments, main_segments;
    const std::string& kind = dimension.kind_name;
    if (kind == "aligned" || kind == "horizontal" || kind == "vertical" || kind == "angular") {
        extension_segments.assign(segments.begin(), segments.begin() + std::min<size_t>(2, segments.size()));
        if (segments.size() > 2) main_segments.assign(segments.begin() + 2, segments.end());
    } else {
        main_segments = segments;
    }

    QPainterPath main_path = segments_path(main_segments, mh);
    for (const auto& [tip, tail] : dimension.arrow_placements()) {
        arrow(main_path, tip, tail, mh, dimension.style.arrow_size, dimension.style.arrow_style);
    }
    return {main_path, segments_path(extension_segments, mh)};
}

EntityItem::EntityItem(cad::EntityPtr entity, double mh, double unit_factor, std::string units,
                       std::optional<Rgb> layer_color)
    : entity_(std::move(entity)),
      mh_(mh),
      unit_factor_(unit_factor),
      units_(std::move(units)),
      layer_color_(layer_color ? *layer_color : entity_->color) {
    // Static imported contours and outlined labels are expensive to rasterize.
    // Qt keeps a device-resolution image and invalidates it on path/style/zoom
    // changes, while other pieces can be repainted without tracing every node.
    if (dense_contour(*entity_))
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    if (cad::is_a<cad::Dimension>(entity_)) {
        dimension_label_ = new QGraphicsSimpleTextItem(this);
        extension_item_ = new QGraphicsPathItem(this);
        extension_item_->setZValue(-1);
    }
    setZValue(10);
    refresh();
}

void EntityItem::update_context(double mh, double unit_factor, const std::string& units,
                               const Rgb& layer_color) {
    if (unit_factor_ != unit_factor || units_ != units) context_dirty_ = true;
    if (layer_color_ != layer_color) style_dirty_ = true;
    mh_ = mh;
    unit_factor_ = unit_factor;
    units_ = units;
    layer_color_ = layer_color;
}

BBox EntityItem::world_bounds() const {
    if (!cached_bounds_ || bounds_version_ != entity_->version) {
        cached_bounds_ = entity_->bounding_box();
        bounds_version_ = entity_->version;
    }
    return *cached_bounds_;
}

bool EntityItem::hit_test_world(double x, double y, double tolerance) const {
    const BBox bounds = world_bounds();
    if (x < bounds.x0 - tolerance || x > bounds.x1 + tolerance ||
        y < bounds.y0 - tolerance || y > bounds.y1 + tolerance) return false;
    if (!cached_hit_path_ || hit_tolerance_ != tolerance) {
        QPainterPathStroker stroker;
        stroker.setWidth(2 * tolerance);
        QPainterPath hit_path = path();
        if (extension_item_) hit_path.addPath(extension_item_->path());
        cached_hit_path_ = stroker.createStroke(hit_path);
        hit_tolerance_ = tolerance;
    }
    return cached_hit_path_->contains(QPointF(x - pos().x(), mh_ - y - pos().y()));
}

void EntityItem::refresh() {
    const bool geometry_changed = cached_version_ != entity_->version ||
                                  std::fabs(cached_mh_ - mh_) > 1e-6 || context_dirty_;
    if (geometry_changed) {
        setPos(0.0, 0.0);
        cached_hit_path_.reset();
        cached_version_ = entity_->version;
        cached_mh_ = mh_;
        context_dirty_ = false;
        if (auto* dim = cad::as<cad::Dimension>(entity_)) {
            const auto [main_path, extension_path] = build_dimension_paths(*dim, mh_);
            setPath(main_path);
            if (extension_item_) extension_item_->setPath(extension_path);
        } else {
            setPath(build_path(*entity_, mh_));
        }
        if (dimension_label_) {
            const auto* e = cad::as<cad::Dimension>(entity_);
            QFont font("Segoe UI");
            font.setPixelSize(std::max(1, static_cast<int>(std::ceil(e->style.text_height))));
            dimension_label_->setFont(font);
            dimension_label_->setText(QString::fromStdString(e->format_label(units_, unit_factor_)));
            const Pt lp = e->label_position();
            dimension_label_->setPos(lp.x, mh_ - lp.y);
            const QRectF bounds = dimension_label_->boundingRect();
            if (e->kind_name == "radius" || e->kind_name == "diameter") {
                const double gap = e->style.text_gap;
                const double left = e->x3 < e->x1 ? -bounds.width() - gap : gap;
                dimension_label_->setTransform(QTransform::fromTranslate(left, -bounds.height() / 2));
            } else {
                dimension_label_->setTransform(
                    QTransform::fromTranslate(-bounds.width() / 2, -bounds.height() - e->style.text_gap));
            }
        }
    }
    if (geometry_changed || style_dirty_ || cached_selected_ != entity_->selected) {
        apply_style();
        style_dirty_ = false;
        cached_selected_ = entity_->selected;
    }
}

void EntityItem::adopt_preview_translation() {
    // O caminho local mais a posicao de pre-visualizacao ja representam a geometria movida.
    // Atualizar a versao evita reconstruir milhares de segmentos no mouse-up.
    cached_version_ = entity_->version;
    cached_bounds_.reset();
    cached_hit_path_.reset();
    hit_tolerance_ = -1.0;
}

QColor EntityItem::layer_qcolor() const {
    bool is_default = true;
    for (size_t i = 0; i < 3; ++i) {
        if (std::fabs(layer_color_[i] - kDefaultLayerColor[i]) >= 1e-6) is_default = false;
    }
    if (is_default) return QColor(ui::color("drawing"));
    // Padrao AutoCAD / Dark CAD: se a cor da camada for excessivamente escura
    // (preto ou quase preto), adapta dinamicamente para alto contraste garantindo
    // que nenhum desenho fora ou dentro da chapa se perca no fundo escuro.
    const double luminance = 0.299 * layer_color_[0] + 0.587 * layer_color_[1] + 0.114 * layer_color_[2];
    if (luminance < 0.20) {
        return QColor(ui::color("drawing"));
    }
    return QColor::fromRgbF(layer_color_[0], layer_color_[1], layer_color_[2]);
}

void EntityItem::apply_style() {
    const cad::Entity& e = *entity_;
    const auto* dim = cad::as<cad::Dimension>(e);
    const bool fast_outline = dense_contour(e);
    QColor col;
    double width;
    if (e.selected) {
        col = kSelPen;
        width = 2.0;
    } else if (dim) {
        col = dim->style.line_color ? QColor(QString::fromStdString(*dim->style.line_color)) : layer_qcolor();
        width = std::max(1.0, dim->style.line_width * 4);
    } else if (cad::is_a<cad::DrillPoint>(e)) {
        col = QColor("#2589f5");
        width = 1.4;
    } else if (cad::is_a<cad::ConstructionLine>(e)) {
        col = QColor("#d29b24");
        width = 1.2;
    } else {
        col = layer_qcolor();
        width = fast_outline ? 1.0 : 1.2;
    }

    QPen pen(col, width);
    if (cad::is_a<cad::ConstructionLine>(e)) pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    // A one-pixel cosmetic outline uses Qt's fast stroke path. Rounded joins
    // on thousands of tiny imported segments dominated every zoom frame.
    pen.setJoinStyle(fast_outline ? Qt::BevelJoin : Qt::RoundJoin);
    pen.setCapStyle(fast_outline ? Qt::FlatCap : Qt::RoundCap);
    setPen(pen);
    if (extension_item_ && dim) {
        QColor extension_color;
        if (e.selected) {
            extension_color = kSelPen;
        } else if (dim->style.extension_color) {
            extension_color = QColor(QString::fromStdString(*dim->style.extension_color));
        } else {
            extension_color = layer_qcolor();
        }
        QPen extension_pen(extension_color, width);
        extension_pen.setCosmetic(true);
        extension_pen.setJoinStyle(Qt::RoundJoin);
        extension_pen.setCapStyle(Qt::RoundCap);
        extension_item_->setPen(extension_pen);
        extension_item_->setBrush(Qt::NoBrush);
    }
    if (dim && dim->style.arrow_style == "closed") {
        setBrush(pen.color());
    } else {
        setBrush(Qt::NoBrush);
    }
    if (dimension_label_) {
        QColor label_color;
        if (e.selected) {
            label_color = kSelPen;
        } else if (dim->style.text_color) {
            label_color = QColor(QString::fromStdString(*dim->style.text_color));
        } else {
            label_color = layer_qcolor();
        }
        dimension_label_->setBrush(label_color);
    }
}

}  // namespace cnc::vp
