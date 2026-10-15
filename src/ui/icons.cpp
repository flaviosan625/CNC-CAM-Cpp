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
    if (name == "rail_selection") {
        return make_icon([](QPainter& p, int) {
            const QRectF box(7, 7, 19, 19);
            p.drawRect(box);
            for (const QPointF& point : {QPointF(7, 7), QPointF(26, 7),
                                         QPointF(7, 26), QPointF(26, 26)})
                p.drawEllipse(point, 1.5, 1.5);
            QPolygonF cursor{{QPointF(12, 12), QPointF(12, 23), QPointF(15, 20),
                              QPointF(18, 25), QPointF(20, 24), QPointF(17, 19), QPointF(22, 19)}};
            p.drawPolygon(cursor);
        }, QColor("#a9d8ff"), 1.8);
    }
    if (name == "rail_toolpaths") {
        return make_icon([](QPainter& p, int) {
            QPainterPath route;
            route.moveTo(6, 25);
            route.lineTo(13, 25);
            route.lineTo(13, 16);
            route.lineTo(22, 16);
            route.lineTo(22, 8);
            p.drawPath(route);
            p.drawLine(18, 8, 22, 4);
            p.drawLine(22, 4, 26, 8);
            for (const QPointF& point : {QPointF(6, 25), QPointF(13, 16),
                                         QPointF(22, 16), QPointF(22, 8)})
                p.drawEllipse(point, 1.7, 1.7);
            p.drawLine(26, 9, 26, 24);
            p.drawLine(23, 24, 29, 24);
        }, QColor("#a9d8ff"), 1.9);
    }
    if (name == "rail_automatic") {
        return make_icon([](QPainter& p, int) {
            for (int y : {9, 16, 23}) p.drawLine(5, y, 23, y);
            p.drawLine(11, 6, 11, 12);
            p.drawLine(18, 13, 18, 19);
            p.drawLine(8, 20, 8, 26);
            QPolygonF spark{{QPointF(26, 4), QPointF(27, 9), QPointF(31, 11),
                             QPointF(27, 13), QPointF(26, 18), QPointF(24, 13),
                             QPointF(20, 11), QPointF(24, 9)}};
            p.drawPolygon(spark);
        }, QColor("#a9d8ff"), 1.8);
    }
    if (name == "rail_simulation") {
        return make_icon([](QPainter& p, int) {
            QPolygonF top{{QPointF(6, 11), QPointF(16, 5), QPointF(26, 11), QPointF(16, 17)}};
            p.drawPolygon(top);
            p.drawLine(6, 11, 6, 22);
            p.drawLine(6, 22, 16, 28);
            p.drawLine(16, 28, 16, 17);
            p.drawLine(26, 11, 26, 20);
            QPolygonF play{{QPointF(21, 17), QPointF(29, 21), QPointF(21, 25)}};
            p.drawPolygon(play);
        }, QColor("#a9d8ff"), 1.8);
    }
    if (name == "rail_order") {
        return make_icon([](QPainter& p, int) {
            p.drawRoundedRect(QRectF(7, 4, 19, 25), 2, 2);
            for (int y : {11, 17, 23}) {
                p.drawRect(QRectF(10, y - 2, 3, 3));
                p.drawLine(16, y, 23, y);
            }
            p.drawLine(10, 11, 11, 12);
            p.drawLine(11, 12, 13, 9);
        }, QColor("#a9d8ff"), 1.8);
    }
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
