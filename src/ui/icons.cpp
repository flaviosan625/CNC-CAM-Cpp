// Porte do gerador de icones de src/ui/tool_panel.py.
#include "ui/icons.hpp"

#include <cmath>
#include <array>
#include <functional>
#include <tuple>

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPen>
#include <QPolygonF>

namespace cnc::ui {

namespace {

constexpr int kSize = 32;
constexpr int kMargin = 5;

QIcon make_icon(const std::function<void(QPainter&, int)>& draw,
               const QColor& color = QColor("#b9cee3"), qreal width = 1.7) {
    QPixmap pixmap(kSize * 2, kSize * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    draw(painter, kSize);
    painter.end();

    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    for (const auto& [mode, state, tint] : {
             std::tuple<QIcon::Mode, QIcon::State, QColor>{QIcon::Active, QIcon::Off, QColor("#8ec9ff")},
             std::tuple<QIcon::Mode, QIcon::State, QColor>{QIcon::Normal, QIcon::On, QColor("#ffffff")},
             std::tuple<QIcon::Mode, QIcon::State, QColor>{QIcon::Active, QIcon::On, QColor("#ffffff")},
             std::tuple<QIcon::Mode, QIcon::State, QColor>{QIcon::Disabled, QIcon::Off, QColor("#5c718a")},
         }) {
        QPixmap tinted = pixmap.copy();
        QPainter tint_painter(&tinted);
        tint_painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint_painter.fillRect(tinted.rect(), tint);
        tint_painter.end();
        icon.addPixmap(tinted, mode, state);
    }
    return icon;
}

QIcon icon_for(const QString& name) {
    if (name == "zoom_fit") {
        return make_icon([](QPainter& p, int s) {
            p.drawRect(kMargin + 3, kMargin + 3, s - 2 * kMargin - 6, s - 2 * kMargin - 6);
            for (const auto& [dx, dy] : std::array<std::pair<int, int>, 4>{{{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}}) {
                const int cx = s / 2 + dx * (s / 2 - kMargin);
                const int cy = s / 2 + dy * (s / 2 - kMargin);
                p.drawLine(cx, cy, cx - dx * 4, cy);
                p.drawLine(cx, cy, cx, cy - dy * 4);
            }
        });
    }
    if (name == "zoom_in" || name == "zoom_out") {
        return make_icon([name](QPainter& p, int s) {
            const int cx = s / 2, cy = s / 2, r = s / 2 - kMargin - 2;
            p.drawEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r));
            p.drawLine(cx - 3, cy, cx + 3, cy);
            if (name == "zoom_in") p.drawLine(cx, cy - 3, cx, cy + 3);
        });
    }
    if (name == "pan") {
        return make_icon([](QPainter& p, int s) {
            const int cx = s / 2, cy = s / 2, length = s / 2 - kMargin - 1;
            for (const auto& [dx, dy] : std::array<std::pair<int, int>, 4>{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}}) {
                const int ex = cx + dx * length, ey = cy + dy * length;
                p.drawLine(cx + dx * 2, cy + dy * 2, ex, ey);
                if (dx == 0) {
                    p.drawLine(ex, ey, ex - 2, ey - dy * 3);
                    p.drawLine(ex, ey, ex + 2, ey - dy * 3);
                } else {
                    p.drawLine(ex, ey, ex - dx * 3, ey - 2);
                    p.drawLine(ex, ey, ex - dx * 3, ey + 2);
                }
            }
        });
    }
    if (name == "layers") {
        return make_icon([](QPainter& p, int s) {
            const double cx = s / 2.0, middle = s / 2.0;
            const double width = s / 2.0 - kMargin + 2, height = (s / 2.0 - kMargin + 2) / 2;
            for (int i = 0; i < 3; ++i) {
                const double top = middle + (i - 1) * (height + 2);
                const QPolygonF polygon{{QPointF(cx, top - height), QPointF(cx + width, top),
                                            QPointF(cx, top + height), QPointF(cx - width, top)}};
                p.setBrush(i == 0 ? p.pen().color() : Qt::NoBrush);
                p.drawPolygon(polygon);
            }
            p.setBrush(Qt::NoBrush);
        });
    }
    if (name == "lock" || name == "lock_open") {
        return make_icon([name](QPainter& p, int s) {
            const QRectF body(kMargin + 1.5, s / 2.0 - 1, s - 2 * kMargin - 3, s / 2.0 - kMargin + 2);
            p.drawRoundedRect(body, 2.5, 2.5);
            const QRectF arc(body.left() + 3, kMargin + 1, body.width() - 6, body.height());
            p.drawArc(arc, 0, 180 * 16);
            p.drawLine(QPointF(arc.left(), arc.center().y()), QPointF(arc.left(), body.top()));
            if (name == "lock")
                p.drawLine(QPointF(arc.right(), arc.center().y()), QPointF(arc.right(), body.top()));
        });
    }
    if (name == "eye" || name == "eye_closed") {
        return make_icon([name](QPainter& p, int s) {
            const double middle = s / 2.0;
            QPainterPath path;
            if (name == "eye") {
                path.moveTo(kMargin, middle);
                path.quadTo(middle, kMargin + 1, s - kMargin, middle);
                path.quadTo(middle, s - kMargin - 1, kMargin, middle);
                p.drawPath(path);
                p.drawEllipse(QPointF(middle, middle), 2.8, 2.8);
            } else {
                path.moveTo(kMargin, middle - 1);
                path.quadTo(middle, s - kMargin - 1, s - kMargin, middle - 1);
                p.drawPath(path);
                for (int dx : {-1, 1})
                    p.drawLine(QPointF(middle + dx * (s / 2.0 - kMargin - 1.5), middle + 1.5),
                               QPointF(middle + dx * (s / 2.0 - kMargin + 0.5), middle + 4));
                p.drawLine(QPointF(middle, middle + 4.5), QPointF(middle, middle + 7));
            }
        });
    }
    if (name == "scale") {
        return make_icon([](QPainter& p, int s) {
            p.drawRect(kMargin, kMargin, (s - 2 * kMargin) / 2, (s - 2 * kMargin) / 2);
            p.drawRect(s / 2, s / 2, s - 2 * kMargin - s / 2, s - 2 * kMargin - s / 2);
            p.drawLine(s / 2 + 3, s / 2 + 3, s - kMargin - 1, s - kMargin - 1);
        });
    }
    return QIcon();
}

}  // namespace

QIcon get_icon(const QString& name) {
    // The Python reference draws the complete tool vocabulary with QPainter.
    // Those rasterized states are embedded in the C++ executable so both
    // implementations use the same artwork and do not depend on a working
    // directory or a system icon theme.
    const QString resource = QString(":/assets/tool-icons/%1.png").arg(name);
    if (QFile::exists(resource)) return QIcon(resource);
    return icon_for(name);
}

}  // namespace cnc::ui
