// Reguas, ponte arrastavel e funcoes de apoio do canvas (porte de src/viewport/canvas_2d.py).
#include <cmath>
#include <format>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include "ui/theme.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

double nice_spacing(double px_per_unit, double target_px) {
    if (px_per_unit <= 1e-9) return 100.0;
    const double raw = target_px / px_per_unit;
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
    const double r = raw / mag;
    const double nice = r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10;
    return nice * mag;
}

QString ruler_label(double v) {
    if (std::fabs(v) < 1e-9) return "0";
    if (v == std::trunc(v)) return QString::number(static_cast<long long>(v));
    return QString::fromStdString(std::format("{:.1f}", v));
}

// ======================================================================
// Ponte arrastavel
// ======================================================================

TabMarker::TabMarker(int index, MovedFn callback, DeletedFn delete_callback, bool manual)
    : QGraphicsEllipseItem(-5, -5, 10, 10), index_(index), callback_(std::move(callback)),
      delete_callback_(std::move(delete_callback)) {
    setPen(QPen(QColor("#fff4dc"), 1.4));
    setBrush(QBrush(manual ? QColor("#39a9ff") : QColor("#ff9f1c")));
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setFlag(QGraphicsItem::ItemIsMovable, static_cast<bool>(callback_));
    setFlag(QGraphicsItem::ItemIsSelectable, static_cast<bool>(callback_));
    setCursor(Qt::OpenHandCursor);
    setToolTip(manual ? "Ponte manual — clique para apagar ou arraste para reposicionar"
                      : "Ponte automática — clique para apagar ou arraste para reposicionar");
    setZValue(110);
}

void TabMarker::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    press_scene_pos_ = event->scenePos();
    setCursor(Qt::ClosedHandCursor);
    QGraphicsEllipseItem::mousePressEvent(event);
}

void TabMarker::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsEllipseItem::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
    const QPointF diff = event->scenePos() - press_scene_pos_;
    const double drag_distance = std::hypot(diff.x(), diff.y());
    if (drag_distance <= 4.0) {
        if (delete_callback_) delete_callback_(index_);
    } else {
        if (callback_) callback_(index_, scenePos());
    }
}

// ======================================================================
// Reguas
// ======================================================================

ConstructionRuler::ConstructionRuler(Canvas2D* canvas, const QString& axis, QWidget* parent)
    : QWidget(parent), canvas_(canvas), axis_(axis) {
    setCursor(Qt::CrossCursor);
    setToolTip("Arraste da régua para criar uma linha de construção");
}

double ConstructionRuler::world_position(const QPointF& local_pos) const {
    const QPoint global_pos = mapToGlobal(local_pos.toPoint());
    const QPoint viewport_pos = canvas_->viewport()->mapFromGlobal(global_pos);
    const QPointF scene_pos = canvas_->mapToScene(viewport_pos);
    const Pt world = canvas_->scene_to_world(scene_pos);
    return axis_ == "vertical" ? world.x : world.y;
}

void ConstructionRuler::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    drag_start_ = event->position();
    dragged_ = false;
    event->accept();
}

void ConstructionRuler::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_start_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    dragged_ = dragged_ || (event->position() - *drag_start_).manhattanLength() >= 4;
    if (dragged_) {
        const double position = world_position(event->position());
        canvas_->show_construction_preview(canvas_->construction_line_points(axis_, position));
    }
    event->accept();
}

void ConstructionRuler::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !drag_start_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (dragged_) emit construction_line_requested(axis_, world_position(event->position()));
    canvas_->clear_construction_preview();
    drag_start_.reset();
    dragged_ = false;
    event->accept();
}

RulerH::RulerH(Canvas2D* canvas, QWidget* parent) : ConstructionRuler(canvas, "vertical", parent) {
    setFixedHeight(kRulerSize);
}

void RulerH::set_cursor(double vx) {
    cx_ = vx;
    update();
}

void RulerH::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const int w = width(), h = height();
    p.fillRect(0, 0, w, h, QColor(ui::color("panel")));

    const double ppu = canvas_->transform().m11();
    if (ppu <= 0) return;

    const double factor = canvas_->material().unit_factor();
    const double ox = canvas_->material().origin_xy().x;
    const double sp = nice_spacing(ppu * factor) * factor;
    const QRectF vr = canvas_->mapToScene(canvas_->viewport()->rect()).boundingRect();
    p.setFont(QFont("Segoe UI", 7));

    double x = std::floor((vr.left() - ox) / sp) * sp + ox;
    while (x <= vr.right()) {
        const int vx = canvas_->mapFromScene(QPointF(x, 0)).x();
        if (-20 <= vx && vx <= w + 20) {
            p.setPen(QPen(QColor("#6e7681"), 1));
            p.drawLine(vx, h - 10, vx, h - 1);
            p.setPen(QColor(ui::color("muted")));
            p.drawText(vx + 2, h - 13, ruler_label((x - ox) / factor));
        }
        for (int j = 1; j < 5; ++j) {
            const int mvx = canvas_->mapFromScene(QPointF(x + j * sp / 5, 0)).x();
            if (0 <= mvx && mvx <= w) {
                p.setPen(QPen(QColor("#3c3c3c"), 1));
                p.drawLine(mvx, h - 5, mvx, h - 1);
            }
        }
        x += sp;
    }

    p.setPen(QPen(QColor(ui::color("border")), 1));
    p.drawLine(0, h - 1, w, h - 1);
    if (cx_) {
        p.setPen(QPen(QColor(255, 80, 80), 1));
        p.drawLine(static_cast<int>(*cx_), 0, static_cast<int>(*cx_), h);
    }
}

RulerV::RulerV(Canvas2D* canvas, double mat_h, QWidget* parent)
    : ConstructionRuler(canvas, "horizontal", parent), mat_h_(mat_h) {
    setFixedWidth(kRulerSize);
}

void RulerV::set_cursor(double vy) {
    cy_ = vy;
    update();
}

void RulerV::set_material_height(double h) {
    mat_h_ = h;
    update();
}

void RulerV::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const int w = width(), h = height();
    p.fillRect(0, 0, w, h, QColor(ui::color("panel")));

    const double ppu = std::fabs(canvas_->transform().m22());
    if (ppu <= 0) return;

    const double factor = canvas_->material().unit_factor();
    const double oy = canvas_->material().origin_xy().y;
    const double sp = nice_spacing(ppu * factor) * factor;
    const QRectF vr = canvas_->mapToScene(canvas_->viewport()->rect()).boundingRect();
    p.setFont(QFont("Segoe UI", 7));

    double wy = std::floor((mat_h_ - vr.bottom() - oy) / sp) * sp + oy;
    const double wy_max = mat_h_ - vr.top();
    while (wy <= wy_max) {
        const int vy = canvas_->mapFromScene(QPointF(0, mat_h_ - wy)).y();
        if (-20 <= vy && vy <= h + 20) {
            p.setPen(QPen(QColor("#6e7681"), 1));
            p.drawLine(w - 10, vy, w - 1, vy);
            const QString label = ruler_label((wy - oy) / factor);
            p.setPen(QColor(ui::color("muted")));
            p.save();
            p.translate(w - 14, vy + static_cast<int>(label.size()) * 3);
            p.rotate(-90);
            p.drawText(0, 0, label);
            p.restore();
        }
        for (int j = 1; j < 5; ++j) {
            const int mvy = canvas_->mapFromScene(QPointF(0, mat_h_ - (wy + j * sp / 5))).y();
            if (0 <= mvy && mvy <= h) {
                p.setPen(QPen(QColor("#3c3c3c"), 1));
                p.drawLine(w - 5, mvy, w - 1, mvy);
            }
        }
        wy += sp;
    }

    p.setPen(QPen(QColor(ui::color("border")), 1));
    p.drawLine(w - 1, 0, w - 1, h);
    if (cy_) {
        p.setPen(QPen(QColor(255, 80, 80), 1));
        p.drawLine(0, static_cast<int>(*cy_), w, static_cast<int>(*cy_));
    }
}

}  // namespace cnc::vp
