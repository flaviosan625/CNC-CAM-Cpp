#include "ui/machining_guide.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include "geo/geom.hpp"
#include "ui/responsive.hpp"

namespace cnc::ui {

// Deterministic, cached maple grain; no external bitmap and no random repaint noise.
const QImage& woodTexture() {
    static const QImage texture = [] {
        QImage image(640, 480, QImage::Format_RGB32);
        quint32 seed = 217;
        for (int y = 0; y < image.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                seed = 1664525u * seed + 1013904223u;
                const double warp = y + 3 * std::sin(x * .007) + 2 * std::sin(x * .015 + y * .009);
                const double grain = 2.5 * std::sin(warp * .43) + 1.8 * std::sin(warp * 2.7)
                    + 2 * std::sin(warp * .072) + (int(seed >> 28) - 7) * .7;
                row[x] = qRgb(int(208 + grain), int(174 + grain), int(132 + grain));
            }
        }
        return image;
    }();
    return texture;
}

namespace {
constexpr double pi = std::numbers::pi;
const QColor cyan("#29d9ef"), orange("#ffae55"), ink("#e6edf5"), muted("#9bacbc");

QPainterPath rounded(QRectF rect, double radius = 22) {
    QPainterPath path; path.addRoundedRect(rect, radius, radius); return path;
}
geo::Geom areaGeometry(const QPainterPath& path) {
    geo::Geom area;
    for (const auto& polygon : path.toSubpathPolygons()) {
        Points points; for (const auto& point : polygon) points.push_back({point.x(), point.y()});
        if (points.size() < 3) continue;
        auto part = geo::make_polygon(points);
        if (!part) continue;
        if (!part.is_valid()) part = part.make_valid();
        area = area ? area.symmetric_difference(part) : part;
    }
    return area;
}
QPainterPath offset(const QPainterPath& path, double distance) {
    if (std::abs(distance) < .001) return path;
    const auto area = areaGeometry(path);
    QPainterPath result; result.setFillRule(Qt::OddEvenFill);
    if (!area) return result;
    const auto buffered = area.buffer(distance);
    if (!buffered || buffered.is_empty()) return result;
    const auto ring = [&result](const Points& points) {
        for (size_t i = 0; i < points.size(); ++i)
            if (i == 0) result.moveTo(points[i].x, points[i].y); else result.lineTo(points[i].x, points[i].y);
        result.closeSubpath();
    };
    for (const auto& part : buffered.geoms()) {
        if (!part.is_polygon()) continue;
        ring(part.exterior_coords());
        for (const auto& hole : part.interior_coords()) ring(hole);
    }
    return result;
}
QPainterPath star(int points, double inner, QRectF rect) {
    QPainterPath path;
    const double radius = std::min(rect.width(), rect.height()) / 2;
    for (int i = 0; i < points * 2; ++i) {
        const double a = i * pi / points - pi / 2;
        const QPointF pt = rect.center() + QPointF(std::cos(a), std::sin(a)) * radius * (i % 2 ? inner : 1);
        if (i == 0) path.moveTo(pt); else path.lineTo(pt);
    }
    path.closeSubpath(); return path;
}
QPointF pointAt(const QPainterPath& path, double fraction) {
    if (path.isEmpty()) return {};
    return path.pointAtPercent(path.percentAtLength(path.length() * std::clamp(fraction, 0.0, 1.0)));
}
void text(QPainter& p, QRectF rect, const QString& value, int size = 13,
          QColor color = ink, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    QFont font("Segoe UI"); font.setPixelSize(size); p.setFont(font); p.setPen(color);
    p.drawText(rect, align, value);
}
void arrow(QPainter& p, QPointF from, QPointF to, QColor color = cyan, double width = 2) {
    const QLineF line(from, to);
    if (line.length() < .01) return;
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap)); p.drawLine(line);
    const QPointF v = (to - from) / line.length(), n(-v.y(), v.x());
    p.setPen(Qt::NoPen); p.setBrush(color);
    p.drawPolygon(QPolygonF{to, to - v * 8 + n * 3.2, to - v * 8 - n * 3.2});
}
void dimension(QPainter& p, QPointF a, QPointF b, const QString& label, QPointF shift = {}) {
    p.setPen(QPen(QColor("#8395a7"), .9, Qt::DashLine));
    p.drawLine(a - shift, a + QPointF(0, 6)); p.drawLine(b - shift, b + QPointF(0, 6));
    arrow(p, a, b, QColor("#c5d0dd"), .9); arrow(p, b, a, QColor("#c5d0dd"), .9);
    QPointF c = (a + b) / 2;
    const bool vertical = std::abs(a.y() - b.y()) > std::abs(a.x() - b.x());
    const QRectF r = vertical ? QRectF(c.x() + 10, c.y() - 13, 145, 26)
                             : QRectF(c.x() - 65, c.y() + 6, 130, 24);
    text(p, r, label, 13, ink, vertical ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
}

void timberTop(QPainter& p, const QPainterPath& top, bool dark = false) {
    p.save();
    p.setClipPath(top, Qt::IntersectClip);
    p.drawImage(top.boundingRect(), woodTexture());
    QLinearGradient light(top.boundingRect().topLeft(), top.boundingRect().bottomRight());
    light.setColorAt(0, QColor(255, 241, 210, dark ? 0 : 85));
    light.setColorAt(1, QColor(61, 36, 20, dark ? 100 : 25));
    p.fillPath(top, light); p.restore();
    p.setBrush(Qt::NoBrush); p.setPen(QPen(dark ? QColor("#887150") : QColor("#f0d4a9"), 1.1)); p.drawPath(top);
}
void timber(QPainter& p, const QPainterPath& top, double thickness = 32) {
    const QPainterPath base = top.translated(0, thickness);
    for (int spread = 13; spread > 0; spread -= 3) {
        p.setBrush(QColor(0, 0, 0, 9)); p.setPen(QPen(QColor(0, 0, 0, 9), spread * 2));
        p.drawPath(base.translated(6, 6));
    }
    QLinearGradient side(0, top.boundingRect().top(), 0, base.boundingRect().bottom());
    side.setColorAt(0, QColor("#c0a174")); side.setColorAt(.65, QColor("#a87e4d")); side.setColorAt(1, QColor("#775333"));
    for (double z = thickness; z >= 0; z -= 1.5) p.fillPath(top.translated(0, z), side);
    p.setPen(QPen(QColor(67, 40, 20, 42), .7)); p.setBrush(Qt::NoBrush);
    for (double z = 5; z < thickness; z += 4) p.drawPath(top.translated(0, z));
    p.setPen(QPen(QColor("#67482f"), 1.3)); p.drawPath(base);
    timberTop(p, top);
}
void pocket(QPainter& p, const QPainterPath& stock, const QPainterPath& cavity, double depth) {
    timber(p, stock, depth + 15);
    p.save(); p.setClipPath(cavity);
    QLinearGradient wall(0, cavity.boundingRect().top(), 0, cavity.boundingRect().bottom());
    wall.setColorAt(0, QColor("#594432")); wall.setColorAt(.7, QColor("#9a7956")); wall.setColorAt(1, QColor("#cfaa76"));
    p.fillPath(cavity, wall);
    for (double z = 4; z < depth; z += 5) {
        p.setPen(QPen(QColor(220, 190, 149, 45), .7)); p.drawPath(cavity.translated(0, z));
    }
    timberTop(p, cavity.translated(0, depth), true);
    p.restore();
    p.setPen(QPen(QColor("#f4d8b0"), 1.5)); p.setBrush(Qt::NoBrush); p.drawPath(cavity);
}
QString mm(double n) { return QString::number(n, 'g', 4) + " mm"; }
}

GuideModel::GuideModel(QObject* parent) : QObject(parent) {
    values_ = {{"side", 0}, {"openSide", 2}, {"direction", 0}, {"ramp", 1}, {"lead", 2}, {"fill", 0},
        {"depth", 6.0}, {"start", 0.0}, {"pass", 2.0}, {"diameter", 6.0}, {"stepover", 2.4}, {"toolStepover", 2.4},
        {"tabs", true}, {"tabCount", 4}, {"tabLength", 8.0}, {"tabHeight", 2.0}, {"tabs3d", false},
        {"leadLength", 12.0}, {"leadRadius", 8.0}, {"leadAngle", 0.0}, {"leadOut", true},
        {"rampDistance", 40.0}, {"rampAngle", 5.0}, {"thickness", 18.0}, {"clearance", 5.0},
        {"width", 120.0}, {"height", 90.0}, {"originX", 0}, {"originY", 0}, {"zeroZ", 0},
        {"toolType", 0}, {"points", 5}, {"innerRatio", .5}, {"radius", 50.0}, {"shape", 0},
        {"reversePreview", false}, {"rasterAngle", 0.0}, {"allowance", 0.0}};
}
double GuideModel::number(const QString& key) const { return values_.value(key).toDouble(); }
void GuideModel::setValue(const QString& key, const QVariant& value) {
    bool ok = false; const double number = value.toDouble(&ok);
    if (ok && !std::isfinite(number)) return;
    if (values_.value(key) == value) return;
    values_[key] = value;
    emit valueChanged(key, value); emit changed();
}
void GuideModel::setContour(const QPainterPath& contour, bool closed) {
    contour_ = contour; closed_ = closed; emit changed();
}

QString MachiningGuide::modeKey(const QString& k) {
    if (k.startsWith("01")) return "side";
    if (k.startsWith("03")) return "tabs3d";
    if (k.startsWith("04")) return "lead";
    if (k.startsWith("05") || k.startsWith("07")) return "ramp";
    if (k.startsWith("06")) return "fill";
    if (k.startsWith("08")) return "zeroZ";
    if (k.startsWith("09")) return "toolType";
    return {};
}
QStringList MachiningGuide::modeLabels(const QString& k) {
    if (k.startsWith("01")) return {"Fora", "Dentro", "Sobre a linha"};
    if (k.startsWith("03")) return {"Ponte reta", "Ponte 3D"};
    if (k.startsWith("04")) return {"Sem entrada", "Reta", "Arco"};
    if (k.startsWith("05") || k.startsWith("07")) return {"Sem rampa", "Suave", "Zig-zag", "Espiral"};
    if (k.startsWith("06")) return {"Offset", "Zig-zag (raster)", "Espiral · demonstração"};
    if (k.startsWith("08")) return {"Z zero no topo", "Z zero na base"};
    if (k.startsWith("09")) return {"Topo", "Esférica", "V", "Gravação", "Topo com raio", "Cônica", "Broca"};
    return {};
}
QString MachiningGuide::title(const QString& k) {
    static const QStringList titles = {"CORTE FORA / DENTRO", "PROFUNDIDADE E PASSES", "PONTES DE FIXAÇÃO",
        "ENTRADAS E SAÍDAS", "RAMPAS DE ENTRADA", "LIMPEZA DE ÁREA", "ATAQUE RAMPEADO",
        "MATERIAL E ORIGEM", "FERRAMENTAS", "DESENHO PARAMÉTRICO"};
    return titles.value(k.left(2).toInt() - 1, "PRÉVIA CAM");
}
MachiningGuide::MachiningGuide(const QString& kind, QWidget* parent, GuideModel* model)
    : QWidget(parent), model_(model ? model : new GuideModel(this)), kind_(kind) {
    setObjectName("CamGuideImage");
    setMinimumSize(180, 75); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Desenho renderizado: " + title(kind));
    setToolTip("Prévia didática dos parâmetros. O percurso calculado e a simulação validam a usinagem.");
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (!visibleRegion().isEmpty()) setProgress(std::fmod(progress_ + .0033 * speed_, 1.0));
    });
    connect(model_, &GuideModel::changed, this, [this] { rebuild(); update(); });
    rebuild();
}
void MachiningGuide::set_kind(const QString& kind) {
    if (kind == kind_) return;
    kind_ = kind; rebuild(); update(); emit topicChanged();
}
int MachiningGuide::mode() const { return model_->integer(modeKey(kind_)); }
void MachiningGuide::set_mode(int mode) {
    const auto labels = modeLabels(kind_);
    if (!labels.isEmpty()) model_->setValue(modeKey(kind_), std::clamp(mode, 0, int(labels.size()) - 1));
}
QString MachiningGuide::mode_label_text() const { return modeLabels(kind_).value(mode(), title(kind_)); }
void MachiningGuide::syncTimer() { if (playing_ && isVisible()) timer_.start(); else timer_.stop(); }
void MachiningGuide::setPlaying(bool play) { playing_ = play; syncTimer(); emit playingChanged(play); update(); }
void MachiningGuide::setProgress(double p) { progress_ = std::clamp(p, 0.0, 1.0); update(); emit progressChanged(progress_); }
void MachiningGuide::showEvent(QShowEvent* e) { QWidget::showEvent(e); syncTimer(); }
void MachiningGuide::hideEvent(QHideEvent* e) { timer_.stop(); QWidget::hideEvent(e); }

void MachiningGuide::rebuild() {
    const QRectF region(95, 95, 420, 280);
    shape_ = {};
    if (model_->hasSelection()) {
        const QRectF bounds = model_->contour().boundingRect();
        const double scale = std::min(region.width() / std::max(.001, bounds.width()),
                                      region.height() / std::max(.001, bounds.height()));
        QTransform fit;
        fit.translate(region.center().x(), region.center().y()); fit.scale(scale, -scale);
        fit.translate(-bounds.center().x(), -bounds.center().y());
        shape_ = fit.map(model_->contour());
        tool_width_ = std::clamp(model_->number("diameter") * scale, 1.0, 120.0);
    } else {
        const int chosen = model_->integer("shape");
        if (chosen == 1) shape_ = rounded(region.adjusted(15, 20, -15, -20));
        else if (chosen == 2) shape_.addEllipse(QRectF(170, 95, 280, 280));
        else if (chosen == 3 || kind_.startsWith("10"))
            shape_ = star(std::clamp(model_->integer("points"), 3, 32),
                          std::clamp(model_->number("innerRatio"), .1, .9), QRectF(160, 85, 300, 300));
        else if (kind_.startsWith("01") || kind_.startsWith("04")) {
            shape_.moveTo(130, 95); shape_.lineTo(245, 95); shape_.quadTo(270, 95, 270, 120);
            shape_.lineTo(270, 210); shape_.quadTo(270, 235, 295, 235); shape_.lineTo(480, 235);
            shape_.quadTo(515, 235, 515, 270); shape_.lineTo(515, 340); shape_.quadTo(515, 375, 480, 375);
            shape_.lineTo(130, 375); shape_.quadTo(95, 375, 95, 340); shape_.lineTo(95, 130);
            shape_.quadTo(95, 95, 130, 95); shape_.closeSubpath();
        } else shape_ = rounded(region.adjusted(20, 15, -20, -15), 32);
        tool_width_ = std::clamp(model_->number("diameter") * 3.5, 2.0, 100.0);
    }
    projection_ = QTransform(.82, -.10, .24, .66, 15, 99);
    route_ = {};
    const int topic = kind_.left(2).toInt();
    const double radius = tool_width_ * .5;
    if (topic == 1 || topic == 3 || topic == 4 || topic == 10) {
        const int side = topic == 10 ? 2 : model_->integer("side");
        if (model_->hasSelection() && !model_->closed()) {
            Points coords;
            for (const auto& polygon : shape_.toSubpathPolygons()) for (const QPointF& pt : polygon) coords.push_back({pt.x(), pt.y()});
            const int open = model_->integer("openSide");
            const auto line = geo::make_linestring(coords);
            const auto parallel = open == 2 ? line : line.offset_curve(open == 0 ? -radius : radius);
            if (parallel) for (const auto& segment : parallel.geoms()) {
                const auto pts = segment.coords();
                for (size_t i = 0; i < pts.size(); ++i) {
                    if (i == 0) route_.moveTo(pts[i].x, pts[i].y); else route_.lineTo(pts[i].x, pts[i].y);
                }
            }
        } else {
            const double allowance = model_->number("allowance") * tool_width_ / std::max(.01, model_->number("diameter"));
            route_ = offset(shape_, side == 0 ? radius + allowance : side == 1 ? -radius - allowance : 0);
        }
        if (topic == 4 && model_->integer("lead") != 0 && !route_.isEmpty()) {
            const QPointF start = pointAt(route_, 0), next = pointAt(route_, .003);
            const QPointF tangent = (next - start) / std::max(.001, QLineF(next, start).length());
            const QPointF normal(-tangent.y(), tangent.x());
            const double angle = model_->number("leadAngle") * pi / 180;
            const QPointF approach = tangent * std::cos(angle) + normal * std::sin(angle);
            const double length = std::clamp(model_->number("leadLength") * 3.5, 4.0, 90.0);
            const double r = std::clamp(model_->number("leadRadius") * 3.5, 4.0, 85.0);
            QPainterPath lead;
            if (model_->integer("lead") == 1) { lead.moveTo(start - approach * length); lead.lineTo(start); }
            else { lead.moveTo(start - tangent * r + normal * r); lead.cubicTo(start - tangent * r, start - tangent * r * .55, start); }
            lead.connectPath(route_); route_ = lead;
            if (model_->integer("leadOut")) route_.lineTo(start + tangent * length);
        }
        route_ = projection_.map(route_);
    } else if (topic == 6) {
        const QPainterPath inside = offset(shape_, -radius);
        const double lateral = model_->number("stepover") > 0 ? model_->number("stepover") : model_->number("toolStepover");
        const double step = std::clamp(lateral * 3.5, 8.0, 65.0);
        if (!inside.isEmpty() && model_->integer("fill") == 1) {
            QTransform rotate; rotate.translate(305, 235); rotate.rotate(model_->number("rasterAngle")); rotate.translate(-305, -235);
            const QPainterPath scan = rotate.inverted().map(inside);
            const auto area = areaGeometry(scan);
            const QRectF b = scan.boundingRect(); bool reverse = false;
            for (double y = b.top() + .1; y < b.bottom(); y += step) {
                const auto line = geo::make_linestring({{b.left() - 1, y}, {b.right() + 1, y}}).intersection(area);
                if (!line) continue;
                for (const auto& segment : line.geoms()) {
                    auto points = segment.coords(); if (reverse) std::reverse(points.begin(), points.end());
                    for (size_t i = 0; i < points.size(); ++i) {
                        const QPointF pt = rotate.map(QPointF(points[i].x, points[i].y));
                        // Never draw a cutting connector across an island or outside the pocket.
                        QPainterPath connector; connector.moveTo(route_.currentPosition()); connector.lineTo(pt);
                        if (i == 0 && (route_.isEmpty() || !inside.contains(connector))) route_.moveTo(pt);
                        else route_.lineTo(pt);
                    }
                }
                reverse = !reverse;
            }
        } else if (model_->integer("fill") == 2 && !model_->hasSelection()) {
            // Continuous superellipse spiral; unlike rings, there is exactly one start.
            const QRectF b = inside.boundingRect();
            const int turns = std::clamp(int(std::min(b.width(), b.height()) / (step * 2)), 2, 16);
            for (int i = 0; i <= turns * 120; ++i) {
                const double t = double(i) / (turns * 120), a = t * turns * 2 * pi;
                const double x = std::copysign(std::pow(std::abs(std::cos(a)), .28), std::cos(a));
                const double y = std::copysign(std::pow(std::abs(std::sin(a)), .28), std::sin(a));
                const QPointF pt = b.center() + QPointF(x * b.width() * .46, y * b.height() * .46) * (1 - t);
                if (i == 0) route_.moveTo(pt); else route_.lineTo(pt);
            }
        } else {
            for (double inset = radius; inset < 250; inset += step) {
                const auto ring = offset(shape_, -inset); if (ring.isEmpty()) break;
                route_.addPath(ring);
            }
        }
        route_ = projection_.map(route_).translated(0, std::clamp(model_->number("depth") * 3, 0.0, 65.0));
    } else if (topic == 7 && model_->integer("ramp") <= 1) {
        const double depth = std::clamp(model_->number("depth") * 15, 0.0, 155.0);
        const double distance = std::clamp(model_->number("rampDistance") * 5, 25.0, 290.0);
        route_.moveTo(155, 152);
        route_.lineTo(model_->integer("ramp") == 0 ? 155 : 155 + distance, 152 + depth);
        route_.lineTo(485, 152 + depth);
    } else if (topic == 5 || topic == 7) {
        const double depth = std::clamp(model_->number("depth") * 9, 0.0, 130.0);
        const double travel = std::clamp(model_->number("rampDistance") * 4, 15.0, 260.0);
        const int ramp = model_->integer("ramp");
        for (int i = 0; i <= 240; ++i) {
            const double t = i / 240.0; QPointF xy;
            if (ramp == 3) xy = QPointF(305 + 105 * std::cos(t * pi * 6), 215 + 65 * std::sin(t * pi * 6));
            else if (ramp == 2) {
                const double wave = std::acos(std::cos(t * pi * 6)) / pi;
                xy = QPointF(170 + travel * wave, 180 + 95 * t);
            } else xy = QPointF(170 + (ramp == 0 ? 0 : travel * t), 210);
            const QPointF pt = projection_.map(xy) + QPointF(0, depth * t);
            if (i == 0) route_.moveTo(pt); else route_.lineTo(pt);
        }
    } else if (topic == 2) {
        const double depth = std::clamp(model_->number("depth") * 12, 0.0, 155.0);
        const double start = std::clamp(model_->number("start") * 8, 0.0, 55.0);
        const int passes = std::clamp(int(std::ceil(model_->number("depth") / std::max(.01, model_->number("pass")))), 1, 24);
        route_.moveTo(190, 162 + start);
        for (int i = 0; i < passes; ++i) {
            const double y = 162 + start + depth * (i + 1) / passes;
            route_.lineTo(i % 2 ? 445 : 190, y); route_.lineTo(i % 2 ? 190 : 445, y);
        }
    }
    if (model_->integer("direction") == 1 && topic != 5 && topic != 7 && topic != 2) route_ = route_.toReversed();
}

void MachiningGuide::drawTool(QPainter& p, QPointF tip, double width, int type) const {
    p.save(); p.translate(tip); width = std::clamp(width, 12.0, 65.0);
    const double r = width / 2, h = width * 3.6;
    QRadialGradient shadow(9, 5, width * 1.1); shadow.setColorAt(0, QColor(0, 0, 0, 115)); shadow.setColorAt(1, Qt::transparent);
    p.setPen(Qt::NoPen); p.setBrush(shadow); p.drawEllipse(QPointF(9, 5), width * 1.4, width * .42);
    QLinearGradient metal(-r, 0, r, 0);
    metal.setColorAt(0, QColor("#404956")); metal.setColorAt(.18, QColor("#b8c4cc"));
    metal.setColorAt(.38, QColor("#f8fcff")); metal.setColorAt(.53, QColor("#8a939e"));
    metal.setColorAt(.78, QColor("#d3dce5")); metal.setColorAt(1, QColor("#4b5563"));
    QPainterPath cutter;
    cutter.moveTo(-r, -h * .64); cutter.lineTo(r, -h * .64);
    if (type == 2 || type == 3 || type == 5) {
        cutter.lineTo(type == 5 ? r * .18 : 0, -2);
        if (type == 5) cutter.quadTo(0, 4, -r * .18, -2);
    } else if (type == 1) { cutter.lineTo(r, -r); cutter.quadTo(r, 0, 0, 0); cutter.quadTo(-r, 0, -r, -r); }
    else if (type == 6) { cutter.lineTo(r, -r * .5); cutter.lineTo(0, 0); cutter.lineTo(-r, -r * .5); }
    else if (type == 4) { cutter.lineTo(r, -r * .25); cutter.quadTo(r, 0, r * .7, 0); cutter.lineTo(-r * .7, 0); cutter.quadTo(-r, 0, -r, -r * .25); }
    else { cutter.lineTo(r, 0); cutter.lineTo(-r, 0); }
    cutter.closeSubpath();
    p.setPen(QPen(QColor("#b0bac7"), .8)); p.setBrush(metal); p.drawPath(cutter);
    p.save(); p.setClipPath(cutter);
    for (int i = -2; i <= 3; ++i) {
        QPainterPath flute;
        const double shift = playing_ ? std::fmod(progress_ * 9, 1.0) * width : 0;
        const double x = i * width * .65 + shift;
        flute.moveTo(x - r, -h * .7);
        flute.cubicTo(x + width * 1.3, -h * .45, x - width * 1.2, -h * .20, x - r, 4);
        p.setPen(QPen(QColor("#333e4b"), width * .22)); p.drawPath(flute);
        p.setPen(QPen(QColor("#e4eff5"), 1.2)); p.drawPath(flute.translated(-width * .1, 0));
    }
    p.restore();
    p.setPen(QPen(QColor("#aebbc6"), .8)); p.setBrush(metal);
    p.drawRect(QRectF(-r, -h, width, h * .36));
    p.drawEllipse(QRectF(-r, -h - 3, width, 6));
    p.setPen(QPen(QColor("#37414d"), 1.1)); p.drawLine(QPointF(-r, -h * .64), QPointF(r, -h * .64));
    p.restore();
}

void MachiningGuide::drawRoute(QPainter& p, const QPainterPath& path, bool tool) const {
    if (path.isEmpty()) return;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(41, 217, 239, 130), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
    const double progress = model_->integer("reversePreview") ? 1 - progress_ : progress_;
    // Preserve moveTo boundaries when revealing a path: never invent links across islands.
    QPainterPath revealed; double previous_length = 0;
    const auto polygons = path.toSubpathPolygons(); const double target = path.length() * progress;
    for (const auto& polygon : polygons) {
        if (polygon.isEmpty()) continue;
        revealed.moveTo(polygon.first());
        for (qsizetype i = 1; i < polygon.size(); ++i) {
            const double length = QLineF(polygon[i - 1], polygon[i]).length();
            if (previous_length + length > target) {
                revealed.lineTo(polygon[i - 1] + (polygon[i] - polygon[i - 1]) * std::clamp((target - previous_length) / std::max(.001, length), 0.0, 1.0));
                previous_length = target + 1; break;
            }
            revealed.lineTo(polygon[i]); previous_length += length;
        }
        if (previous_length > target) break;
    }
    p.setPen(QPen(cyan, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(revealed);
    // Sample arrows within each subpath. A pair of global percentages may straddle
    // a moveTo and would misleadingly draw a diagonal cut between separate rings.
    int displayed = 0;
    for (const auto& polygon : polygons) {
        if (displayed++ >= 5 || polygon.size() < 2) break;
        QPainterPath contour; contour.addPolygon(polygon);
        for (double t : {.18, .65}) arrow(p, pointAt(contour, t), pointAt(contour, t + .025), cyan, 1.5);
    }
    p.setPen(QPen(QColor("#efffff"), 1.2)); p.setBrush(QColor("#32cba1")); p.drawEllipse(pointAt(path, 0), 4, 4);
    if (tool) drawTool(p, pointAt(path, progress), std::clamp(tool_width_, 16.0, 38.0), model_->integer("toolType"));
}

void MachiningGuide::drawScene(QPainter& p) {
    const int topic = kind_.left(2).toInt();
    const double depth = model_->number("depth");
    const QPainterPath top = projection_.map(shape_);
    if (topic == 1 || topic == 4 || topic == 10) {
        if (model_->hasSelection() && !model_->closed()) {
            timber(p, projection_.map(rounded(QRectF(60, 60, 500, 345))), 30);
            p.setPen(QPen(ink, 1.4)); p.setBrush(Qt::NoBrush); p.drawPath(top);
        } else timber(p, top, 28);
        p.setPen(QPen(QColor("#fff4de"), 1.2)); p.setBrush(Qt::NoBrush); p.drawPath(top);
        drawRoute(p, route_, topic != 10);
        if (topic == 10) {
            const QPointF center = projection_.map(shape_.boundingRect().center());
            const QPointF outside = projection_.map(QPointF(shape_.boundingRect().center().x(), shape_.boundingRect().top()));
            p.setPen(QPen(muted, 1, Qt::DashLine)); p.setBrush(Qt::NoBrush);
            p.drawPath(projection_.map(rounded(shape_.boundingRect(), 150)));
            dimension(p, center, outside, "R " + mm(model_->number("radius")));
            text(p, {555, 160, 150, 30}, QString("%1 pontas").arg(model_->integer("points")), 17);
            text(p, {555, 195, 150, 30}, QString("Interno %1%").arg(qRound(model_->number("innerRatio") * 100)), 14, orange);
        } else {
            text(p, {550, 145, 155, 30}, mode_label_text(), 19, cyan);
            text(p, {550, 180, 155, 30}, "Ø " + mm(model_->number("diameter")), 14);
            text(p, {550, 212, 150, 45}, "Linha clara = vetor\nCiano = centro da fresa", 11, muted);
            if (topic == 4) text(p, {550, 267, 155, 30}, "Raio " + mm(model_->number("leadRadius")), 13, orange);
        }
    } else if (topic == 2) {
        const double z = std::clamp(depth * 12, 0.0, 155.0), start = std::clamp(model_->number("start") * 8, 0.0, 55.0);
        QPainterPath block; block.addPolygon(QPolygonF{{75, 135}, {150, 90}, {490, 90}, {520, 135}, {520, 330}, {75, 330}}); block.closeSubpath();
        timber(p, block, 18);
        const QRectF opening(170, 143, 290, 178); QPainterPath hole; hole.addRect(opening);
        p.fillPath(hole, QColor("#5b4836"));
        for (int i = 0; i < 10; ++i) {
            p.setPen(QPen(QColor(213, 173, 118, 45), 1)); p.drawLine(170, 151 + i * 17, 460, 151 + i * 17);
        }
        p.fillRect(QRectF(170, 162 + start + z, 290, std::max(0.0, 159 - start - z)), QColor("#bb996c"));
        const int passes = std::clamp(int(std::ceil(depth / std::max(.01, model_->number("pass")))), 1, 24);
        for (int i = 1; i <= passes; ++i) {
            const double y = 162 + start + z * i / passes;
            p.setPen(QPen(QColor("#b3a58f"), 1, Qt::DashLine)); p.drawLine(QPointF(175, y), QPointF(505, y));
        }
        drawRoute(p, route_);
        dimension(p, {535, 143}, {535, 162 + start}, "D " + mm(model_->number("start")));
        dimension(p, {535, 162 + start}, {535, 162 + start + z}, "C " + mm(depth));
        text(p, {100, 355, 500, 25}, QString("%1 passes  ·  Passo Z %2").arg(passes).arg(mm(model_->number("pass"))), 14);
    } else if (topic == 3) {
        const auto stock = projection_.map(rounded(shape_.boundingRect().adjusted(-30, -30, 30, 30)));
        const auto kerf = projection_.map(offset(shape_, 9));
        pocket(p, stock, kerf, 30);
        // The part and stock share the same top plane; only the kerf is recessed.
        timberTop(p, top);
        const int count = model_->integer("tabs") ? std::clamp(model_->integer("tabCount"), 0, 200) : 0;
        const double length = std::clamp(model_->number("tabLength") * 2, 4.0, 55.0);
        for (int i = 0; i < count; ++i) {
            const double t = (i + .5) / count;
            const QPointF pt = pointAt(top, t), tangent = pointAt(top, std::min(.9999, t + .003)) - pointAt(top, std::max(0.0, t - .003));
            p.save(); p.translate(pt); p.rotate(std::atan2(tangent.y(), tangent.x()) * 180 / pi);
            QLinearGradient bridge(0, -10, 0, 12); bridge.setColorAt(0, QColor("#ffd398")); bridge.setColorAt(1, QColor("#ce762d"));
            p.setBrush(bridge); p.setPen(QPen(QColor("#ffd092"), .9));
            p.drawRoundedRect(QRectF(-length / 2, -13, length, 26), model_->integer("tabs3d") ? 5 : 0, 3); p.restore();
        }
        // Enlarged cross-section: width and remaining height are both parameter-driven.
        const double h = std::clamp(model_->number("tabHeight") * 9, 1.0, 64.0);
        const double w = std::clamp(model_->number("tabLength") * 5, 12.0, 90.0);
        p.fillRect(QRectF(551, 225, 145, 60), QColor("#6f5138"));
        QPainterPath bridge; bridge.moveTo(560, 281); bridge.lineTo(568, 281);
        if (model_->integer("tabs3d")) { bridge.lineTo(568 + w / 2, 281 - h); bridge.lineTo(568 + w, 281); }
        else { bridge.lineTo(568, 281 - h); bridge.lineTo(568 + w, 281 - h); bridge.lineTo(568 + w, 281); }
        bridge.lineTo(690, 281); p.setBrush(Qt::NoBrush); p.setPen(QPen(orange, 3)); p.drawPath(bridge);
        text(p, {550, 110, 150, 50}, count ? QString("%1 pontes\n%2").arg(count).arg(mode_label_text()) : "Pontes desativadas", 15, orange);
        text(p, {550, 302, 155, 45}, "L " + mm(model_->number("tabLength")) + "\nH " + mm(model_->number("tabHeight")), 13);
        const double t = model_->integer("reversePreview") ? 1 - progress_ : progress_;
        double lift = 0;
        if (count > 0) for (int i = 0; i < count; ++i) {
            const double d = std::abs(t - (i + .5) / count);
            const double span = length / std::max(1.0, top.length());
            if (d < span) lift = h * .4 * (model_->integer("tabs3d") ? 1 - d / span : 1);
        }
        drawTool(p, pointAt(top, t) - QPointF(0, lift), 23);
    } else if (topic == 7 && model_->integer("ramp") <= 1) {
        const double cut = std::clamp(depth * 15, 0.0, 155.0);
        const double distance = std::clamp(model_->number("rampDistance") * 5, 25.0, 290.0);
        QPainterPath block; block.addRect(QRectF(78, 152, 430, 190));
        timber(p, block, 18);
        QPainterPath upper; upper.addPolygon(QPolygonF{{78,152},{130,113},{560,113},{508,152}}); upper.closeSubpath(); timberTop(p, upper);
        QPainterPath removed; removed.moveTo(155, 152); removed.lineTo(508, 152);
        removed.lineTo(508, 152 + cut); removed.lineTo(model_->integer("ramp") == 0 ? 155 : 155 + distance, 152 + cut); removed.closeSubpath();
        QLinearGradient recess(155, 152, 480, 320); recess.setColorAt(0, QColor("#302c29")); recess.setColorAt(1, QColor("#9c805b"));
        p.fillPath(removed, recess);
        p.setPen(QPen(orange, 2)); p.drawLine(QPointF(155 + distance, 152 + cut), QPointF(508, 152 + cut));
        drawRoute(p, route_);
        dimension(p, {155, 365}, {155 + distance, 365}, mm(model_->number("rampDistance")), {0, 35});
        dimension(p, {535, 152}, {535, 152 + cut}, "C " + mm(depth));
        text(p, {550, 110, 150, 30}, mode_label_text(), 17, cyan);
        const double angle = std::atan2(depth, std::max(.001, model_->number("rampDistance"))) * 180 / pi;
        text(p, {550, 304, 150, 30}, QString("Ângulo %1°").arg(angle, 0, 'f', 1), 14, orange);
    } else if (topic == 5 || topic == 7 || topic == 6) {
        const double cut = std::clamp(depth * (topic == 6 ? 3 : 9), 0.0, topic == 6 ? 65.0 : 130.0);
        const auto stock = projection_.map(rounded(shape_.boundingRect().adjusted(-24, -24, 24, 24), 32));
        if (topic == 6) pocket(p, stock, top, cut);
        else {
            // A genuine open-front section: base, recessed floor and a U-shaped
            // remaining wall, not a dark rectangle painted over a finished block.
            timber(p, stock.translated(0, cut), 17);
            timberTop(p, top.translated(0, cut), true);
            QPainterPath front;
            front.addRect(QRectF(shape_.boundingRect().left(), shape_.boundingRect().center().y(),
                                 shape_.boundingRect().width(), shape_.boundingRect().height()));
            const QPainterPath opening = shape_.united(front);
            const auto rim = stock.subtracted(projection_.map(opening));
            timber(p, rim, cut);
            p.setPen(QPen(QColor("#9c8564"), .8, Qt::DashLine));
            p.drawPath(top.translated(0, cut));
        }
        drawRoute(p, route_);
        text(p, {555, 125, 150, 55}, mode_label_text(), 17, cyan);
        text(p, {555, 195, 150, 28}, "C " + mm(depth), 14);
        if (topic == 6) text(p, {555, 230, 155, 45}, "Passo lateral\n" + mm(model_->number("stepover")), 13, muted);
        else text(p, {555, 230, 155, 65}, "Distância\n" + mm(model_->number("rampDistance")), 13, muted);
    } else if (topic == 8) {
        const double z = std::clamp(model_->number("thickness") * 2.4, 10.0, 100.0);
        const auto stock = projection_.map(rounded(QRectF(110, 135, 380, 235), 2)); timber(p, stock, z);
            const double x = 110 + model_->integer("originX") * 190;
        const double y = 370 - model_->integer("originY") * 117.5;
        QPointF origin = projection_.map(QPointF(x, y)); if (model_->integer("zeroZ")) origin += QPointF(0, z);
        arrow(p, origin, origin + QPointF(80, -10), QColor("#ef6a62")); text(p, QRectF(origin + QPointF(83, -24), QSizeF(24, 24)), "X", 17);
        arrow(p, origin, origin + QPointF(35, -55), QColor("#73cc77")); text(p, QRectF(origin + QPointF(36, -79), QSizeF(24, 24)), "Y", 17);
        arrow(p, origin, origin + QPointF(0, -90), QColor("#6f9eff")); text(p, QRectF(origin + QPointF(-7, -117), QSizeF(24, 24)), "Z", 17);
        p.setPen(QPen(ink, 1)); p.setBrush(orange); p.drawEllipse(origin, 5, 5);
        const double clearance = std::clamp(model_->number("clearance") * 4, 8.0, 75.0);
        p.setPen(QPen(QColor("#91acc5"), 1, Qt::DashLine)); p.setBrush(QColor(64, 117, 151, 22)); p.drawPath(stock.translated(0, -clearance));
        dimension(p, {535, 234}, {535, 234 + z}, mm(model_->number("thickness")));
        text(p, {110, 355 + z / 2, 400, 26}, mm(model_->number("width")) + " × " + mm(model_->number("height")), 13);
        text(p, {555, 100, 150, 35}, "Origem XY", 14, muted);
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
            const bool selected = col == model_->integer("originX") && 2 - row == model_->integer("originY");
            p.setBrush(selected ? orange : QColor("#304454")); p.setPen(QPen(selected ? ink : muted, 1));
            p.drawEllipse(QPointF(574 + col * 42, 155 + row * 30), selected ? 7 : 5, selected ? 7 : 5);
        }
        text(p, {550, 305, 160, 26}, mode_label_text(), 13);
        text(p, {550, 340, 160, 40}, "Segurança\n" + mm(model_->number("clearance")), 12, muted);
    } else if (topic == 9) {
        static const int types[] = {0, 1, 2, 4, 3, 5, 6};
        static const QStringList names = {"Topo", "Esférica", "V", "Com raio", "Gravação", "Cônica", "Broca"};
        for (int i = 0; i < 7; ++i) {
            const double x = 55 + i * 101;
            const bool selected = model_->integer("toolType") == types[i];
            if (selected) {
                p.setPen(QPen(cyan, 1.2)); p.setBrush(QColor(41, 217, 239, 13)); p.drawRoundedRect(QRectF(x - 44, 77, 88, 290), 8, 8);
            }
            drawTool(p, QPointF(x, 263), std::clamp(model_->number("diameter") * 4 + 12, 22.0, 45.0), types[i]);
            text(p, {x - 50, 274, 100, 24}, names[i], 12, selected ? cyan : ink, Qt::AlignCenter);
            QPainterPath section; section.addRect(QRectF(x - 35, 315, 70, 36)); timberTop(p, section);
            QPainterPath cut; cut.moveTo(x - 16, 315);
            if (types[i] == 1) { cut.lineTo(x - 16, 321); cut.cubicTo(x - 16, 344, x + 16, 344, x + 16, 321); }
            else if (types[i] == 2 || types[i] == 3) cut.lineTo(x, 341);
            else { cut.lineTo(x - 16, 338); cut.lineTo(x + 16, 338); }
            cut.lineTo(x + 16, 315); cut.closeSubpath(); p.fillPath(cut, QColor("#1b252e"));
        }
        text(p, {80, 382, 560, 25}, mode_label_text() + "  ·  Ø " + mm(model_->number("diameter")), 15, cyan, Qt::AlignCenter);
    }
}

void MachiningGuide::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, QColor("#222222"));
    background.setColorAt(1, QColor("#181818"));
    p.fillRect(rect(), background);
    p.setPen(QPen(QColor("#333333"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
    if (property("compact").toBool()) {
        // Header thumbnail uses the same textured material as the full-size guide.
        const double aspect = std::clamp(model_->number("width") / std::max(1.0, model_->number("height")), .4, 2.5);
        const QPainterPath stock = projection_.map(rounded(QRectF(0, 0, 240 * aspect, 240), 2));
        const double thickness = std::clamp(model_->number("thickness") * 1.5, 8.0, 70.0);
        const QRectF bounds = stock.boundingRect().adjusted(-15, -15, 22, thickness + 22);
        const double scale = std::min(width() / bounds.width(), height() / bounds.height());
        p.translate(width() / 2.0, height() / 2.0); p.scale(scale, scale); p.translate(-bounds.center());
        timber(p, stock, thickness);
        const QPointF origin = projection_.map(QPointF(model_->integer("originX") * 120 * aspect,
                                                     (2 - model_->integer("originY")) * 120));
        const QPointF zero = origin + QPointF(0, model_->integer("zeroZ") == 1 ? thickness : 0);
        p.setPen(QPen(orange, 3)); p.drawLine(zero - QPointF(12, 0), zero + QPointF(12, 0));
        p.drawLine(zero - QPointF(0, 12), zero + QPointF(0, 12));
        return;
    }
    const double scale = std::min(width() / 720.0, height() / 450.0);
    p.translate((width() - 720 * scale) / 2, (height() - 450 * scale) / 2); p.scale(scale, scale);
    text(p, {23, 16, 670, 30}, kind_.left(2) + "   " + title(kind_), 20);
    text(p, {24, 48, 665, 23}, model_->hasSelection() ? "Geometria selecionada · prévia dos parâmetros" : "Modelo demonstrativo · parâmetros em tempo real", 12, muted);
    p.save(); p.setClipRect(QRectF(12, 75, 696, 338)); drawScene(p); p.restore();
    text(p, {24, 421, 675, 19}, "Prévia didática · valide o percurso calculado e a simulação antes de usinar", 11, muted);
}
void MachiningGuide::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || property("readOnlyPreview").toBool()) return;
    const double scale = std::min(width() / 720.0, height() / 450.0);
    const QPointF pt = (e->position() - QPointF((width() - 720 * scale) / 2, (height() - 450 * scale) / 2)) / scale;
    if (kind_.startsWith("08")) {
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
            if (QLineF(pt, QPointF(574 + col * 42, 155 + row * 30)).length() < 15) {
                model_->setValue("originX", col); model_->setValue("originY", 2 - row); return;
            }
    } else if (kind_.startsWith("09") && pt.y() > 77 && pt.y() < 365) {
        if (model_->property("toolReadOnly").toBool()) return; // choose actual tools in the database
        static const int types[] = {0, 1, 2, 4, 3, 5, 6};
        const int index = int((pt.x() - 5) / 101);
        if (index >= 0 && index < 7) { set_mode(types[index]); if (mode_clicked) mode_clicked(mode()); }
    }
}

QWidget* guidePlayback(MachiningGuide* guide, QWidget* parent) {
    auto* widget = new QWidget(parent); auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 2, 0, 2); layout->setSpacing(5);
    auto* play = new QPushButton("▶ Animar", widget); play->setObjectName("GuidePlay"); play->setCheckable(true);
    auto* reset = new QPushButton("↺", widget); reset->setToolTip("Reiniciar animação"); reset->setMaximumWidth(35);
    auto* scrub = new QSlider(Qt::Horizontal, widget); scrub->setObjectName("GuideProgress"); scrub->setRange(0, 1000); scrub->setValue(qRound(guide->progress() * 1000)); scrub->setMinimumWidth(35);
    auto* speed = new QComboBox(widget); speed->setObjectName("GuideSpeed");
    speed->addItem("0,5×", .5); speed->addItem("1×", 1.0); speed->addItem("2×", 2.0); speed->setCurrentIndex(1); speed->setToolTip("Velocidade da demonstração (não altera o avanço)");
    layout->addWidget(play); layout->addWidget(reset); layout->addWidget(scrub, 1); layout->addWidget(speed);
    QObject::connect(play, &QPushButton::toggled, guide, &MachiningGuide::setPlaying);
    QObject::connect(guide, &MachiningGuide::playingChanged, widget, [play](bool active) { QSignalBlocker block(play); play->setChecked(active); play->setText(active ? "Ⅱ Pausar" : "▶ Animar"); });
    QObject::connect(reset, &QPushButton::clicked, guide, [guide] { guide->setProgress(0); });
    QObject::connect(scrub, &QSlider::sliderPressed, guide, [guide] { guide->setPlaying(false); });
    QObject::connect(scrub, &QSlider::valueChanged, guide, [guide](int value) { guide->setProgress(value / 1000.0); });
    QObject::connect(guide, &MachiningGuide::progressChanged, widget, [scrub](double value) { QSignalBlocker block(scrub); scrub->setValue(qRound(value * 1000)); });
    QObject::connect(speed, &QComboBox::currentIndexChanged, guide, [guide, speed] { guide->setSpeed(speed->currentData().toDouble()); });
    return widget;
}

QWidget* guideDemoControls(MachiningGuide* guide, QWidget* parent) {
    auto* widget = new QWidget(parent); auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0); form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* model = guide->model();
    const auto choice = [form, widget, model](QString label, QString key, QStringList items) {
        auto* field = new QComboBox(widget); field->setObjectName("Guide_" + key); field->addItems(items); field->setCurrentIndex(model->integer(key));
        field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); field->setMinimumContentsLength(5);
        form->addRow(label, field);
        QObject::connect(field, &QComboBox::currentIndexChanged, model, [model, key](int value) { model->setValue(key, value); });
        QObject::connect(model, &GuideModel::valueChanged, field, [field, key](const QString& changed, const QVariant& value) { if (key == changed) { QSignalBlocker b(field); field->setCurrentIndex(value.toInt()); } });
    };
    const auto number = [form, widget, model](QString label, QString key, double min, double max, QString suffix = " mm") {
        auto* field = new QDoubleSpinBox(widget); field->setObjectName("Guide_" + key); field->setRange(min, max); field->setDecimals(key == "points" || key == "tabCount" ? 0 : 2); field->setSuffix(suffix); field->setValue(model->number(key)); form->addRow(label, field);
        QObject::connect(field, &QDoubleSpinBox::valueChanged, model, [model, key](double value) { model->setValue(key, value); });
        QObject::connect(model, &GuideModel::valueChanged, field, [field, key](const QString& changed, const QVariant& value) { if (key == changed) { QSignalBlocker b(field); field->setValue(value.toDouble()); } });
    };
    const int topic = guide->kind().left(2).toInt();
    if (!MachiningGuide::modeKey(guide->kind()).isEmpty()) choice("Modo", MachiningGuide::modeKey(guide->kind()), MachiningGuide::modeLabels(guide->kind()));
    if (topic == 1 || topic == 3 || topic == 4 || topic == 6) choice("Forma", "shape", {"Contorno em L", "Retângulo", "Círculo", "Estrela"});
    if (topic == 1 || topic == 6) {
        choice("Sentido do corte", "direction", {"Convencional", "Concordante"});
        choice("Reprodução", "reversePreview", {"Início → fim", "Fim → início (só prévia)"});
    }
    if (topic == 1 || topic == 9) number("Diâmetro", "diameter", .1, 30);
    if (topic == 2 || topic == 5 || topic == 6 || topic == 7) number("Profundidade", "depth", .1, 18);
    if (topic == 2) { number("Início", "start", 0, 6); number("Passo Z", "pass", .1, 10); }
    if (topic == 3) { number("Quantidade", "tabCount", 0, 24, ""); number("Largura", "tabLength", 1, 25); number("Altura", "tabHeight", .1, 8); }
    if (topic == 4) { number("Comprimento", "leadLength", 1, 25); number("Raio", "leadRadius", 1, 25); }
    if (topic == 5 || topic == 7) number("Distância", "rampDistance", 1, 65);
    if (topic == 6) { number("Passo lateral", "stepover", 1, 15); number("Ângulo raster", "rasterAngle", -180, 180, " °"); }
    if (topic == 8) { number("Espessura", "thickness", 1, 40); number("Segurança", "clearance", 0, 20); }
    if (topic == 10) { number("Pontas", "points", 3, 32, ""); number("Raio externo", "radius", 1, 1000); number("Proporção interna", "innerRatio", .1, .9, ""); }
    return widget;
}

void showGuideGallery(QWidget* parent) {
    ResponsiveDialog dialog(parent); dialog.setObjectName("GuideGallery"); dialog.setWindowTitle("Desenhos CAM · estúdio interativo");
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel("Acabamento aprovado · madeira, metal e trajetórias renderizados em tempo real", &dialog); intro->setWordWrap(true); layout->addWidget(intro);
    auto* topics = new QComboBox(&dialog); topics->setObjectName("GalleryTopic");
    const QStringList keys = {"01_usinar_vetores", "02_profundidades", "03_pontes", "04_entradas_saidas", "05_rampas", "06_limpar_area", "07_ataque_rampeado", "08_material", "09_ferramentas", "10_estrela"};
    for (const auto& key : keys) topics->addItem(key.left(2) + " · " + MachiningGuide::title(key), key);
    layout->addWidget(topics);
    auto* guide = new MachiningGuide(keys.first(), &dialog); guide->setObjectName("GalleryRender"); layout->addWidget(guide, 1);
    layout->addWidget(guidePlayback(guide, &dialog));
    auto* controls = new QWidget(&dialog); auto* controlLayout = new QVBoxLayout(controls); controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->addWidget(guideDemoControls(guide, controls)); layout->addWidget(controls);
    QObject::connect(topics, &QComboBox::currentIndexChanged, &dialog, [=] {
        guide->set_kind(topics->currentData().toString());
        while (auto* item = controlLayout->takeAt(0)) { delete item->widget(); delete item; }
        controlLayout->addWidget(guideDemoControls(guide, controls));
        guide->setProgress(0);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(900, 850); dialog.exec();
}
} // namespace cnc::ui
