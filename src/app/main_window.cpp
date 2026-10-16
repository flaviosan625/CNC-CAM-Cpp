#include "app/main_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QListWidget>
#include <QResizeEvent>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QTabWidget>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/theme.hpp"
#include "ui/machining_guide.hpp"
#include "ui/responsive.hpp"
#include "ui/icons.hpp"
#include "ui/layer_manager_dialog.hpp"
#include "ui/quick_edit.hpp"
#include "ui/safe_inputs.hpp"
#include "cad/layout.hpp"
#include "cad/selection.hpp"
#include "cam/gcode.hpp"
#include "cam/job_sheet.hpp"
#include "cam/presets.hpp"
#include "cam/setup_sheet.hpp"
#include "cam/templates.hpp"
#include "cam/vectric_tools.hpp"
#include "io/importers.hpp"
#include "tools/draw_tools.hpp"
#include "tools/edit_tools.hpp"
#include "tools/transform_tools.hpp"
#include "viewport/canvas_2d.hpp"
#include "viewport/gl_widget.hpp"

namespace cnc::app {

namespace {

QString format_simulation_time(double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    if (total >= 3600) {
        return QString("%1:%2:%3")
            .arg(total / 3600)
            .arg((total % 3600) / 60, 2, 10, QChar('0'))
            .arg(total % 60, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(total / 60, 2, 10, QChar('0'))
                            .arg(total % 60, 2, 10, QChar('0'));
}

std::array<float, 3> material_display_color(const cad::MaterialSetup& material) {
    QColor color("#e6c99e");
    if (material.appearance == "oak") color = QColor("#c79e68");
    else if (material.appearance == "walnut") color = QColor("#9d7557");
    else if (material.appearance == "mdf") color = QColor("#c5a778");
    else if (material.appearance == "aluminium") color = QColor("#c9ced4");
    else if (material.appearance == "solid") color = QColor(QString::fromStdString(material.solid_color));
    if (!color.isValid()) color = QColor("#e6c99e");
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF())};
}

cam::Tool simulation_tool_for_result(const cam::ToolpathJob& job, const cam::Toolpath& path) {
    std::vector<const cam::Tool*> candidates = {&job.tool};
    if (const auto* vcarve = std::get_if<cam::VCarveSettings>(&job.settings)) {
        for (const auto& tool : vcarve->clearance_tools) candidates.push_back(&tool);
    }
    for (const cam::Tool* tool : candidates) {
        if (tool->name == path.tool_name && tool->tool_number == path.tool_number) return *tool;
    }
    for (const cam::Tool* tool : candidates) {
        if (tool->name == path.tool_name) return *tool;
    }
    return job.tool;
}

class ToolTileWidget final : public QWidget {
public:
    explicit ToolTileWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
    }

    void set_button(QToolButton* button) { button_ = button; }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (button_ && event->button() == Qt::LeftButton) {
            button_->setDown(true);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (button_ && event->button() == Qt::LeftButton) {
            button_->setDown(false);
            if (rect().contains(event->position().toPoint())) {
                button_->setFocus();
                button_->click();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QToolButton* button_ = nullptr;
};

class AspectPixmapLabel final : public QLabel {
public:
    explicit AspectPixmapLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(0, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void set_source_image(const QImage& image) {
        source_ = image;
        update_pixmap();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        update_pixmap();
    }

private:
    void update_pixmap() {
        if (source_.isNull() || width() <= 0 || height() <= 0) {
            clear();
            return;
        }
        setPixmap(QPixmap::fromImage(source_).scaled(size(), Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
    }
    QImage source_;
};

using InteractiveGuideWidget = ui::MachiningGuide;

}  // namespace

// This renderer is exclusive to the OS perspective; profile guides continue
// using their original interactive MachiningGuide illustrations.
QImage make_job_layout_preview(const std::vector<cam::SetupSheetOperation>& operations,
                                const cad::MaterialSetup* material,
                                const JobPreviewOptions& options) {
    const double canvas_w = std::max(320, options.width);
    const double supersample = std::clamp(options.supersample, 1.0, 6.0);
    const auto make_canvas = [&](double height) {
        QImage image(qRound(canvas_w * supersample), qRound(height * supersample),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#0b1521"));
        return image;
    };
    const auto start_painter = [&](QPainter& painter) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // Everything below works in logical units; the scale keeps the strokes
        // and the lettering crisp at the resolution the sheet is printed at.
        painter.scale(supersample, supersample);
    };

    // Only the selected toolpath is drawn on a per-operation sheet.
    const auto included = [&](size_t index) {
        return options.only_operation < 0 ||
               static_cast<size_t>(options.only_operation) == index;
    };

    std::optional<BBox> cut_bounds;
    double cut_z_min = 0.0, cut_z_max = 0.0;
    bool has_cut_z = false;
    for (size_t op_index = 0; op_index < operations.size(); ++op_index) {
        const auto& operation = operations[op_index];
        if (!operation.path || !included(op_index)) continue;
        for (const cam::Move& move : operation.path->moves) {
            if (!move.cutting()) continue;
            if (!cut_bounds) cut_bounds = BBox{move.x, move.y, move.x, move.y};
            else {
                cut_bounds->x0 = std::min(cut_bounds->x0, move.x);
                cut_bounds->y0 = std::min(cut_bounds->y0, move.y);
                cut_bounds->x1 = std::max(cut_bounds->x1, move.x);
                cut_bounds->y1 = std::max(cut_bounds->y1, move.y);
            }
            if (!has_cut_z) {
                cut_z_min = cut_z_max = move.z;
                has_cut_z = true;
            } else {
                cut_z_min = std::min(cut_z_min, move.z);
                cut_z_max = std::max(cut_z_max, move.z);
            }
        }
    }

    const bool has_material = material && material->width > 0.0 && material->height > 0.0;
    if (!cut_bounds && !has_material) {
        QImage image = make_canvas(options.min_height);
        QPainter painter(&image);
        start_painter(painter);
        painter.setPen(QColor("#cbd5e1"));
        painter.setFont(QFont("Segoe UI", 12, QFont::DemiBold));
        painter.drawText(QRectF(0, 0, canvas_w, options.min_height), Qt::AlignCenter,
                         "Sem percursos de corte calculados");
        return image;
    }

    const double board_width = has_material ? material->width
                                             : std::max(100.0, cut_bounds->x1 - cut_bounds->x0 + 20.0);
    const double board_height = has_material ? material->height
                                              : std::max(100.0, cut_bounds->y1 - cut_bounds->y0 + 20.0);
    const double thickness = has_material ? material->thickness : 15.0;
    const double z_top = has_material ? material->surface_z() : 0.0;
    const double z_bottom = z_top - thickness;
    // Paths and stock use drawing coordinates. The configured work zero is a
    // point *on* that stock; it must not shift the stock geometry itself.
    const double mat_x0 = has_material ? 0.0 : cut_bounds->x0 - 10.0;
    const double mat_y0 = has_material ? 0.0 : cut_bounds->y0 - 10.0;
    const double mat_x1 = mat_x0 + board_width;
    const double mat_y1 = mat_y0 + board_height;
    const double origin_x = has_material ? material->origin_xy().x : 0.0;
    const double origin_y = has_material ? material->origin_xy().y : 0.0;
    const double origin_z = has_material && material->zero_z == "bottom" ? z_bottom : z_top;
    const double cut_width = cut_bounds ? cut_bounds->x1 - cut_bounds->x0 : 0.0;
    const double cut_height = cut_bounds ? cut_bounds->y1 - cut_bounds->y0 : 0.0;
    const double area_m2 = board_width * board_height / 1000000.0;

    constexpr double x_axis_x = .92, x_axis_y = .28;
    // In this view the sheet's (0, 0) corner is the left vertex. X advances
    // down/right and Y advances up/right, matching the 2D sheet orientation.
    constexpr double y_axis_x = .92, y_axis_y = -.28;
    constexpr double z_scale = .9;
    const auto raw_project = [&](double x, double y, double z) {
        const double dx = x - mat_x0;
        const double dy = y - mat_y0;
        return QPointF(x_axis_x * dx + y_axis_x * dy,
                       x_axis_y * dx + y_axis_y * dy - (z - z_top) * z_scale);
    };

    double raw_x0 = std::numeric_limits<double>::max();
    double raw_y0 = std::numeric_limits<double>::max();
    double raw_x1 = std::numeric_limits<double>::lowest();
    double raw_y1 = std::numeric_limits<double>::lowest();
    const auto include_raw = [&](const QPointF& point) {
        raw_x0 = std::min(raw_x0, point.x());
        raw_y0 = std::min(raw_y0, point.y());
        raw_x1 = std::max(raw_x1, point.x());
        raw_y1 = std::max(raw_y1, point.y());
    };
    const std::array<QPointF, 4> board_corners = {
        QPointF(mat_x0, mat_y0), QPointF(mat_x1, mat_y0),
        QPointF(mat_x1, mat_y1), QPointF(mat_x0, mat_y1)};
    for (const QPointF& corner : board_corners) {
        include_raw(raw_project(corner.x(), corner.y(), z_top));
        include_raw(raw_project(corner.x(), corner.y(), z_bottom));
    }
    // Offsets can intentionally place work zero just beyond the board edge.
    // Keep the marker in frame without changing the board or cutting paths.
    include_raw(raw_project(origin_x, origin_y, origin_z));
    for (size_t op_index = 0; op_index < operations.size(); ++op_index) {
        const auto& operation = operations[op_index];
        if (!operation.path || !included(op_index)) continue;
        for (const cam::Move& move : operation.path->moves)
            if (move.cutting()) include_raw(raw_project(move.x, move.y, move.z));
    }
    const double raw_width = std::max(1.0, raw_x1 - raw_x0);
    const double raw_height = std::max(1.0, raw_y1 - raw_y0);

    // The cotas sit in a strip under the stock rather than in a side column, so
    // the drawing keeps nearly the full width.  The canvas is then made exactly
    // as tall as the projected stock needs, which is what stops the sheet from
    // padding the drawing with dead space instead of enlarging it.
    constexpr double kDrawingTop = 70.0;
    constexpr double kLegendHeight = 62.0;
    constexpr double kChromeHeight = kDrawingTop + kLegendHeight + 24.0;
    const QRectF drawing_probe(18.0, kDrawingTop, canvas_w - 36.0, 1.0);
    const double wanted = drawing_probe.width() / (raw_width / raw_height);
    const double canvas_h = std::clamp(wanted + kChromeHeight,
                                       static_cast<double>(options.min_height),
                                       static_cast<double>(options.max_height));

    QImage image = make_canvas(canvas_h);
    QPainter painter(&image);
    start_painter(painter);

    const QRectF header(12, 6, canvas_w - 24, 32);
    QPainterPath header_path;
    header_path.addRoundedRect(header, 15, 15);
    painter.setBrush(QLinearGradient(header.topLeft(), header.bottomLeft()));
    QLinearGradient header_gradient(header.topLeft(), header.bottomLeft());
    header_gradient.setColorAt(0.0, QColor("#111f31"));
    header_gradient.setColorAt(1.0, QColor("#1b2a3d"));
    painter.setBrush(header_gradient);
    painter.setPen(QPen(QColor("#29d9ef"), 1.1));
    painter.drawPath(header_path);
    painter.setPen(QColor("#f5f8fc"));
    painter.setFont(QFont("Segoe UI", 9.5, QFont::DemiBold));
    QString header_text = QString("CHAPA %1 × %2 × %3 mm  |  CORTE X %4 × Y %5 mm  |  Z %6…%7 mm  |  ZERO Z: %8")
        .arg(board_width, 0, 'f', 1).arg(board_height, 0, 'f', 1)
        .arg(thickness, 0, 'f', 1).arg(cut_width, 0, 'f', 1)
        .arg(cut_height, 0, 'f', 1)
        .arg(has_cut_z ? cut_z_min : z_top, 0, 'f', 2)
        .arg(has_cut_z ? cut_z_max : z_top, 0, 'f', 2)
        .arg(has_material && material->zero_z == "bottom" ? "BASE DA CHAPA" : "TOPO DA CHAPA");
    painter.drawText(header.adjusted(12, 0, -12, 0), Qt::AlignCenter, header_text);

    const QRectF viewport(12, 46, canvas_w - 24, canvas_h - 62);
    QLinearGradient viewport_gradient(viewport.topLeft(), viewport.bottomLeft());
    viewport_gradient.setColorAt(0.0, QColor("#172636"));
    viewport_gradient.setColorAt(1.0, QColor("#0d1825"));
    painter.setBrush(viewport_gradient);
    painter.setPen(QPen(QColor("#344b61"), 1.0));
    painter.drawRoundedRect(viewport, 7, 7);

    QString badge_text = "VISTA 3D ISO · PERCURSO REAL E MATERIAL";
    if (options.only_operation >= 0 &&
        static_cast<size_t>(options.only_operation) < operations.size() &&
        operations[options.only_operation].path) {
        badge_text = QString("PERCURSO %1 · %2")
                         .arg(options.only_operation + 1)
                         .arg(QString::fromStdString(operations[options.only_operation].path->name));
    }
    const QRectF badge(22, 52, std::max(270.0, badge_text.size() * 6.4 + 28.0), 20);
    painter.setBrush(QColor("#1879ad"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(badge, 4, 4);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 8.5, QFont::Bold));
    painter.drawText(badge, Qt::AlignCenter, badge_text);

    const QRectF legend(18.0, canvas_h - kLegendHeight - 16.0, canvas_w - 36.0, kLegendHeight);
    const QRectF drawing_area(18.0, kDrawingTop, canvas_w - 36.0,
                              legend.top() - kDrawingTop - 6.0);
    const double projection_scale = std::min(drawing_area.width() / raw_width,
                                              drawing_area.height() / raw_height);
    const QPointF raw_center((raw_x0 + raw_x1) / 2.0, (raw_y0 + raw_y1) / 2.0);
    const auto project = [&](double x, double y, double z) {
        const QPointF point = raw_project(x, y, z);
        return drawing_area.center() + (point - raw_center) * projection_scale;
    };

    std::array<QPointF, 4> top_face, bottom_face;
    for (size_t i = 0; i < board_corners.size(); ++i) {
        top_face[i] = project(board_corners[i].x(), board_corners[i].y(), z_top);
        bottom_face[i] = project(board_corners[i].x(), board_corners[i].y(), z_bottom);
    }
    QPolygonF top_polygon;
    for (const QPointF& point : top_face) top_polygon << point;
    const QPolygonF front_face{top_face[0], top_face[1], bottom_face[1], bottom_face[0]};
    const QPolygonF right_face{top_face[1], top_face[2], bottom_face[2], bottom_face[1]};

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 75));
    painter.drawPolygon(top_polygon.translated(7, 12));
    painter.setBrush(QLinearGradient(right_face.boundingRect().topLeft(), right_face.boundingRect().bottomLeft()));
    QLinearGradient right_shade(right_face.boundingRect().topLeft(), right_face.boundingRect().bottomLeft());
    right_shade.setColorAt(0.0, QColor("#b89261"));
    right_shade.setColorAt(1.0, QColor("#65452e"));
    painter.setBrush(right_shade);
    painter.drawPolygon(right_face);
    QLinearGradient front_shade(front_face.boundingRect().topLeft(), front_face.boundingRect().bottomLeft());
    front_shade.setColorAt(0.0, QColor("#c19a68"));
    front_shade.setColorAt(1.0, QColor("#795535"));
    painter.setBrush(front_shade);
    painter.drawPolygon(front_face);
    painter.restore();

    painter.save();
    QPainterPath top_clip;
    top_clip.addPolygon(top_polygon);
    painter.setClipPath(top_clip);
    const QRectF texture_bounds = top_polygon.boundingRect();
    painter.drawImage(texture_bounds, ui::woodTexture());
    QLinearGradient wood_light(texture_bounds.topLeft(), texture_bounds.bottomRight());
    wood_light.setColorAt(0.0, QColor(255, 246, 222, 78));
    wood_light.setColorAt(1.0, QColor(45, 30, 19, 56));
    painter.fillRect(texture_bounds, wood_light);
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#f0d4a9"), 1.35));
    painter.drawPolygon(top_polygon);

    // Layer lines make the stock thickness readable in the perspective view.
    for (int layer = 1; layer <= 3; ++layer) {
        const double z = z_top - thickness * layer / 4.0;
        painter.setPen(QPen(QColor(72, 43, 24, 90), .8));
        painter.drawLine(project(mat_x0, mat_y0, z), project(mat_x1, mat_y0, z));
        painter.drawLine(project(mat_x1, mat_y0, z), project(mat_x1, mat_y1, z));
    }

    // Draw a clipped, inset safety margin and a material name plate.
    if (has_material && material->margin > 0.0 &&
        board_width > material->margin * 2 && board_height > material->margin * 2) {
        const std::array<QPointF, 4> margin_corners = {
            QPointF(mat_x0 + material->margin, mat_y0 + material->margin),
            QPointF(mat_x1 - material->margin, mat_y0 + material->margin),
            QPointF(mat_x1 - material->margin, mat_y1 - material->margin),
            QPointF(mat_x0 + material->margin, mat_y1 - material->margin)};
        QPolygonF margin_polygon;
        for (const QPointF& corner : margin_corners)
            margin_polygon << project(corner.x(), corner.y(), z_top);
        painter.setPen(QPen(QColor("#f3e4c8"), .8, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(margin_polygon);
    }
    const QRectF material_tag(top_polygon.boundingRect().topRight() + QPointF(-122, 3), QSizeF(118, 19));
    painter.setBrush(QColor(13, 25, 38, 210));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(material_tag, 3, 3);
    painter.setPen(QColor("#fff8e9"));
    painter.setFont(QFont("Segoe UI", 7.6, QFont::Bold));
    painter.drawText(material_tag, Qt::AlignCenter,
        QString("%1 × %2 × %3 mm").arg(board_width, 0, 'f', 0)
                                      .arg(board_height, 0, 'f', 0)
                                      .arg(thickness, 0, 'f', 1));

    const std::array<QColor, 7> operation_colors = {
        QColor("#29d9ef"), QColor("#ffae55"), QColor("#55d68b"), QColor("#b49cff"),
        QColor("#f27ca2"), QColor("#61a6ff"), QColor("#f2d35e")};
    struct DirectionMark { QPointF tip; QPointF direction; QColor color; };
    std::vector<DirectionMark> direction_marks;
    std::optional<QPointF> first_cut_point;
    std::optional<QPointF> first_cut_xy;
    std::optional<double> deepest_cut;
    for (size_t op_index = 0; op_index < operations.size(); ++op_index) {
        const auto* path = operations[op_index].path;
        if (!path || path->moves.empty() || !included(op_index)) continue;
        const cam::Tool* tool = operations[op_index].job ? &operations[op_index].job->tool : nullptr;
        const double diameter = tool ? tool->diameter : 4.0;
        const double kerf = std::clamp(diameter * projection_scale, 3.0, 12.0);
        QPainterPath cut_path;
        bool have_previous = false, previous_cutting = false;
        QPointF previous_point;
        double distance_since_arrow = 0.0;
        for (const cam::Move& move : path->moves) {
            const QPointF point = project(move.x, move.y, move.z);
            if (move.cutting()) {
                if (!first_cut_point) {
                    // Keep the XY of the first actual feed move, while pinning
                    // its label to the stock surface where it stays visible.
                    first_cut_point = project(move.x, move.y, z_top);
                    first_cut_xy = QPointF(move.x, move.y);
                }
                if (previous_cutting) {
                    cut_path.lineTo(point);
                    const double segment = QLineF(previous_point, point).length();
                    distance_since_arrow += segment;
                    if (segment > 2.0 && distance_since_arrow >= 58.0) {
                        direction_marks.push_back({(previous_point + point) / 2.0,
                                                   point - previous_point,
                                                   operation_colors[op_index % operation_colors.size()]});
                        distance_since_arrow = 0.0;
                    }
                } else {
                    cut_path.moveTo(point);
                    distance_since_arrow = 0.0;
                }
                deepest_cut = deepest_cut ? std::max(*deepest_cut, z_top - move.z)
                                          : z_top - move.z;
            }
            previous_point = point;
            have_previous = true;
            previous_cutting = move.cutting();
        }

        // The OS illustrates only material removed by the cutter. G0 travel
        // moves remain available in the real simulation and G-code, but do not
        // clutter the production drawing with dashed lines between cut shapes.
        painter.setPen(QPen(QColor(37, 27, 19, 190), kerf, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(cut_path);
        QColor glow = operation_colors[op_index % operation_colors.size()];
        glow.setAlpha(90);
        painter.setPen(QPen(glow, std::max(2.0, kerf * .72), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(cut_path);
        painter.setPen(QPen(operation_colors[op_index % operation_colors.size()], 1.45,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(cut_path);
    }

    for (const DirectionMark& mark : direction_marks) {
        const double length = std::max(1.0, QLineF(QPointF(), mark.direction).length());
        const QPointF unit = mark.direction / length;
        const QPointF normal(-unit.y(), unit.x());
        painter.setPen(QPen(mark.color, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(mark.tip, mark.tip - unit * 7.0 + normal * 3.4);
        painter.drawLine(mark.tip, mark.tip - unit * 7.0 - normal * 3.4);
    }

    if (first_cut_point) {
        painter.setBrush(QColor("#35dc91"));
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawEllipse(*first_cut_point, 4, 4);

        // A compact metallic cutter points at the real first plunge of the path.
        const QRectF cutter_body(first_cut_point->x() - 5, first_cut_point->y() - 35, 10, 25);
        QLinearGradient steel(cutter_body.topLeft(), cutter_body.topRight());
        steel.setColorAt(0.0, QColor("#657585"));
        steel.setColorAt(.35, QColor("#eff5fa"));
        steel.setColorAt(.7, QColor("#8999a8"));
        steel.setColorAt(1.0, QColor("#4a5966"));
        painter.setBrush(steel);
        painter.setPen(QPen(QColor("#dce5ed"), .7));
        painter.drawRoundedRect(cutter_body, 2, 2);
        painter.setPen(QPen(QColor("#344451"), 1.0));
        painter.drawLine(cutter_body.left() + 3, cutter_body.top() + 8,
                         cutter_body.right() - 1, cutter_body.bottom() - 1);
        painter.drawLine(cutter_body.right() - 3, cutter_body.top() + 8,
                         cutter_body.left() + 1, cutter_body.bottom() - 1);
        painter.setPen(QPen(QColor("#b9c6d1"), .7));
        painter.drawLine(cutter_body.left(), cutter_body.top() + 5,
                         cutter_body.right(), cutter_body.top() + 5);
    }

    // Origin tripod follows the configured work zero, including bottom Z.
    const QPointF origin = project(origin_x, origin_y, origin_z);
    const auto arrow = [&](QPointF from, QPointF to, QColor color, const QString& label) {
        const QPointF delta = to - from;
        const double length = std::max(1.0, QLineF(from, to).length());
        const QPointF unit = delta / length;
        const QPointF normal(-unit.y(), unit.x());
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(from, to);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF{to, to - unit * 5.5 + normal * 2.5,
                                          to - unit * 5.5 - normal * 2.5});
        painter.setPen(color);
        painter.setFont(QFont("Segoe UI", 8, QFont::Bold));
        painter.drawText(to + QPointF(2, -2), label);
    };
    arrow(origin, origin + QPointF(x_axis_x, x_axis_y) * 22.0, QColor("#f05b5b"), "X");
    arrow(origin, origin + QPointF(y_axis_x, y_axis_y) * 22.0, QColor("#4dd78a"), "Y");
    arrow(origin, origin + QPointF(0, -22), QColor("#63a7ff"), "Z");
    painter.setBrush(QColor("#ffae55"));
    painter.setPen(QPen(Qt::white, .8));
    painter.drawEllipse(origin, 3.3, 3.3);

    // Keep both production references readable on the OS screen and PDF. The
    // boxes stay inside the drawing area and avoid each other when P0 and the
    // first cut happen at the same XY.
    QPainterPath stock_silhouette;
    stock_silhouette.setFillRule(Qt::WindingFill);
    stock_silhouette.addPolygon(top_polygon);
    stock_silhouette.addPolygon(front_face);
    stock_silhouette.addPolygon(right_face);
    std::vector<QRectF> placed_callouts;
    const auto draw_callout = [&](const QPointF& point, const QString& title,
                                  const QString& detail, const QColor& color) {
        const double box_w = std::min(214.0, drawing_area.width() - 8.0);
        constexpr double box_h = 46.0;
        const QRectF limits = drawing_area.adjusted(4.0, 4.0, -4.0, -4.0);
        // Reserve two adjacent cards in the dark strip above the sheet. This
        // is predictable for the operator and keeps labels off the cut lines.
        QRectF box(limits.left() + placed_callouts.size() * (box_w + 10.0),
                   limits.top(), box_w, box_h);
        bool preferred_clear = box.right() <= limits.right() &&
                               !stock_silhouette.intersects(box.adjusted(-3, -3, 3, 3)) &&
                               !box.contains(point);
        for (const QRectF& placed : placed_callouts)
            preferred_clear = preferred_clear && !box.intersects(placed);
        if (!preferred_clear) {
            // For unusually shaped or offset sheets, use an unobstructed edge.
            const std::array<QPointF, 8> positions = {
                QPointF(limits.left(), limits.top()),
                QPointF(limits.right() - box_w, limits.top()),
                QPointF(limits.left(), limits.bottom() - box_h),
                QPointF(limits.right() - box_w, limits.bottom() - box_h),
                QPointF(limits.center().x() - box_w / 2.0, limits.top()),
                QPointF(limits.center().x() - box_w / 2.0, limits.bottom() - box_h),
                QPointF(limits.left(), limits.center().y() - box_h / 2.0),
                QPointF(limits.right() - box_w, limits.center().y() - box_h / 2.0)};
            double best = std::numeric_limits<double>::max();
            for (const QPointF& position : positions) {
                const QRectF candidate(position, QSizeF(box_w, box_h));
                double score = QLineF(point, candidate.center()).length() * .1;
                if (candidate.contains(point)) score += 10000.0;
                if (stock_silhouette.intersects(candidate)) {
                    score += 10000.0;
                    for (int y = 0; y < 5; ++y)
                        for (int x = 0; x < 14; ++x) {
                            const QPointF sample(candidate.left() + (x + .5) * box_w / 14.0,
                                                 candidate.top() + (y + .5) * box_h / 5.0);
                            if (stock_silhouette.contains(sample)) score += 180.0;
                        }
                }
                for (const QRectF& placed : placed_callouts)
                    score += candidate.intersected(placed).width() *
                             candidate.intersected(placed).height() * 30.0;
                if (score < best) { best = score; box = candidate; }
            }
        }
        placed_callouts.push_back(box);
        const QPointF edge(std::clamp(point.x(), box.left(), box.right()),
                           std::clamp(point.y(), box.top(), box.bottom()));
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(point, edge);
        painter.setBrush(QColor(7, 18, 30, 238));
        painter.setPen(QPen(color, 1.25));
        painter.drawRoundedRect(box, 5, 5);
        painter.setPen(color);
        painter.setFont(QFont("Segoe UI", 10, QFont::Bold));
        painter.drawText(box.adjusted(9, 5, -7, -23), Qt::AlignLeft | Qt::AlignVCenter,
                         title);
        painter.setPen(QColor("#f4f8fc"));
        painter.setFont(QFont("Segoe UI", 8.8, QFont::DemiBold));
        painter.drawText(box.adjusted(9, 23, -7, -4), Qt::AlignLeft | Qt::AlignVCenter,
                         detail);
    };

    const bool zero_is_cut_start = first_cut_point &&
        QLineF(origin, *first_cut_point).length() < 14.0;
    painter.setBrush(QColor(7, 18, 30, 235));
    painter.setPen(QPen(QColor("#ffbd69"), 2.0));
    painter.drawEllipse(origin, zero_is_cut_start ? 12.0 : 7.0,
                        zero_is_cut_start ? 12.0 : 7.0);
    painter.setBrush(QColor("#ffbd69"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(origin, 2.5, 2.5);
    draw_callout(origin, "PONTO 0 · ORIGEM",
                 QString("X %1 · Y %2 mm · Z %3")
                     .arg(origin_x, 0, 'f', 1).arg(origin_y, 0, 'f', 1)
                     .arg(has_material && material->zero_z == "bottom" ? "base" : "topo"),
                 QColor("#ffbd69"));
    if (first_cut_point && first_cut_xy) {
        painter.setBrush(QColor(8, 30, 24, 235));
        painter.setPen(QPen(QColor("#47ec9c"), 2.0));
        painter.drawEllipse(*first_cut_point, zero_is_cut_start ? 6.0 : 8.0,
                            zero_is_cut_start ? 6.0 : 8.0);
        painter.setBrush(QColor("#47ec9c"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(*first_cut_point, 3.0, 3.0);
        draw_callout(*first_cut_point, "INÍCIO DO CORTE",
                     QString("Na chapa: X %1 · Y %2 mm")
                         .arg(first_cut_xy->x(), 0, 'f', 1)
                         .arg(first_cut_xy->y(), 0, 'f', 1), QColor("#47ec9c"));
    }

    // The cotas run as a strip under the stock: spreading them across the width
    // leaves room for type the operator can read from the machine.
    painter.setBrush(QColor(9, 20, 31, 205));
    painter.setPen(QPen(QColor("#334b61"), .9));
    painter.drawRoundedRect(legend, 6, 6);

    const std::array<std::pair<QString, QString>, 5> cotas = {{
        {"CHAPA X × Y × Z", QString("%1 × %2 × %3 mm").arg(board_width, 0, 'f', 1)
                                                            .arg(board_height, 0, 'f', 1)
                                                            .arg(thickness, 0, 'f', 1)},
        {"ENVELOPE X", QString("%1 mm").arg(cut_width, 0, 'f', 1)},
        {"ENVELOPE Y", QString("%1 mm").arg(cut_height, 0, 'f', 1)},
        {"PROFUNDIDADE MÁX.", QString("%1 mm").arg(deepest_cut.value_or(0.0), 0, 'f', 2)},
        {"ÁREA DA CHAPA", QString("%1 m²").arg(area_m2, 0, 'f', 3)},
    }};
    // The stock dimensions need roughly twice the room of a single measurement.
    const std::array<double, 5> cell_weights = {2.1, 1.0, 1.0, 1.25, 1.1};
    const double weight_total = std::accumulate(cell_weights.begin(), cell_weights.end(), 0.0);
    const double strip_width = legend.width() - 24;
    double cell_x = legend.left() + 12;
    for (size_t i = 0; i < cotas.size(); ++i) {
        const double cell_width = strip_width * cell_weights[i] / weight_total;
        const double x = cell_x;
        cell_x += cell_width;
        if (i > 0) {
            painter.setPen(QPen(QColor(70, 96, 122, 150), .8));
            painter.drawLine(QPointF(x - 6, legend.top() + 10),
                             QPointF(x - 6, legend.bottom() - 10));
        }
        painter.setPen(QColor("#8ea4ba"));
        painter.setFont(QFont("Segoe UI", 7.5, QFont::Bold));
        painter.drawText(QRectF(x, legend.top() + 8, cell_width - 10, 16),
                         Qt::AlignLeft | Qt::AlignVCenter, cotas[i].first);
        painter.setPen(QColor("#f4f8fc"));
        painter.setFont(QFont("Segoe UI", 11, QFont::Bold));
        painter.drawText(QRectF(x, legend.top() + 26, cell_width - 10, 26),
                         Qt::AlignLeft | Qt::AlignVCenter, cotas[i].second);
    }

    painter.setFont(QFont("Segoe UI", 7.6));
    painter.setPen(QColor("#29d9ef"));
    painter.drawText(QRectF(16, canvas_h - 17, canvas_w - 32, 13),
                     Qt::AlignLeft | Qt::AlignVCenter, "Colorido: centro da fresa");
    painter.setPen(QColor("#9bacbc"));
    painter.drawText(QRectF(16, canvas_h - 17, canvas_w - 32, 13),
                     Qt::AlignRight | Qt::AlignVCenter,
                     "Vista isométrica · percurso de corte e material removido.");
    return image;
}

std::vector<QImage> make_operation_previews(const std::vector<cam::SetupSheetOperation>& operations,
                                            const cad::MaterialSetup* material) {
    std::vector<QImage> previews;
    previews.reserve(operations.size());
    for (size_t i = 0; i < operations.size(); ++i) {
        if (!operations[i].path) continue;
        JobPreviewOptions options;
        // This sheet is almost all drawing, so the stock may grow much taller
        // than it does on the summary page before the frame caps it.
        options.min_height = 330;
        options.max_height = 760;
        options.only_operation = static_cast<int>(i);
        previews.push_back(make_job_layout_preview(operations, material, options));
    }
    return previews;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), tool_context_(document_, undo_stack_) {
    setWindowTitle("CNC-CAM");
    // The stylesheet is installed on QApplication in main.cpp.  A local
    // stylesheet here used to leave top-level dialogs and helper widgets on
    // the native Windows style.
    setMinimumSize(760, 480);
    const QSize available = screen() ? screen()->availableGeometry().size() : QSize(1280, 800);
    resize(std::min(1280, available.width() - 32), std::min(800, available.height() - 64));
    // Same initial stock as MainWindow.novo_documento() in the PySide6 app.
    // Keep MaterialSetup's core default small for non-GUI tests; the GUI opens
    // on the shop's PVC sheet, 1220 x 2440 mm.
    document_.material.width = 1220.0;
    document_.material.height = 2440.0;

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* view_header = new QWidget(central);
    auto* view_header_layout = new QHBoxLayout(view_header);
    view_header_layout->setContentsMargins(0, 0, 4, 0);
    view_header_layout->setSpacing(4);

    view_tabs_ = new QTabBar(view_header);
    view_tabs_->setObjectName("ViewTabs");
    view_tabs_->addTab("Vista 2D");
    view_tabs_->addTab("Vista 3D");
    view_tabs_->addTab("OS · Setup");
    view_header_layout->addWidget(view_tabs_, 1);

    auto* view_3d_controls = new QWidget(view_header);
    view_3d_controls_ = view_3d_controls;
    view_3d_controls->setObjectName("View3DControls");
    auto* view_3d_layout = new QHBoxLayout(view_3d_controls);
    view_3d_layout->setContentsMargins(4, 3, 4, 3);
    view_3d_layout->setSpacing(2);
    const auto set_standard_3d_view = [this](const QString& name, const QString& label) {
        if (!viewport_3d_) return;
        viewport_3d_->set_standard_view(name);
        if (projection_3d_button_)
            projection_3d_button_->setText(viewport_3d_->camera.use_ortho ? "ORTO" : "PERSP");
        statusBar()->showMessage(QString("Vista 3D %1").arg(label));
    };
    const std::vector<std::tuple<QString, QString, std::function<void()>>> view_3d_buttons = {
        {"AJ", "Ajustar material a tela", [this] {
             if (viewport_3d_) viewport_3d_->fit_scene();
             statusBar()->showMessage("Material ajustado a vista 3D");
         }},
        {"ISO", "Vista isometrica", [set_standard_3d_view] {
             set_standard_3d_view("iso", "isometrica");
         }},
        {"Z+", "Vista superior", [set_standard_3d_view] {
             set_standard_3d_view("top", "superior");
         }},
        {"Z-", "Vista inferior", [set_standard_3d_view] {
             set_standard_3d_view("bottom", "inferior");
         }},
        {"Y-", "Vista frontal", [set_standard_3d_view] {
             set_standard_3d_view("front", "frontal");
         }},
        {"Y+", "Vista traseira", [set_standard_3d_view] {
             set_standard_3d_view("back", "traseira");
         }},
        {"X+", "Vista lateral direita", [set_standard_3d_view] {
             set_standard_3d_view("right", "lateral direita");
         }},
        {"X-", "Vista lateral esquerda", [set_standard_3d_view] {
             set_standard_3d_view("left", "lateral esquerda");
         }},
    };
    for (const auto& [label, tip, action] : view_3d_buttons) {
        auto* button = new QToolButton(view_3d_controls);
        button->setObjectName("View3DBtn");
        button->setText(label);
        button->setToolTip(tip);
        button->setFixedHeight(28);
        view_3d_layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [action] { action(); });
    }
    projection_3d_button_ = new QToolButton(view_3d_controls);
    projection_3d_button_->setObjectName("View3DBtn");
    projection_3d_button_->setText("PERSP");
    projection_3d_button_->setToolTip("Alternar projecao em perspectiva e ortografica");
    projection_3d_button_->setFixedHeight(28);
    view_3d_layout->addWidget(projection_3d_button_);
    connect(projection_3d_button_, &QToolButton::clicked, this, [this] {
        if (!viewport_3d_) return;
        const bool ortho = viewport_3d_->toggle_projection();
        projection_3d_button_->setText(ortho ? "ORTO" : "PERSP");
        statusBar()->showMessage(ortho ? "Projecao ortografica" : "Projecao em perspectiva");
    });
    view_3d_controls->setVisible(false);
    view_header_layout->addWidget(view_3d_controls);

    bridge_mode_banner_ = new QWidget(view_header);
    bridge_mode_banner_->setObjectName("BridgeModeBanner");
    auto* banner_layout = new QHBoxLayout(bridge_mode_banner_);
    banner_layout->setContentsMargins(10, 2, 8, 2);
    banner_layout->setSpacing(8);
    auto* banner_icon = new QLabel(bridge_mode_banner_);
    banner_icon->setPixmap(ui::get_icon("tp_edit").pixmap(18, 18));
    banner_layout->addWidget(banner_icon);
    auto* banner_text = new QLabel(
        "Modo de pontes ativo — Clique nos contornos para adicionar ou remover · Arraste para mover",
        bridge_mode_banner_);
    banner_text->setStyleSheet("font-weight: bold; color: #7ec7ff; font-size: 12px;");
    banner_layout->addWidget(banner_text, 1);
    auto* finish_button = new QPushButton("Concluir pontes (Esc)", bridge_mode_banner_);
    finish_button->setCursor(Qt::PointingHandCursor);
    finish_button->setStyleSheet(
        "QPushButton { background-color: #1976d2; color: #ffffff; font-weight: bold; padding: 4px 12px; "
        "border-radius: 4px; border: 1px solid #42a5f5; font-size: 11px; } "
        "QPushButton:hover { background-color: #1e88e5; border-color: #64b5f6; }");
    banner_layout->addWidget(finish_button);
    connect(finish_button, &QPushButton::clicked, this, &MainWindow::stop_manual_bridge_mode);
    bridge_mode_banner_->setVisible(false);
    bridge_mode_banner_->setStyleSheet(
        "QWidget#BridgeModeBanner { background-color: #0e2238; border: 1px solid #1976d2; border-radius: 5px; margin: 1px 4px; }");
    view_header_layout->addWidget(bridge_mode_banner_);

    layout->addWidget(view_header);

    canvas_widget_ = new vp::Canvas2DWidget(document_.material.width, document_.material.height,
                                             central);
    canvas_widget_->canvas->set_document(&document_);
    view_stack_ = new QStackedWidget(central);
    view_stack_->setObjectName("MainViewStack");
    view_stack_->addWidget(canvas_widget_);

    viewport_3d_ = new vp::Viewport3D(central);
    view_stack_->addWidget(viewport_3d_);
    create_order_service_page();
    layout->addWidget(view_stack_, 1);
    setCentralWidget(central);
    setDockNestingEnabled(false);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    coordinate_label_ = new QLabel("X: —   Y: —", this);
    statusBar()->addPermanentWidget(coordinate_label_);

    tool_context_.canvas = canvas_widget_->canvas;
    tool_context_.status_fn = [this](const std::string& message) {
        statusBar()->showMessage(QString::fromUtf8(message.c_str()));
    };
    tool_context_.prompt_fn = [this](const std::string& message) {
        statusBar()->showMessage(QString::fromUtf8(message.c_str()));
    };
    tool_context_.finished_fn = [this] { activate_tool("select"); };

    create_tool_panel();
    create_toolpath_panel();
    const auto docks = findChildren<QDockWidget*>();
    QDockWidget* left_dock = nullptr;
    QDockWidget* right_dock = nullptr;
    for (QDockWidget* dock : docks) {
        if (dock->objectName() == "ToolPanel") left_dock = dock;
        if (dock->objectName() == "ToolpathPanel") right_dock = dock;
    }
    if (left_dock && right_dock)
        resizeDocks({left_dock, right_dock}, {224, 44}, Qt::Horizontal);
    activate_tool("select");

    connect(canvas_widget_, &vp::Canvas2DWidget::mouse_moved, this,
            [this](double x, double y) {
                coordinate_label_->setText(QString("X: %1   Y: %2 mm")
                                               .arg(x, 0, 'f', 2)
                                               .arg(y, 0, 'f', 2));
            });
    connect(view_tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index != 0 && canvas_widget_ && canvas_widget_->canvas && canvas_widget_->canvas->manual_tab_mode) {
            stop_manual_bridge_mode();
        }
        if (view_stack_) view_stack_->setCurrentIndex(index);
        if (view_3d_controls_) view_3d_controls_->setVisible(index == 1);
        if (index == 1 && viewport_3d_) {
            QTimer::singleShot(120, this, [this] { refresh_3d_view(); });
        }
        if (order_service_nav_button_)
            order_service_nav_button_->setChecked(index == 2);
        if (index == 2) {
            set_right_panel_collapsed(false);
            show_right_page(1);
            refresh_order_service_page();
        }
    });
    connect(canvas_widget_, &vp::Canvas2DWidget::files_dropped, this,
            [this](const QStringList& paths, double, double) { import_files(paths); });
    selection_preview_timer_ = new QTimer(this);
    selection_preview_timer_->setSingleShot(true);
    selection_preview_timer_->setInterval(32);
    connect(selection_preview_timer_, &QTimer::timeout, this, [this] {
        refresh_guide_previews();
        refresh_automatic_cut_panel();
    });
    connect(canvas_widget_->canvas, &vp::Canvas2D::selection_changed, this,
            &MainWindow::schedule_selection_previews);
    connect(canvas_widget_->canvas, &vp::Canvas2D::manual_tab_clicked, this,
            &MainWindow::add_manual_bridge);
    connect(canvas_widget_->canvas, &vp::Canvas2D::tab_marker_moved, this,
            &MainWindow::move_bridge_marker);
    connect(canvas_widget_->canvas, &vp::Canvas2D::cancel_requested, this,
            &MainWindow::stop_manual_bridge_mode);
    connect(canvas_widget_, &vp::Canvas2DWidget::entity_double_clicked,
            this, &MainWindow::quick_edit_entity);
    connect(canvas_widget_, &vp::Canvas2DWidget::context_menu_requested,
            this, &MainWindow::show_canvas_context_menu);

    create_actions();
    refresh_document_view();
    statusBar()->showMessage(QFile::exists(":/assets/check.svg")
                                 ? "0 objeto(s) selecionado(s)"
                                 : "recursos FALTANDO");
}

void MainWindow::create_order_service_page() {
    auto* page = new QWidget(view_stack_);
    page->setObjectName("OrderServicePage");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    auto* heading = new QHBoxLayout();
    auto* heading_text = new QVBoxLayout();
    heading_text->setSpacing(2);
    auto* title = new QLabel("CENTRAL OS · SETUP EM 5 ETAPAS", page);
    title->setStyleSheet("font-size: 15pt; font-weight: 700; color: #8cc8ff;");
    auto* subtitle = new QLabel(
        "Confira material, origem e ferramentas; o desenho acompanha os percursos calculados.", page);
    subtitle->setObjectName("MutedText");
    heading_text->addWidget(title);
    heading_text->addWidget(subtitle);
    heading->addLayout(heading_text, 1);
    order_service_state_label_ = new QLabel(page);
    order_service_state_label_->setObjectName("MutedText");
    order_service_state_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    heading->addWidget(order_service_state_label_);
    root->addLayout(heading);

    order_service_step_tabs_ = new QTabBar(page);
    order_service_step_tabs_->setObjectName("OSStepTabs");
    order_service_step_tabs_->setExpanding(true);
    order_service_step_tabs_->setDrawBase(false);
    order_service_step_tabs_->setStyleSheet(R"QSS(
        QTabBar::tab { background:#1a2939; color:#a9bfd6; border:1px solid #344a62;
            padding:8px 7px; margin-right:3px; font-weight:600; }
        QTabBar::tab:selected { background:#1d4567; color:#f0f7ff; border-color:#168be0; }
        QTabBar::tab:hover:!selected { background:#24384d; color:#ffffff; }
    )QSS");
    for (const QString& step : {"1 · Material", "2 · Origem", "3 · Ferramentas",
                                "4 · Percurso", "5 · Identificação / PDF"})
        order_service_step_tabs_->addTab(step);
    root->addWidget(order_service_step_tabs_);

    auto* body = new QHBoxLayout();
    body->setSpacing(10);
    order_service_steps_ = new QStackedWidget(page);
    order_service_steps_->setObjectName("OSStepStack");
    order_service_steps_->setMinimumWidth(250);
    body->addWidget(order_service_steps_, 4);

    auto* material_page = new QWidget(order_service_steps_);
    auto* material_layout = new QVBoxLayout(material_page);
    material_layout->setContentsMargins(12, 10, 12, 10);
    material_layout->setSpacing(10);
    auto* material_title = new QLabel("1 · Material da chapa", material_page);
    material_title->setObjectName("SectionHeader");
    material_layout->addWidget(material_title);
    auto* material_form = new QFormLayout();
    material_form->setSpacing(10);
    order_service_material_label_ = new QLabel(material_page);
    order_service_material_dims_label_ = new QLabel(material_page);
    order_service_material_area_label_ = new QLabel(material_page);
    order_service_margin_label_ = new QLabel(material_page);
    for (auto* value : {order_service_material_label_, order_service_material_dims_label_,
                        order_service_material_area_label_, order_service_margin_label_}) {
        value->setObjectName("OSValue");
        value->setWordWrap(true);
    }
    material_form->addRow("Material", order_service_material_label_);
    material_form->addRow("Dimensões X × Y × Z", order_service_material_dims_label_);
    material_form->addRow("Área", order_service_material_area_label_);
    material_form->addRow("Margem de segurança", order_service_margin_label_);
    material_layout->addLayout(material_form);
    auto* material_hint = new QLabel(
        "Use a mesma espessura cadastrada para a chapa carregada na máquina.", material_page);
    material_hint->setObjectName("MutedText");
    material_hint->setWordWrap(true);
    material_layout->addWidget(material_hint);
    auto* edit_material = new QPushButton("Editar material e dimensões…", material_page);
    material_layout->addWidget(edit_material);
    connect(edit_material, &QPushButton::clicked, this, &MainWindow::edit_material_setup);
    material_layout->addStretch();
    order_service_steps_->addWidget(material_page);

    auto* origin_page = new QWidget(order_service_steps_);
    auto* origin_layout = new QVBoxLayout(origin_page);
    origin_layout->setContentsMargins(12, 10, 12, 10);
    origin_layout->setSpacing(10);
    auto* origin_title = new QLabel("2 · Origem e segurança", origin_page);
    origin_title->setObjectName("SectionHeader");
    origin_layout->addWidget(origin_title);
    auto* origin_form = new QFormLayout();
    origin_form->setSpacing(10);
    order_service_zero_xy_label_ = new QLabel(origin_page);
    order_service_zero_z_label_ = new QLabel(origin_page);
    order_service_clearance_label_ = new QLabel(origin_page);
    order_service_plunge_gap_label_ = new QLabel(origin_page);
    order_service_offset_label_ = new QLabel(origin_page);
    for (auto* value : {order_service_zero_xy_label_, order_service_zero_z_label_,
                        order_service_clearance_label_, order_service_plunge_gap_label_,
                        order_service_offset_label_}) {
        value->setObjectName("OSValue");
        value->setWordWrap(true);
    }
    origin_form->addRow("Zero XY", order_service_zero_xy_label_);
    origin_form->addRow("Zero Z", order_service_zero_z_label_);
    origin_form->addRow("Altura segura Z", order_service_clearance_label_);
    origin_form->addRow("Folga de mergulho", order_service_plunge_gap_label_);
    origin_form->addRow("Deslocamento adicional XY", order_service_offset_label_);
    origin_layout->addLayout(origin_form);
    auto* origin_hint = new QLabel(
        "Confirme o zero físico e a altura segura antes de enviar o programa CNC.", origin_page);
    origin_hint->setObjectName("MutedText");
    origin_hint->setWordWrap(true);
    origin_layout->addWidget(origin_hint);
    auto* edit_origin = new QPushButton("Mudar Ponto 0 e segurança…", origin_page);
    origin_layout->addWidget(edit_origin);
    connect(edit_origin, &QPushButton::clicked, this, &MainWindow::edit_material_setup);
    origin_layout->addStretch();
    order_service_steps_->addWidget(origin_page);

    auto* tools_page = new QWidget(order_service_steps_);
    auto* tools_layout = new QVBoxLayout(tools_page);
    tools_layout->setContentsMargins(8, 8, 8, 8);
    tools_layout->setSpacing(8);
    auto* tools_title = new QLabel("3 · Ferramentas e operações calculadas", tools_page);
    tools_title->setObjectName("SectionHeader");
    tools_layout->addWidget(tools_title);
    order_service_operations_table_ = new QTableWidget(tools_page);
    order_service_operations_table_->setObjectName("OSOperationsTable");
    order_service_operations_table_->setColumnCount(5);
    order_service_operations_table_->setHorizontalHeaderLabels(
        {"Operação", "Ferramenta", "RPM", "Avanço", "Tempo"});
    order_service_operations_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    order_service_operations_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    order_service_operations_table_->verticalHeader()->hide();
    order_service_operations_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    order_service_operations_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    order_service_operations_table_->setAlternatingRowColors(true);
    tools_layout->addWidget(order_service_operations_table_, 1);
    auto* open_toolpaths = new QPushButton("Abrir parâmetros dos percursos…", tools_page);
    tools_layout->addWidget(open_toolpaths);
    connect(open_toolpaths, &QPushButton::clicked, this, [this] { show_right_page(1); });
    order_service_steps_->addWidget(tools_page);

    auto* route_page = new QWidget(order_service_steps_);
    auto* route_layout = new QVBoxLayout(route_page);
    route_layout->setContentsMargins(12, 10, 12, 10);
    route_layout->setSpacing(9);
    auto* route_title = new QLabel("4 · Conferência do percurso", route_page);
    route_title->setObjectName("SectionHeader");
    route_layout->addWidget(route_title);
    auto* route_text = new QLabel(
        "A imagem ao lado é renderizada dos movimentos calculados. Verifique o envelope, o sentido, "
        "a profundidade e as cotas antes de gerar a folha.", route_page);
    route_text->setObjectName("MutedText");
    route_text->setWordWrap(true);
    route_layout->addWidget(route_text);
    auto* verify_simulation = new QPushButton("Abrir simulação do percurso", route_page);
    route_layout->addWidget(verify_simulation);
    connect(verify_simulation, &QPushButton::clicked, this, [this] {
        show_right_page(2);
        if (view_tabs_) view_tabs_->setCurrentIndex(1);
    });
    route_layout->addStretch();
    order_service_steps_->addWidget(route_page);

    auto* details_page = new QWidget(order_service_steps_);
    auto* details_layout = new QVBoxLayout(details_page);
    details_layout->setContentsMargins(12, 8, 12, 8);
    details_layout->setSpacing(6);
    auto* details_title = new QLabel("5 · Identificação da ordem", details_page);
    details_title->setObjectName("SectionHeader");
    details_layout->addWidget(details_title);
    auto* details_form = new QFormLayout();
    details_form->setSpacing(6);
    order_service_part_ = new QLineEdit(details_page);
    order_service_number_ = new QLineEdit(details_page);
    order_service_customer_ = new QLineEdit(details_page);
    order_service_contact_ = new QLineEdit(details_page);
    order_service_revision_ = new QLineEdit(details_page);
    order_service_operator_ = new QLineEdit(details_page);
    order_service_notes_ = new QPlainTextEdit(details_page);
    order_service_notes_->setMaximumHeight(74);
    details_form->addRow("Peça / projeto", order_service_part_);
    details_form->addRow("Nº da OS / pedido", order_service_number_);
    details_form->addRow("Cliente", order_service_customer_);
    details_form->addRow("Contato", order_service_contact_);
    details_form->addRow("Revisão", order_service_revision_);
    details_form->addRow("Operador", order_service_operator_);
    details_form->addRow("Observações", order_service_notes_);
    details_layout->addLayout(details_form);
    order_service_steps_->addWidget(details_page);

    connect(order_service_step_tabs_, &QTabBar::currentChanged,
            order_service_steps_, &QStackedWidget::setCurrentIndex);
    connect(order_service_steps_, &QStackedWidget::currentChanged,
            order_service_step_tabs_, &QTabBar::setCurrentIndex);

    auto* preview_panel = new QFrame(page);
    preview_panel->setObjectName("OSPreviewPanel");
    preview_panel->setStyleSheet(
        "QFrame#OSPreviewPanel { background: #14202d; border: 1px solid #304861; border-radius: 8px; }");
    auto* preview_layout = new QVBoxLayout(preview_panel);
    preview_layout->setContentsMargins(10, 9, 10, 9);
    preview_layout->setSpacing(7);
    auto* preview_heading = new QHBoxLayout();
    preview_heading->setContentsMargins(0, 0, 0, 0);
    auto* preview_title = new QLabel("DESENHO 3D · PERCURSO REAL", preview_panel);
    preview_title->setObjectName("SectionHeader");
    preview_heading->addWidget(preview_title, 1);
    auto* change_origin = new QPushButton("Alterar Ponto 0", preview_panel);
    change_origin->setToolTip("Editar a origem XY/Z. A prévia da OS acompanha a origem configurada no desenho 2D.");
    connect(change_origin, &QPushButton::clicked, this, &MainWindow::edit_material_setup);
    preview_heading->addWidget(change_origin);
    preview_layout->addLayout(preview_heading);
    auto* image_frame = new QFrame(preview_panel);
    image_frame->setStyleSheet("background: #0b1521; border: 1px solid #344a62; border-radius: 6px;");
    auto* image_layout = new QVBoxLayout(image_frame);
    image_layout->setContentsMargins(4, 4, 4, 4);
    order_service_preview_label_ = new AspectPixmapLabel(image_frame);
    order_service_preview_label_->setMinimumHeight(260);
    image_layout->addWidget(order_service_preview_label_);
    // The drawing takes every pixel the panel can spare: it is what the
    // operator reads before letting the machine cut.
    preview_layout->addWidget(image_frame, 1);
    auto* dimension_card = new QFrame(preview_panel);
    dimension_card->setObjectName("MaterialCard");
    auto* dimension_layout = new QVBoxLayout(dimension_card);
    dimension_layout->setContentsMargins(10, 7, 10, 7);
    dimension_layout->setSpacing(2);
    order_service_dimensions_label_ = new QLabel(dimension_card);
    order_service_dimensions_label_->setObjectName("MaterialInfo");
    order_service_origin_label_ = new QLabel(dimension_card);
    order_service_origin_label_->setObjectName("MutedText");
    dimension_layout->addWidget(order_service_dimensions_label_);
    dimension_layout->addWidget(order_service_origin_label_);
    preview_layout->addWidget(dimension_card);

    auto* metrics = new QGridLayout();
    metrics->setSpacing(6);
    auto make_metric = [preview_panel](const QString& title_text, const QColor& accent) {
        auto* card = new QFrame(preview_panel);
        card->setStyleSheet(QString("background:#202f41; border:1px solid #344a62; border-top:3px solid %1; border-radius:6px;")
                                .arg(accent.name()));
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(8, 5, 8, 5);
        card_layout->setSpacing(1);
        auto* label = new QLabel(title_text.toUpper(), card);
        label->setStyleSheet("color:#9db0c4; font-size:8pt; font-weight:700; border:none; background:transparent;");
        auto* value = new QLabel("—", card);
        value->setStyleSheet("color:#f0f7ff; font-size:12pt; font-weight:700; border:none; background:transparent;");
        card_layout->addWidget(label);
        card_layout->addWidget(value);
        return std::pair{card, value};
    };
    const auto [time_card, time_value] = make_metric("Tempo", QColor("#f5a623"));
    const auto [cut_card, cut_value] = make_metric("Corte", QColor("#9a6bff"));
    const auto [depth_card, depth_value] = make_metric("Z máx.", QColor("#31c48d"));
    order_service_time_label_ = time_value;
    order_service_cut_label_ = cut_value;
    order_service_depth_label_ = depth_value;
    metrics->addWidget(time_card, 0, 0);
    metrics->addWidget(cut_card, 0, 1);
    metrics->addWidget(depth_card, 0, 2);
    preview_layout->addLayout(metrics);
    body->addWidget(preview_panel, 7);
    root->addLayout(body, 1);

    auto* footer = new QHBoxLayout();
    auto* step_hint = new QLabel("Confira cotas e fixação antes de usinar.", page);
    step_hint->setObjectName("MutedText");
    footer->addWidget(step_hint, 1);
    auto* previous = new QPushButton("Etapa anterior", page);
    auto* next = new QPushButton("Próxima etapa", page);
    auto* save_pdf = new QPushButton("GERAR PDF DA OS", page);
    save_pdf->setProperty("primary", true);
    save_pdf->setIcon(ui::get_icon("tp_sheet"));
    save_pdf->setMinimumHeight(40);
    connect(previous, &QPushButton::clicked, this, [this] {
        if (order_service_step_tabs_)
            order_service_step_tabs_->setCurrentIndex(std::max(0, order_service_step_tabs_->currentIndex() - 1));
    });
    connect(next, &QPushButton::clicked, this, [this] {
        if (order_service_step_tabs_)
            order_service_step_tabs_->setCurrentIndex(std::min(4, order_service_step_tabs_->currentIndex() + 1));
    });
    connect(save_pdf, &QPushButton::clicked, this, &MainWindow::generate_order_service_pdf);
    footer->addWidget(previous);
    footer->addWidget(next);
    footer->addWidget(save_pdf);
    root->addLayout(footer);

    order_service_page_ = page;
    view_stack_->addWidget(page);
    connect(order_service_part_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/partName", text);
    });
    connect(order_service_number_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/orderNumber", text);
    });
    connect(order_service_customer_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/customer", text);
    });
    connect(order_service_contact_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/contact", text);
    });
    connect(order_service_revision_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/revision", text);
    });
    connect(order_service_operator_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue("jobSheet/operator", text);
    });
    connect(order_service_notes_, &QPlainTextEdit::textChanged, this, [this] {
        QSettings().setValue("jobSheet/notes", order_service_notes_->toPlainText());
    });

    QSettings settings;
    order_service_customer_->setText(settings.value("jobSheet/customer").toString());
    order_service_contact_->setText(settings.value("jobSheet/contact").toString());
    order_service_number_->setText(settings.value("jobSheet/orderNumber").toString());
    order_service_part_->setText(settings.value("jobSheet/partName").toString());
    order_service_revision_->setText(settings.value("jobSheet/revision").toString());
    order_service_operator_->setText(settings.value("jobSheet/operator").toString());
    order_service_notes_->setPlainText(settings.value("jobSheet/notes").toString());
}

void MainWindow::open_order_service_page() {
    if (!view_tabs_) return;
    set_right_panel_collapsed(false);
    show_right_page(1);
    if (order_service_nav_button_) order_service_nav_button_->setChecked(true);
    const bool already_open = view_tabs_->currentIndex() == 2;
    view_tabs_->setCurrentIndex(2);
    if (already_open) refresh_order_service_page();
}

bool MainWindow::recalculate_order_service_if_geometry_changed() {
    std::vector<std::pair<std::string, uint64_t>> current_versions;
    current_versions.reserve(document_.entities.size());
    for (const auto& entity : document_.entities) {
        if (entity) current_versions.emplace_back(entity->id, entity->version);
    }
    if (order_service_geometry_snapshot_valid_ &&
        current_versions == order_service_geometry_versions_)
        return false;

    const std::vector<std::string> errors = document_.recalculate_toolpaths();
    order_service_geometry_versions_ = std::move(current_versions);
    order_service_geometry_snapshot_valid_ = true;
    if (!errors.empty())
        statusBar()->showMessage(QString("OS atualizada; %1 percurso(s) precisam de revisÃ£o.")
                                     .arg(errors.size()));
    return true;
}

void MainWindow::refresh_order_service_page() {
    if (!order_service_page_ && view_stack_ && view_stack_->count() >= 3)
        order_service_page_ = view_stack_->widget(2);
    if (!order_service_preview_label_ || !view_stack_ ||
        view_stack_->currentWidget() != order_service_page_) return;
    // Rebuild calculated cuts only after the 2D geometry changes. The OS view
    // then follows the current drawing while profile guide illustrations stay
    // on their original interactive MachiningGuide renderer.
    if (recalculate_order_service_if_geometry_changed()) {
        refresh_toolpaths();
        return;
    }

    std::vector<cam::SetupSheetOperation> operations;
    double seconds = 0.0;
    double cut_length = 0.0;
    double deepest_z = 0.0;
    int operation_count = 0;
    for (const auto& job : document_.toolpaths) {
        if (!job || !job->visible) continue;
        for (const auto& path : job->results) {
            if (!path.visible) continue;
            operations.push_back({&path, job.get()});
            ++operation_count;
            seconds += path.estimated_seconds();
            cut_length += path.lengths().first;
            deepest_z = std::min(deepest_z, path.depth_range().first);
        }
    }
    const auto& material = document_.material;
    const auto origin = material.origin_xy();
    const auto axis_label = [](const std::string& value, bool x_axis) {
        if (value == "center") return QString("Centro");
        if (x_axis) return value == "right" ? QString("Direita") : QString("Esquerda");
        return value == "top" ? QString("Superior") : QString("Inferior");
    };
    if (order_service_material_label_)
        order_service_material_label_->setText(QString::fromStdString(
            material.material_profile.empty() ? "Não informado" : material.material_profile));
    if (order_service_material_dims_label_)
        order_service_material_dims_label_->setText(
            QString("%1 × %2 × %3 mm").arg(material.width, 0, 'g', 8)
                .arg(material.height, 0, 'g', 8).arg(material.thickness, 0, 'g', 8));
    if (order_service_material_area_label_)
        order_service_material_area_label_->setText(
            QString::number(material.width * material.height / 1000000.0, 'f', 3) + " m²");
    if (order_service_margin_label_)
        order_service_margin_label_->setText(QString::number(material.margin, 'g', 8) + " mm");
    if (order_service_zero_xy_label_)
        order_service_zero_xy_label_->setText(QString("%1 · %2 · X%3 / Y%4 mm")
            .arg(axis_label(material.zero_x, true), axis_label(material.zero_y, false))
            .arg(origin.x, 0, 'g', 8).arg(origin.y, 0, 'g', 8));
    if (order_service_zero_z_label_)
        order_service_zero_z_label_->setText(material.zero_z == "top" ? "Face superior" : "Face inferior");
    if (order_service_clearance_label_)
        order_service_clearance_label_->setText(QString::number(material.clearance_z, 'g', 8) + " mm");
    if (order_service_plunge_gap_label_)
        order_service_plunge_gap_label_->setText(QString::number(material.plunge_gap, 'g', 8) + " mm");
    if (order_service_offset_label_)
        order_service_offset_label_->setText(material.use_origin_offset
            ? QString("X %1 · Y %2 mm").arg(material.origin_offset_x, 0, 'g', 8)
                .arg(material.origin_offset_y, 0, 'g', 8)
            : "Desativado");
    if (order_service_state_label_) {
        order_service_state_label_->setText(operation_count > 0
            ? QString("%1 operação(ões) calculada(s)").arg(operation_count)
            : "Aguardando percursos calculados");
    }
    if (order_service_dimensions_label_)
        order_service_dimensions_label_->setText(
            QString("%1  ·  %2 × %3 × %4 mm  ·  %5 m²")
                .arg(QString::fromStdString(material.material_profile.empty() ? "Material" : material.material_profile))
                .arg(material.width, 0, 'g', 8).arg(material.height, 0, 'g', 8)
                .arg(material.thickness, 0, 'g', 8)
                .arg(material.width * material.height / 1000000.0, 0, 'f', 3));
    if (order_service_origin_label_)
        order_service_origin_label_->setText(
            QString("Origem X %1 · Y %2 · Z na face %3 · Seguro %4 mm")
                .arg(origin.x, 0, 'g', 8).arg(origin.y, 0, 'g', 8)
                .arg(material.zero_z == "top" ? "superior" : "inferior")
                .arg(material.clearance_z, 0, 'g', 8));
    if (order_service_time_label_)
        order_service_time_label_->setText(QString("%1 min %2 s")
            .arg(static_cast<int>(seconds / 60.0))
            .arg(static_cast<int>(seconds) % 60, 2, 10, QChar('0')));
    if (order_service_cut_label_)
        order_service_cut_label_->setText(QString::number(cut_length / 1000.0, 'f', 2) + " m");
    if (order_service_depth_label_)
        order_service_depth_label_->setText(operation_count > 0
            ? QString::number(std::abs(deepest_z), 'f', 2) + " mm"
            : "—");

    const QImage preview = make_job_layout_preview(operations, &material);
    static_cast<AspectPixmapLabel*>(order_service_preview_label_)->set_source_image(preview);

    if (order_service_operations_table_) {
        order_service_operations_table_->setRowCount(operation_count);
        for (int row = 0; row < operation_count; ++row) {
            const auto& operation = operations[static_cast<size_t>(row)];
            const auto* path = operation.path;
            const cam::Tool* tool = operation.job ? &operation.job->tool : nullptr;
            const auto time_seconds = static_cast<int>(path->estimated_seconds());
            const std::array<QString, 5> cells = {
                QString::fromStdString(path->name),
                QString("T%1 · %2 · Ø %3 mm").arg(path->tool_number)
                    .arg(QString::fromStdString(path->tool_name))
                    .arg(tool ? tool->diameter : 0.0, 0, 'f', 2),
                QString::number(path->spindle_speed),
                QString::number(path->feed_rate, 'f', 0) + " mm/min",
                QString("%1:%2").arg(time_seconds / 60).arg(time_seconds % 60, 2, 10, QChar('0'))};
            for (int col = 0; col < static_cast<int>(cells.size()); ++col)
                order_service_operations_table_->setItem(row, col, new QTableWidgetItem(cells[static_cast<size_t>(col)]));
        }
        order_service_operations_table_->resizeRowsToContents();
    }

    if (order_service_part_ && order_service_part_->text().isEmpty())
        order_service_part_->setText(QString::fromStdString(document_.title()));
}

void MainWindow::generate_order_service_pdf() {
    if (recalculate_order_service_if_geometry_changed()) refresh_toolpaths();
    std::vector<cam::SetupSheetOperation> operations;
    for (const auto& job : document_.toolpaths) {
        if (!job || !job->visible) continue;
        for (const auto& path : job->results)
            if (path.visible) operations.push_back({&path, job.get()});
    }
    if (operations.empty()) {
        QMessageBox::information(this, "Sem percurso calculado",
                                 "Calcule ao menos um percurso antes de gerar a Ordem de Serviço.");
        return;
    }

    cam::SetupSheetInfo info;
    info.customer = order_service_customer_->text().toStdString();
    info.contact = order_service_contact_->text().toStdString();
    info.order_number = order_service_number_->text().toStdString();
    info.part_name = order_service_part_->text().toStdString();
    info.revision = order_service_revision_->text().toStdString();
    info.operator_name = order_service_operator_->text().toStdString();
    info.notes = order_service_notes_->toPlainText().toStdString();
    info.material_type = document_.material.material_profile;
    info.area_m2 = std::format("{:.3f}", document_.material.width * document_.material.height / 1000000.0);

    QSettings settings;
    QImage logo;
    const QString logo_path = settings.value("jobSheet/logoPath").toString();
    if (!logo_path.isEmpty()) {
        QImageReader reader(logo_path);
        logo = reader.read();
        if (!logo.isNull()) info.logo_source = "job-logo";
    }
    const QString base = order_service_part_->text().trimmed().isEmpty()
        ? "Ordem_de_Servico" : order_service_part_->text().trimmed();
    QString path = QFileDialog::getSaveFileName(this, "Salvar Ordem de Serviço em PDF",
                                                 base + "_OS.pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".pdf";

    const QImage preview = make_job_layout_preview(operations, &document_.material);
    QString error;
    if (!cam::write_job_setup_sheet_pdf(path, operations, document_.material,
                                        document_.title(), info, preview, logo,
                                        {}, &error)) {
        QMessageBox::critical(this, "Erro ao gerar PDF", error);
        return;
    }
    statusBar()->showMessage(QString("Ordem de Serviço salva: %1").arg(path));
    QMessageBox::information(this, "PDF da OS gerado",
                             QString("A folha de produção foi salva em:\n%1").arg(path));
}

void MainWindow::show_3d_view() {
    if (view_tabs_) view_tabs_->setCurrentIndex(1);
}

void MainWindow::create_toolpath_panel() {
    auto* dock = new QDockWidget("Percursos", this);
    toolpath_dock_ = dock;
    dock->setObjectName("ToolpathPanel");
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(44);

    auto* shell = new QWidget(dock);
    auto* shell_layout = new QHBoxLayout(shell);
    shell_layout->setContentsMargins(0, 0, 0, 0);
    shell_layout->setSpacing(0);

    auto* rail = new QWidget(shell);
    rail->setObjectName("RightToolRail");
    rail->setFixedWidth(44);
    auto* rail_layout = new QVBoxLayout(rail);
    rail_layout->setContentsMargins(2, 6, 2, 8);
    rail_layout->setSpacing(5);

    // 3 mini abas recolhidas no canto direito (padrão Illustrator)
    // Aba 1: Seleção / Propriedades
    auto* selection_button = new QToolButton(rail);
    selection_button->setObjectName("RightRailButton");
    selection_button->setCheckable(true);
    selection_button->setChecked(true);
    selection_button->setFixedSize(38, 34);
    selection_button->setIcon(ui::get_icon("rail_selection"));
    selection_button->setIconSize(QSize(24, 24));
    selection_button->setToolTip("Seleção e Propriedades");
    rail_layout->addWidget(selection_button);
    selection_nav_button_ = selection_button;

    // Aba 2: Percursos CAM
    auto* toolpath_button = new QToolButton(rail);
    toolpath_button->setObjectName("RightRailButton");
    toolpath_button->setCheckable(true);
    toolpath_button->setFixedSize(38, 34);
    toolpath_button->setIcon(ui::get_icon("rail_toolpaths"));
    toolpath_button->setIconSize(QSize(24, 24));
    toolpath_button->setToolTip("Percursos CAM");
    rail_layout->addWidget(toolpath_button);
    toolpath_nav_button_ = toolpath_button;

    auto* automatic_button = new QToolButton(rail);
    automatic_button->setObjectName("RightRailButton");
    automatic_button->setCheckable(true);
    automatic_button->setFixedSize(38, 34);
    automatic_button->setIcon(ui::get_icon("rail_automatic"));
    automatic_button->setIconSize(QSize(24, 24));
    automatic_button->setToolTip("Programação automática por material");
    rail_layout->addWidget(automatic_button);
    automatic_nav_button_ = automatic_button;

    // Aba 3: Simulação
    auto* simulation_button = new QToolButton(rail);
    simulation_button->setObjectName("RightRailButton");
    simulation_button->setCheckable(true);
    simulation_button->setFixedSize(38, 34);
    simulation_button->setIcon(ui::get_icon("rail_simulation"));
    simulation_button->setIconSize(QSize(24, 24));
    simulation_button->setToolTip(QString::fromUtf8("Simulação do percurso"));
    rail_layout->addWidget(simulation_button);
    simulation_nav_button_ = simulation_button;

    auto* order_button = new QToolButton(rail);
    order_button->setObjectName("RightRailButton");
    order_button->setCheckable(true);
    order_button->setFixedSize(38, 34);
    order_button->setIcon(ui::get_icon("rail_order"));
    order_button->setIconSize(QSize(24, 24));
    order_button->setToolTip("Abrir a Central da Ordem de Serviço e gerar o PDF");
    rail_layout->addWidget(order_button);
    order_service_nav_button_ = order_button;
    connect(order_button, &QToolButton::clicked, this, &MainWindow::open_order_service_page);

    rail_layout->addStretch();

    auto* stack = new QStackedWidget(shell);
    right_panel_stack_ = stack;
    auto* selection_page = new QWidget(stack);
    selection_page->setObjectName("PropPanel");
    auto* selection_layout = new QVBoxLayout(selection_page);
    selection_layout->setContentsMargins(0, 0, 0, 0);
    selection_layout->setSpacing(0);
    auto* selection_title = new QLabel(QString::fromUtf8("Sele\xC3\xA7\xC3\xA3o"), selection_page);
    selection_title->setObjectName("PropTitle");
    selection_layout->addWidget(selection_title);
    auto* selection_hint = new QLabel(
        "Arraste por um ponto branco para encostar na peca ao lado. Ctrl: solta copia - Alt: trava no eixo - Shift: soma/remove e desloca o ima",
        selection_page);
    selection_hint->setObjectName("PropHint");
    selection_hint->setWordWrap(true);
    selection_layout->addWidget(selection_hint);
    auto* preview = new ui::MachiningGuide("08_material", selection_page);
    preview->setObjectName("SelectionGuide");
    preview->setProperty("readOnlyPreview", true);
    preview->setMinimumWidth(0);
    preview->setMaximumHeight(230);
    preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    selection_layout->addWidget(preview);
    auto* gallery = new QPushButton("Desenhos interativos…", selection_page);
    gallery->setObjectName("OpenGuideGallery");
    gallery->setToolTip("Abrir os dez desenhos com parâmetros e animação");
    connect(gallery, &QPushButton::clicked, this, [this] { ui::showGuideGallery(this); });
    selection_layout->addWidget(gallery);
    selection_layout->addStretch();
    stack->addWidget(selection_page);

    auto* toolpath_page = new QWidget(stack);
    auto* layout = new QVBoxLayout(toolpath_page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    auto* material_head = new QHBoxLayout();
    material_head->setSpacing(10);
    auto* material_info = new QVBoxLayout();
    material_info->setSpacing(5);
    auto* material_button = new QPushButton("Definir...", toolpath_page);
    material_button->setObjectName("MaterialBtn");
    material_button->setIcon(ui::get_icon("workspace_setup"));
    material_button->setIconSize(QSize(20, 20));
    material_button->setToolTip("Definições do Trabalho (Ctrl+J)");
    material_info->addWidget(material_button, 0, Qt::AlignLeft);
    toolpath_size_label_ = new QLabel(toolpath_page);
    toolpath_size_label_->setObjectName("MutedText");
    toolpath_origin_label_ = new QLabel(toolpath_page);
    toolpath_origin_label_->setObjectName("MutedText");
    for (auto* label : {toolpath_size_label_, toolpath_origin_label_}) {
        label->setStyleSheet("color: #96a9c1; font-size: 11px;");
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        material_info->addWidget(label);
    }
    material_head->addLayout(material_info, 1);
    auto* material_thumb = new ui::MachiningGuide("08_material", toolpath_page);
    material_thumb->setObjectName("MaterialGuideThumbnail");
    material_thumb->setProperty("compact", true);
    material_thumb->setProperty("readOnlyPreview", true);
    material_thumb->setFixedSize(86, 68);
    material_thumb->setToolTip("Material do trabalho — alterar em Definir…");
    material_head->addWidget(material_thumb, 0, Qt::AlignTop);
    layout->addLayout(material_head);
    connect(material_button, &QPushButton::clicked, this, &MainWindow::edit_material_setup);

    auto* material_sep = new QFrame(toolpath_page);
    material_sep->setObjectName("ToolSep");
    material_sep->setFixedHeight(1);
    material_sep->setFrameShape(QFrame::NoFrame);
    layout->addWidget(material_sep);

    auto* operations_title = new QLabel("PERCURSOS DE USINAGEM", toolpath_page);
    operations_title->setObjectName("SectionHeader");
    layout->addWidget(operations_title);

    auto* operations_grid = new ui::FlowLayout(6);
    const std::vector<std::tuple<QString, QString, cam::ToolpathType>> operations = {
        {"tp_profile", "Perfil", cam::ToolpathType::Profile},
        {"tp_pocket", "Bolso", cam::ToolpathType::Pocket},
        {"tp_drill", QString::fromUtf8("Fura\xC3\xA7\xC3\xA3o"), cam::ToolpathType::Drill},
        {"tp_vcarve", "Entalhe V", cam::ToolpathType::VCarve},
        {"tp_fluting", "Canais", cam::ToolpathType::Fluting},
        {"tp_texture", "Textura", cam::ToolpathType::Texture},
        {"tp_keyhole", "Keyhole", cam::ToolpathType::Keyhole},
        {"tp_inlay", "Inlay", cam::ToolpathType::Inlay},
        {"tp_dragknife", "Faca", cam::ToolpathType::Dragknife},
    };
    for (int index = 0; index < static_cast<int>(operations.size()); ++index) {
        const auto& [icon_name, label, kind] = operations[static_cast<size_t>(index)];
        auto* button = new QToolButton(toolpath_page);
        button->setObjectName("ToolBtn");
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(ui::get_icon(icon_name));
        button->setIconSize(QSize(34, 34));
        button->setText(label);
        button->setMinimumSize(78, 58);
        button->setToolTip(QString("Criar percurso de %1").arg(label));
        connect(button, &QToolButton::clicked, this, [this, kind] {
            toolpath_panel_kind_ = kind;
            add_toolpath_from_panel();
        });
        operations_grid->addWidget(button);
    }
    layout->addLayout(operations_grid);

    auto* list_sep = new QFrame(toolpath_page);
    list_sep->setObjectName("ToolSep");
    list_sep->setFixedHeight(1);
    list_sep->setFrameShape(QFrame::NoFrame);
    layout->addWidget(list_sep);

    auto* list_title = new QLabel("PERCURSOS NO PROJETO", toolpath_page);
    list_title->setObjectName("SectionHeader");
    layout->addWidget(list_title);

    auto* list_stack = new QStackedWidget(toolpath_page);
    toolpath_list_stack_ = list_stack;
    toolpath_list_ = new QListWidget(list_stack);
    toolpath_list_->setObjectName("ToolpathList");
    toolpath_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_stack->addWidget(toolpath_list_);
    toolpath_empty_label_ = new QLabel(
        QString::fromUtf8("Nenhum percurso no projeto\n\nSelecione vetores e escolha uma opera\xC3\xA7\xC3\xA3o acima."),
        list_stack);
    toolpath_empty_label_->setObjectName("ToolpathEmpty");
    toolpath_empty_label_->setAlignment(Qt::AlignCenter);
    toolpath_empty_label_->setWordWrap(true);
    list_stack->addWidget(toolpath_empty_label_);
    layout->addWidget(list_stack, 1);

    auto add_icon_button = [this, toolpath_page](const QString& icon_name, const QString& tip,
                                                  const std::function<void()>& action) {
        auto* button = new QToolButton(toolpath_page);
        button->setObjectName("ToolBtn");
        button->setIcon(ui::get_icon(icon_name));
        button->setIconSize(QSize(26, 26));
        button->setFixedSize(36, 36);
        button->setToolTip(tip);
        connect(button, &QToolButton::clicked, this, [action] { action(); });
        return button;
    };
    auto* manage_grid = new QGridLayout();
    manage_grid->setSpacing(6);
    const std::vector<std::tuple<QString, QString, std::function<void()>>> manage_actions = {
        {"tp_tools", "Base de dados de ferramentas", [this] { edit_tool_database(); }},
        {"tp_edit", "Editar o percurso selecionado", [this] { edit_current_toolpath(); }},
        {"tp_duplicate", "Duplicar o percurso selecionado", [this] { duplicate_current_toolpath(); }},
        {"tp_delete", "Apagar o percurso selecionado", [this] { delete_current_toolpath(); }},
        {"tp_recalc", "Recalcular todos os percursos", [this] { recalculate_toolpaths(); }},
    };
    for (int index = 0; index < static_cast<int>(manage_actions.size()); ++index) {
        const auto& [icon, tip, action] = manage_actions[static_cast<size_t>(index)];
        manage_grid->addWidget(add_icon_button(icon, tip, action), 0, index,
                               Qt::AlignHCenter);
    }
    layout->addLayout(manage_grid);

    auto* output_grid = new QGridLayout();
    output_grid->setSpacing(6);
    const std::vector<std::tuple<QString, QString, std::function<void()>>> output_actions = {
        {"tp_template", "Corte automático por material", [this] {
            show_right_page(3);
            refresh_automatic_cut_panel();
        }},
        {"tp_preview", "Mostrar/ocultar percursos", [this] { toggle_current_toolpath(); }},
        {"tp_time", "Estimativa de tempo de usinagem", [this] {
            if (!toolpath_list_ || toolpath_list_->currentRow() < 0) return;
            const int row = toolpath_list_->currentRow();
            if (row >= static_cast<int>(document_.toolpaths.size())) return;
            const auto& job = document_.toolpaths[static_cast<size_t>(row)];
            double seconds = 0.0;
            for (const auto& path : job->results) seconds += path.estimated_seconds();
            QMessageBox::information(this, "Tempo de usinagem",
                                     seconds > 0.0
                                         ? QString("%1: aproximadamente %2 min")
                                               .arg(QString::fromStdString(job->name))
                                               .arg(seconds / 60.0, 0, 'f', 1)
                                         : "Recalcule o percurso para estimar o tempo.");
        }},
        {"tp_sheet", "Central da Ordem de Serviço", [this] { open_order_service_page(); }},
    };
    for (int index = 0; index < static_cast<int>(output_actions.size()); ++index) {
        const auto& [icon, tip, action] = output_actions[static_cast<size_t>(index)];
        output_grid->addWidget(add_icon_button(icon, tip, action), 0, index,
                               Qt::AlignHCenter);
    }
    layout->addLayout(output_grid);

    auto* order = new QHBoxLayout();
    order->setSpacing(6);
    order->addWidget(add_icon_button("tp_up", "Subir na ordem de corte", [this] {
        move_current_toolpath(-1);
    }));
    order->addWidget(add_icon_button("tp_down", "Descer na ordem de corte", [this] {
        move_current_toolpath(1);
    }));
    order->addStretch();
    layout->addLayout(order);

    auto* gcode_separator = new QFrame(toolpath_page);
    gcode_separator->setFrameShape(QFrame::HLine);
    gcode_separator->setObjectName("PanelSeparator");
    layout->addWidget(gcode_separator);

    auto* save_gcode_button = new QPushButton("Salvar G-Code", toolpath_page);
    save_gcode_button->setProperty("primary", true);
    save_gcode_button->setIcon(ui::get_icon("tp_gcode"));
    save_gcode_button->setIconSize(QSize(22, 22));
    save_gcode_button->setMinimumHeight(42);
    save_gcode_button->setToolTip("Salvar o G-Code dos percursos calculados e visíveis");
    layout->addWidget(save_gcode_button);
    connect(save_gcode_button, &QPushButton::clicked, this, &MainWindow::export_gcode);

    auto* work_order_button = new QPushButton("Ordem de Serviço (PDF)", toolpath_page);
    work_order_button->setIcon(ui::get_icon("tp_sheet"));
    work_order_button->setIconSize(QSize(20, 20));
    work_order_button->setMinimumHeight(38);
    work_order_button->setToolTip("Visualizar detalhes da usinagem, medidas, material e baixar Ordem de Serviço em PDF");
    layout->addWidget(work_order_button);
    connect(work_order_button, &QPushButton::clicked, this, &MainWindow::open_order_service_page);

    connect(toolpath_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            statusBar()->showMessage(QString::fromStdString(document_.toolpaths[static_cast<size_t>(row)]->summary()));
        refresh_bridge_markers();
    });
    stack->addWidget(toolpath_page);

    auto* simulation_page = new QWidget(stack);
    simulation_page->setObjectName("PropPanel");
    auto* simulation_layout = new QVBoxLayout(simulation_page);
    simulation_layout->setContentsMargins(14, 12, 14, 12);
    simulation_layout->setSpacing(9);

    auto* simulation_title = new QLabel("SIMULAÇÃO DO PERCURSO", simulation_page);
    simulation_title->setObjectName("SectionHeader");
    simulation_layout->addWidget(simulation_title);

    simulation_operation_label_ = new QLabel("Calcule um percurso para simular.", simulation_page);
    simulation_operation_label_->setObjectName("MutedText");
    simulation_operation_label_->setWordWrap(true);
    simulation_layout->addWidget(simulation_operation_label_);

    auto make_playback_button = [simulation_page](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, simulation_page);
        button->setMinimumHeight(34);
        button->setFixedWidth(36);
        button->setToolTip(tip);
        button->setEnabled(false);
        return button;
    };
    auto* playback = new QHBoxLayout();
    playback->setSpacing(6);
    simulation_play_button_ = make_playback_button("▶", "Reproduzir");
    simulation_pause_button_ = make_playback_button("Ⅱ", "Pausar");
    simulation_step_button_ = make_playback_button("▶|", "Avançar um movimento");
    simulation_finish_button_ = make_playback_button("▶▶", "Ir ao final");
    simulation_restart_button_ = make_playback_button("↻", "Reiniciar");
    for (auto* button : {simulation_play_button_, simulation_pause_button_, simulation_step_button_,
                         simulation_finish_button_, simulation_restart_button_}) {
        playback->addWidget(button, 1);
    }
    simulation_layout->addLayout(playback);

    auto* speed_row = new QHBoxLayout();
    speed_row->setSpacing(8);
    speed_row->addWidget(new QLabel("Velocidade", simulation_page));
    simulation_speed_slider_ = new QSlider(Qt::Horizontal, simulation_page);
    simulation_speed_slider_->setRange(1, 100);
    simulation_speed_slider_->setValue(10);
    simulation_speed_slider_->setToolTip("0,1× a 10× a velocidade estimada");
    speed_row->addWidget(simulation_speed_slider_, 1);
    simulation_speed_label_ = new QLabel("1.0×", simulation_page);
    speed_row->addWidget(simulation_speed_label_);
    simulation_layout->addLayout(speed_row);
    connect(simulation_speed_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (simulation_speed_label_)
            simulation_speed_label_->setText(QString("%1×").arg(value / 10.0, 0, 'f', 1));
    });

    auto add_simulation_scope = [this, simulation_page, simulation_layout](const QString& text,
                                                                            const QString& scope) {
        auto* button = new QPushButton(text, simulation_page);
        button->setMinimumHeight(36);
        simulation_layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, scope] { begin_simulation(scope); });
    };
    add_simulation_scope("Simular percurso selecionado", "selected");
    add_simulation_scope("Simular todos os percursos", "all");
    add_simulation_scope("Simular percursos visíveis", "visible");

    simulation_info_label_ = new QLabel("Percurso: 00:00 / 00:00", simulation_page);
    simulation_info_label_->setObjectName("MutedText");
    simulation_info_label_->setWordWrap(true);
    simulation_layout->addWidget(simulation_info_label_);

    simulation_progress_ = new QProgressBar(simulation_page);
    simulation_progress_->setObjectName("SimulationProgress");
    simulation_progress_->setRange(0, 1000);
    simulation_progress_->setValue(0);
    simulation_progress_->setTextVisible(true);
    simulation_progress_->setFormat("%p% concluído");
    simulation_layout->addWidget(simulation_progress_);

    auto* simulation_note = new QLabel(
        "Estimativa pelos avanços programados e rápido de 8000 mm/min. "
        "Não inclui aceleração, desaceleração nem troca de ferramenta.", simulation_page);
    simulation_note->setObjectName("MutedText");
    simulation_note->setWordWrap(true);
    simulation_layout->addWidget(simulation_note);
    simulation_layout->addStretch();
    stack->addWidget(simulation_page);

    connect(simulation_play_button_, &QPushButton::clicked, this, &MainWindow::play_simulation);
    connect(simulation_pause_button_, &QPushButton::clicked, this, &MainWindow::pause_simulation);
    connect(simulation_step_button_, &QPushButton::clicked, this, &MainWindow::step_simulation);
    connect(simulation_finish_button_, &QPushButton::clicked, this, &MainWindow::finish_simulation);
    connect(simulation_restart_button_, &QPushButton::clicked, this, &MainWindow::restart_simulation);
    simulation_timer_ = new QTimer(this);
    simulation_timer_->setInterval(16);
    connect(simulation_timer_, &QTimer::timeout, this, &MainWindow::update_simulation);

    auto* automatic_page = new QWidget(stack);
    automatic_page->setObjectName("PropPanel");
    auto* automatic_layout = new QVBoxLayout(automatic_page);
    automatic_layout->setContentsMargins(14, 12, 14, 12);
    automatic_layout->setSpacing(10);

    auto* automatic_title = new QLabel("CORTE AUTOMÁTICO POR MATERIAL", automatic_page);
    automatic_title->setObjectName("SectionHeader");
    automatic_layout->addWidget(automatic_title);
    auto* automatic_intro = new QLabel(
        "Escolha um dos 9 parâmetros de produção já cadastrados no Python. "
        "O botão cria e calcula um perfil externo usando os vetores selecionados.", automatic_page);
    automatic_intro->setObjectName("MutedText");
    automatic_intro->setWordWrap(true);
    automatic_intro->setText(QString::fromUtf8(
        "Escolha o preset do material. Sem sele\xC3\xA7\xC3\xA3o, usa todos os contornos edit\xC3\xA1veis vis\xC3\xADveis; textos e linhas de constru\xC3\xA7\xC3\xA3o ficam fora."));
    automatic_layout->addWidget(automatic_intro);

    automatic_cut_combo_ = new QComboBox(automatic_page);
    automatic_cut_combo_->addItem("— escolher —", -1);
    const auto& presets = cam::house_presets();
    for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
        automatic_cut_combo_->addItem(QString::fromStdString(presets[static_cast<size_t>(index)].name()), index);
    }
    automatic_layout->addWidget(automatic_cut_combo_);

    automatic_cut_info_label_ = new QLabel(automatic_page);
    automatic_cut_info_label_->setObjectName("MutedText");
    automatic_cut_info_label_->setWordWrap(true);
    automatic_layout->addWidget(automatic_cut_info_label_);

    auto* automatic_create = new QPushButton("Criar corte automático", automatic_page);
    automatic_create->setToolTip("Criar corte automático com os vetores selecionados");
    automatic_create_button_ = automatic_create;
    automatic_create->setProperty("primary", true);
    automatic_create->setMinimumHeight(42);
    automatic_layout->addWidget(automatic_create);
    connect(automatic_create, &QPushButton::clicked, this, &MainWindow::create_automatic_cut);
    connect(automatic_cut_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                automatic_cut_panel_dirty_ = true;
                refresh_automatic_cut_panel();
            });

    auto* automatic_note = new QLabel(
        "Segurança: a espessura escolhida deve ser a mesma da chapa do trabalho. "
        "Materiais sem cadastro não recebem parâmetros inventados.", automatic_page);
    automatic_note->setObjectName("MutedText");
    automatic_note->setWordWrap(true);
    automatic_layout->addWidget(automatic_note);
    automatic_layout->addStretch();
    stack->addWidget(automatic_page);

    auto* panel_scroll = new QScrollArea(shell);
    right_panel_scroll_ = panel_scroll;
    panel_scroll->setObjectName("RightPanelScroll");
    panel_scroll->setWidgetResizable(true);
    panel_scroll->setFrameShape(QFrame::NoFrame);
    stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    stack->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    stack->setMinimumWidth(0);
    for (auto* combo : stack->findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }
    panel_scroll->setWidget(stack);
    shell_layout->addWidget(panel_scroll, 1);
    shell_layout->addWidget(rail, 0);
    dock->setWidget(shell);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    auto toggle_right_tab = [this](int page_idx) {
        if (!right_panel_scroll_ || !right_panel_stack_) return;
        const bool is_open = right_panel_scroll_->isVisible();
        const bool is_current = (right_panel_stack_->currentIndex() == page_idx);
        if (is_open && is_current) {
            // Comportamento do Illustrator: clicar na aba ativa recolhe o painel de volta para o canto
            set_right_panel_collapsed(true);
        } else {
            // Abre ou troca para a aba selecionada no canto
            show_right_page(page_idx);
        }
    };

    connect(selection_button, &QToolButton::clicked, this, [toggle_right_tab] { toggle_right_tab(0); });
    connect(toolpath_button, &QToolButton::clicked, this, [toggle_right_tab] { toggle_right_tab(1); });
    connect(automatic_button, &QToolButton::clicked, this, [toggle_right_tab] { toggle_right_tab(3); });
    connect(simulation_button, &QToolButton::clicked, this, [toggle_right_tab] { toggle_right_tab(2); });

    connect(stack, &QStackedWidget::currentChanged, this, [this](int page) {
        if (page >= 0) set_right_panel_collapsed(false);
    });

    stack->setCurrentIndex(1);
    // Inicia recolhido no canto com 3 mini abas visíveis (estilo Illustrator)
    set_right_panel_collapsed(true);

    refresh_automatic_cut_panel();
    refresh_toolpaths();
}

void MainWindow::show_guides_gallery() {
    ui::showGuideGallery(this);
}

void MainWindow::create_legacy_toolpath_panel() {
    auto* dock = new QDockWidget("Percursos CAM", this);
    dock->setObjectName("ToolpathPanel");
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(240);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    auto* title = new QLabel("PERCURSOS", panel);
    title->setObjectName("SectionHeader");
    layout->addWidget(title);

    auto* material_button = new QPushButton("Definições do material...", panel);
    layout->addWidget(material_button);
    connect(material_button, &QPushButton::clicked, this, &MainWindow::edit_material_setup);

    toolpath_list_ = new QListWidget(panel);
    toolpath_list_->setObjectName("ToolpathList");
    toolpath_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(toolpath_list_, 1);

    auto* type_label = new QLabel("Novo percurso", panel);
    type_label->setObjectName("PropLabel");
    layout->addWidget(type_label);
    toolpath_type_combo_ = new QComboBox(panel);
    const std::vector<std::pair<QString, cam::ToolpathType>> kinds = {
        {"Perfil", cam::ToolpathType::Profile}, {"Bolso", cam::ToolpathType::Pocket},
        {"Furação", cam::ToolpathType::Drill}, {"V-carve", cam::ToolpathType::VCarve},
        {"Fluting", cam::ToolpathType::Fluting}, {"Textura", cam::ToolpathType::Texture},
        {"Keyhole", cam::ToolpathType::Keyhole}, {"Inlay", cam::ToolpathType::Inlay},
        {"Drag knife", cam::ToolpathType::Dragknife},
    };
    for (const auto& [label, kind] : kinds) toolpath_type_combo_->addItem(label, static_cast<int>(kind));
    layout->addWidget(toolpath_type_combo_);

    auto* add_button = new QPushButton("Adicionar percurso", panel);
    add_button->setProperty("primary", true);
    layout->addWidget(add_button);
    connect(add_button, &QPushButton::clicked, this, &MainWindow::add_toolpath_from_panel);

    auto* recalc_button = new QPushButton("Recalcular todos", panel);
    layout->addWidget(recalc_button);
    connect(recalc_button, &QPushButton::clicked, this, &MainWindow::recalculate_toolpaths);

    auto* row = new QHBoxLayout();
    auto* edit_button = new QPushButton("Editar", panel);
    auto* toggle_button = new QPushButton("Mostrar/ocultar", panel);
    auto* delete_button = new QPushButton("Excluir", panel);
    row->addWidget(edit_button);
    row->addWidget(toggle_button);
    row->addWidget(delete_button);
    layout->addLayout(row);
    connect(edit_button, &QPushButton::clicked, this, &MainWindow::edit_current_toolpath);
    connect(toggle_button, &QPushButton::clicked, this, &MainWindow::toggle_current_toolpath);
    connect(delete_button, &QPushButton::clicked, this, &MainWindow::delete_current_toolpath);

    auto* simulate_button = new QPushButton("Simular percursos", panel);
    simulate_button->setObjectName("SimulatePathBtn");
    layout->addWidget(simulate_button);
    connect(simulate_button, &QPushButton::clicked, this, &MainWindow::toggle_simulation);

    simulation_timer_ = new QTimer(this);
    simulation_timer_->setInterval(30);
    connect(simulation_timer_, &QTimer::timeout, this, &MainWindow::update_simulation);
    connect(toolpath_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            statusBar()->showMessage(QString::fromStdString(document_.toolpaths[static_cast<size_t>(row)]->summary()));
        refresh_bridge_markers();
    });

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    refresh_toolpaths();
}

void MainWindow::refresh_toolpaths() {
    if (!toolpath_list_) return;
    if (simulation_timer_ && simulation_timer_->isActive()) simulation_timer_->stop();
    simulation_.reset();
    refresh_simulation_status();
    const int old_row = toolpath_list_->currentRow();
    QSignalBlocker blocker(toolpath_list_);
    toolpath_list_->clear();
    for (const auto& job : document_.toolpaths) {
        auto* item = new QListWidgetItem(QString::fromStdString(job->name), toolpath_list_);
        item->setToolTip(QString::fromStdString(job->summary()));
        item->setCheckState(job->visible ? Qt::Checked : Qt::Unchecked);
        if (!job->error.empty()) item->setForeground(QColor("#ff8a80"));
    }
    if (!document_.toolpaths.empty())
        toolpath_list_->setCurrentRow(std::clamp(old_row, 0, static_cast<int>(document_.toolpaths.size()) - 1));
    if (toolpath_list_stack_)
        toolpath_list_stack_->setCurrentIndex(document_.toolpaths.empty() ? 1 : 0);
    if (toolpath_size_label_) {
        toolpath_size_label_->setText(
            QString::fromUtf8("%1 \xC3\x97 %2 \xC3\x97 %3 mm")
                .arg(document_.material.width, 0, 'g', 10)
                .arg(document_.material.height, 0, 'g', 10)
                .arg(document_.material.thickness, 0, 'g', 10));
    }
    if (toolpath_origin_label_) {
        const auto origin = document_.material.origin_xy();
        const QString zero = document_.material.zero_z == "top" ? "topo" : "base";
        toolpath_origin_label_->setText(
            QString::fromUtf8("Origem X %1 \xC2\xB7 Y %2 \xC2\xB7 Z no %3\nSeguran\xC3\xA7" "a Z %4 mm")
                .arg(origin.x, 0, 'g', 10)
                .arg(origin.y, 0, 'g', 10)
                .arg(zero)
                .arg(document_.material.clearance_z, 0, 'g', 10));
    }

    const auto visible = cam::calculated_toolpaths(document_.toolpaths);
    canvas_widget_->canvas->refresh_toolpaths(visible);
    refresh_bridge_markers();
    refresh_3d_view();
    refresh_order_service_page();
}

void MainWindow::refresh_bridge_markers() {
    if (!canvas_widget_ || !canvas_widget_->canvas) return;
    std::shared_ptr<cam::ToolpathJob> selected_job;
    if (!bridge_target_job_id_.empty()) {
        for (const auto& job : document_.toolpaths) {
            if (job && job->id == bridge_target_job_id_) {
                selected_job = job;
                break;
            }
        }
    }
    if (!selected_job && toolpath_list_) {
        const int row = toolpath_list_->currentRow();
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            selected_job = document_.toolpaths[static_cast<size_t>(row)];
    }

    const auto* settings = selected_job
                               ? std::get_if<cam::ProfileSettings>(&selected_job->settings)
                               : nullptr;
    if (!selected_job || !settings || !settings->tabs) {
        canvas_widget_->canvas->set_profile_tab_markers({}, false);
        return;
    }

    const auto entities = selected_job->entities(document_);
    const auto preview = cam::profile_tab_preview(entities, *settings, &selected_job->tool);
    std::vector<vp::TabPlacement> markers;
    markers.reserve(preview.size());
    for (const cam::TabPreview& tab : preview)
        markers.push_back({tab.entity_id, tab.x, tab.y, tab.manual});
    canvas_widget_->canvas->set_profile_tab_markers(markers, true);
}

void MainWindow::add_manual_bridge(double x, double y) {
    if (bridge_target_job_id_.empty()) return;
    auto found = std::find_if(document_.toolpaths.begin(), document_.toolpaths.end(),
                              [&](const auto& job) {
                                  return job && job->id == bridge_target_job_id_;
                              });
    if (found == document_.toolpaths.end()) return;
    const auto& job = *found;
    auto* settings = std::get_if<cam::ProfileSettings>(&job->settings);
    if (!settings) return;

    const auto entities = job->entities(document_);
    const auto existing = cam::profile_tab_preview(entities, *settings, &job->tool);
    const double tolerance = std::max(1.5, canvas_widget_->canvas->pick_tolerance() * 1.5);
    int hit_index = -1;
    double hit_distance = tolerance;
    for (size_t i = 0; i < existing.size(); ++i) {
        const double distance = std::hypot(existing[i].x - x, existing[i].y - y);
        if (distance <= hit_distance) {
            hit_distance = distance;
            hit_index = static_cast<int>(i);
        }
    }
    if (hit_index >= 0) {
        settings->manual_tabs.clear();
        for (size_t i = 0; i < existing.size(); ++i) {
            if (i == static_cast<size_t>(hit_index)) continue;
            settings->manual_tabs.push_back({existing[i].entity_id, existing[i].x, existing[i].y});
        }
        settings->tab_manual_only = !settings->manual_tabs.empty();
        settings->tabs = !settings->manual_tabs.empty();
        document_.modified = true;
        const auto errors = document_.recalculate_toolpaths(job);
        refresh_toolpaths();
        statusBar()->showMessage(errors.empty()
                                     ? QString("Ponte removida; as restantes foram preservadas manualmente. Use Redistribuir para voltar ao automático.")
                                     : QString::fromStdString(errors.front()),
                                 5000);
        return;
    }

    std::optional<cam::TabPreview> best;
    double best_distance = 1.0e100;
    for (const auto& entity : entities) {
        if (!entity || !entity->closed()) continue;
        cam::ProfileSettings projection = *settings;
        projection.tabs = true;
        projection.tab_manual_only = true;
        projection.manual_tabs = {{entity->id, x, y}};
        const auto projected = cam::profile_tab_preview({entity}, projection, &job->tool);
        if (projected.empty()) continue;
        const double distance = std::hypot(projected.front().x - x, projected.front().y - y);
        if (distance < best_distance) {
            best_distance = distance;
            best = projected.front();
        }
    }

    const double placement_tolerance = std::max(1.5, canvas_widget_->canvas->pick_tolerance() * 2.5);
    if (!best || best_distance > placement_tolerance) {
        statusBar()->showMessage("Clique perto de um contorno fechado para posicionar a ponte.", 4000);
        return;
    }
    for (const cam::ManualTab& tab : settings->manual_tabs) {
        if (tab.entity_id == best->entity_id &&
            std::hypot(tab.x - best->x, tab.y - best->y) < settings->tab_length * 0.65) {
            statusBar()->showMessage("Ja existe uma ponte manual proxima deste ponto.", 3000);
            return;
        }
    }

    settings->tabs = true;
    settings->manual_tabs.push_back({best->entity_id, best->x, best->y});
    document_.modified = true;
    const auto errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    statusBar()->showMessage(errors.empty()
                                 ? QString("Ponte manual posicionada em X %1, Y %2 mm. Arraste para ajustar; Esc encerra.")
                                       .arg(best->x, 0, 'f', 2).arg(best->y, 0, 'f', 2)
                                 : QString::fromStdString(errors.front()));
}

void MainWindow::move_bridge_marker(int index, double x, double y) {
    std::shared_ptr<cam::ToolpathJob> job;
    if (!bridge_target_job_id_.empty()) {
        for (const auto& candidate : document_.toolpaths) {
            if (candidate && candidate->id == bridge_target_job_id_) {
                job = candidate;
                break;
            }
        }
    }
    if (!job && toolpath_list_) {
        const int row = toolpath_list_->currentRow();
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            job = document_.toolpaths[static_cast<size_t>(row)];
    }
    auto* settings = job ? std::get_if<cam::ProfileSettings>(&job->settings) : nullptr;
    if (!job || !settings || !settings->tabs) return;

    const auto entities = job->entities(document_);
    const auto placements = cam::profile_tab_preview(entities, *settings, &job->tool);
    if (index < 0 || index >= static_cast<int>(placements.size())) return;

    std::optional<cam::TabPreview> moved;
    double best_distance = 1.0e100;
    for (const auto& entity : entities) {
        if (!entity || !entity->closed()) continue;
        cam::ProfileSettings projection = *settings;
        projection.tabs = true;
        projection.tab_manual_only = true;
        projection.manual_tabs = {{entity->id, x, y}};
        const auto projected = cam::profile_tab_preview({entity}, projection, &job->tool);
        if (projected.empty()) continue;
        const double distance = std::hypot(projected.front().x - x, projected.front().y - y);
        if (distance < best_distance) {
            best_distance = distance;
            moved = projected.front();
        }
    }
    const double tolerance = std::max(1.5, canvas_widget_->canvas->pick_tolerance() * 2.5);
    if (!moved || best_distance > tolerance) {
        refresh_bridge_markers();
        statusBar()->showMessage("Solte a ponte sobre um contorno fechado.", 3000);
        return;
    }

    settings->manual_tabs.clear();
    settings->manual_tabs.reserve(placements.size());
    for (size_t i = 0; i < placements.size(); ++i) {
        const cam::TabPreview& tab = i == static_cast<size_t>(index) ? *moved : placements[i];
        settings->manual_tabs.push_back({tab.entity_id, tab.x, tab.y});
    }
    settings->tabs = true;
    settings->tab_manual_only = true;
    document_.modified = true;
    const auto errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    if (!errors.empty()) statusBar()->showMessage(QString::fromStdString(errors.front()));
}

void MainWindow::stop_manual_bridge_mode() {
    if (!canvas_widget_ || !canvas_widget_->canvas || !canvas_widget_->canvas->manual_tab_mode) return;
    canvas_widget_->canvas->manual_tab_mode = false;
    canvas_widget_->canvas->setCursor(Qt::ArrowCursor);
    bridge_target_job_id_.clear();
    if (bridge_mode_banner_) bridge_mode_banner_->setVisible(false);
    refresh_bridge_markers();
    statusBar()->showMessage("Marcacao manual de pontes encerrada.", 3500);
}

void MainWindow::refresh_3d_view() {
    if (!viewport_3d_ || (view_stack_ && view_stack_->currentWidget() != viewport_3d_)) return;
#if !defined(CNC_HAVE_VTK)
    if (!viewport_3d_->isValid()) return;
#endif

    viewport_3d_->clear_entities();
    const bool simulation_active = simulation_ != nullptr;
    if (!simulation_active) viewport_3d_->clear_simulation();

    const auto& material = document_.material;
    const double z_top = material.surface_z();
    const double z_bottom = z_top - material.thickness;
    const std::array<double, 4> scene_bounds = {material.width, material.height, z_bottom, z_top};
    const bool bounds_changed = !view_3d_scene_bounds_ || *view_3d_scene_bounds_ != scene_bounds;
    view_3d_scene_bounds_ = scene_bounds;
    viewport_3d_->set_workpiece(material.width, material.height, z_bottom, z_top);

    const std::array<float, 3> stock_color = material_display_color(material);
    const auto add_line = [](std::vector<float>& vertices, double x1, double y1, double z1,
                             double x2, double y2, double z2, const float color[3]) {
        vertices.insert(vertices.end(), {static_cast<float>(x1), static_cast<float>(y1),
                                         static_cast<float>(z1), color[0], color[1], color[2],
                                         static_cast<float>(x2), static_cast<float>(y2),
                                         static_cast<float>(z2), color[0], color[1], color[2]});
    };
    const std::array<std::pair<double, double>, 4> corners = {{{0.0, 0.0},
                                                                {material.width, 0.0},
                                                                {material.width, material.height},
                                                                {0.0, material.height}}};
    std::vector<float> stock;
    for (double z : {z_bottom, z_top}) {
        for (size_t i = 0; i < corners.size(); ++i) {
            const auto [x1, y1] = corners[i];
            const auto [x2, y2] = corners[(i + 1) % corners.size()];
            add_line(stock, x1, y1, z, x2, y2, z, stock_color.data());
        }
    }
    for (const auto& [x, y] : corners)
        add_line(stock, x, y, z_bottom, x, y, z_top, stock_color.data());
    viewport_3d_->add_entity_lines(stock);

    const auto origin = material.origin_xy();
    viewport_3d_->set_origin(origin.x, origin.y, 0.0);
    for (const auto& entity : document_.entities) {
        const auto* layer = document_.get_layer(entity->layer);
        if (layer && !layer->visible) continue;
        const Points points = entity->to_points(material.curve_segments());
        if (points.size() < 2) continue;
        const Rgb color = entity->selected ? cad::kSelColor : entity->color;
        const std::array<float, 3> entity_color = {
            static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2])};
        std::vector<float> vertices;
        const size_t segment_count = entity->closed() ? points.size() : points.size() - 1;
        for (size_t i = 0; i < segment_count; ++i) {
            const size_t next = (i + 1) % points.size();
            add_line(vertices, points[i].x, points[i].y, z_top,
                     points[next].x, points[next].y, z_top,
                     entity_color.data());
        }
        if (!vertices.empty()) viewport_3d_->add_entity_lines(vertices);
    }

    const auto visible = cam::calculated_toolpaths(document_.toolpaths);
    for (const cam::Toolpath* path : visible) {
        if (simulation_active) break;
        if (path->moves.size() < 2) continue;
        std::vector<float> vertices;
        for (size_t i = 1; i < path->moves.size(); ++i) {
            const cam::Move& a = path->moves[i - 1];
            const cam::Move& b = path->moves[i];
            const bool rapid = b.kind == cam::MoveKind::Rapid;
            const float color[] = {rapid ? 0.42f : 0.28f,
                                   rapid ? 0.52f : 0.88f,
                                   rapid ? 0.65f : 0.52f};
            add_line(vertices, a.x, a.y, a.z, b.x, b.y, b.z, color);
        }
        if (!vertices.empty()) viewport_3d_->add_entity_lines(vertices);
    }
    if (bounds_changed) viewport_3d_->set_standard_view("iso");
    else viewport_3d_->update();
}

void MainWindow::edit_tool_database() {
    (void)edit_tool_database("", nullptr);
}

bool MainWindow::edit_tool_database(const std::string& preselected_tool_name, cam::Tool* out_tool) {
    cam::ToolDatabase database = cam::ToolDatabase::load();

    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle(preselected_tool_name.empty()
        ? "Banco de ferramentas"
        : QString("Banco de ferramentas — %1").arg(QString::fromStdString(preselected_tool_name)));
    dialog.setMinimumSize(900, 600);
    dialog.resize(1080, 700);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(&dialog);
    header->setObjectName("BrandHeader");
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(18, 13, 18, 13);
    auto* header_icon = new QLabel(header);
    header_icon->setPixmap(ui::get_icon("tp_tools").pixmap(30, 30));
    header_layout->addWidget(header_icon);
    auto* header_text = new QVBoxLayout();
    header_text->setSpacing(1);
    auto* title = new QLabel("BANCO DE FERRAMENTAS", header);
    title->setObjectName("SectionHeader");
    title->setStyleSheet("font-size: 15px; color: #d9ecff;");
    header_text->addWidget(title);
    auto* subtitle = new QLabel("Geometria e parametros de corte em milimetros", header);
    subtitle->setObjectName("MutedText");
    header_text->addWidget(subtitle);
    header_layout->addLayout(header_text, 1);
    root->addWidget(header);

    auto* splitter = new QSplitter(Qt::Horizontal, &dialog);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    root->addWidget(splitter, 1);

    auto* library_panel = new QWidget(splitter);
    auto* library_layout = new QVBoxLayout(library_panel);
    library_layout->setContentsMargins(14, 14, 10, 10);
    library_layout->setSpacing(8);
    auto* library_title = new QLabel("LISTA DE FERRAMENTAS", library_panel);
    library_title->setObjectName("SectionHeader");
    library_layout->addWidget(library_title);

    auto* tree = new QTreeWidget(library_panel);
    tree->setHeaderHidden(true);
    tree->setObjectName("ToolpathList");
    tree->setIndentation(17);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setMinimumWidth(180);
    library_layout->addWidget(tree, 1);

    auto* library_buttons = new QGridLayout();
    library_buttons->setHorizontalSpacing(6);
    library_buttons->setVerticalSpacing(6);
    auto* new_button = new QPushButton("Nova", library_panel);
    auto* copy_button = new QPushButton("Copiar", library_panel);
    auto* delete_button = new QPushButton("Apagar", library_panel);
    new_button->setIcon(ui::get_icon("tp_tools"));
    copy_button->setIcon(ui::get_icon("tp_duplicate"));
    delete_button->setIcon(ui::get_icon("tp_delete"));
    library_buttons->addWidget(new_button, 0, 0);
    library_buttons->addWidget(copy_button, 0, 1);
    library_buttons->addWidget(delete_button, 0, 2);
    library_layout->addLayout(library_buttons);

    auto* transfer_buttons = new QHBoxLayout();
    auto* import_button = new QPushButton("Importar Aspire...", library_panel);
    auto* export_button = new QPushButton("Exportar...", library_panel);
    import_button->setIcon(ui::get_icon("import"));
    export_button->setIcon(ui::get_icon("export"));
    transfer_buttons->addWidget(import_button, 1);
    transfer_buttons->addWidget(export_button, 1);
    library_layout->addLayout(transfer_buttons);

    auto* editor_scroll = new QScrollArea(splitter);
    editor_scroll->setWidgetResizable(true);
    editor_scroll->setFrameShape(QFrame::NoFrame);
    editor_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* editor_panel = new QWidget(editor_scroll);
    auto* editor_layout = new QVBoxLayout(editor_panel);
    editor_layout->setContentsMargins(16, 14, 18, 10);
    editor_layout->setSpacing(10);
    editor_scroll->setWidget(editor_panel);

    auto add_group = [&](const QString& group_title) {
        auto* box = new QGroupBox(group_title, editor_panel);
        auto* form = new QFormLayout(box);
        form->setContentsMargins(13, 11, 13, 11);
        form->setSpacing(7);
        form->setLabelAlignment(Qt::AlignRight);
        editor_layout->addWidget(box);
        return form;
    };

    auto* identity = add_group("Identificacao");
    auto* name_field = new QLineEdit(editor_panel);
    identity->addRow("Nome", name_field);
    auto* type_field = new QComboBox(editor_panel);
    const std::vector<std::pair<QString, cam::ToolType>> tool_types = {
        {"Fresa de topo", cam::ToolType::EndMill},
        {"Fresa esferica", cam::ToolType::BallNose},
        {"Fresa em V", cam::ToolType::VBit},
        {"Ponta de gravacao", cam::ToolType::Engraving},
        {"Topo com raio", cam::ToolType::Radiused},
        {"Esferica conica", cam::ToolType::TaperedBall},
        {"Broca", cam::ToolType::Drill},
    };
    for (const auto& [label, kind] : tool_types)
        type_field->addItem(label, static_cast<int>(kind));
    identity->addRow("Tipo", type_field);
    auto* number_field = new QSpinBox(editor_panel);
    number_field->setRange(0, 999);
    identity->addRow("Numero da ferramenta", number_field);
    auto* flutes_field = new QSpinBox(editor_panel);
    flutes_field->setRange(1, 99);
    identity->addRow("Numero de cortes", flutes_field);

    auto* geometry = add_group("Geometria");
    struct ToolRow { QWidget* field; QWidget* label; };
    std::vector<ToolRow> special_geometry_rows;
    auto add_geometry_double = [&](const QString& label, double minimum, double maximum,
                                   int decimals, const QString& suffix, bool special = false) {
        auto* field = new QDoubleSpinBox(editor_panel);
        field->setRange(minimum, maximum);
        field->setDecimals(decimals);
        field->setSuffix(suffix);
        geometry->addRow(label, field);
        if (special) special_geometry_rows.push_back({field, geometry->labelForField(field)});
        return field;
    };
    auto* diameter_field = add_geometry_double("Diametro", 0.001, 1000.0, 3, " mm");
    auto* angle_field = add_geometry_double("Angulo V", 1.0, 179.0, 1, " deg", true);
    auto* tip_field = add_geometry_double("Diametro da ponta", 0.0, 1000.0, 3, " mm", true);
    auto* radius_field = add_geometry_double("Raio de canto", 0.0, 500.0, 3, " mm", true);

    auto* tool_guide = new ui::MachiningGuide("09_ferramentas", editor_panel);
    tool_guide->setMinimumHeight(255);
    editor_layout->addWidget(tool_guide);
    const auto update_tool_guide = [tool_guide, type_field, diameter_field] {
        tool_guide->model()->setValue("toolType", type_field->currentData());
        tool_guide->model()->setValue("diameter", diameter_field->value());
    };
    connect(type_field, &QComboBox::currentIndexChanged, tool_guide, update_tool_guide);
    connect(diameter_field, &QDoubleSpinBox::valueChanged, tool_guide, update_tool_guide);
    tool_guide->mode_clicked = [type_field](int type) {
        type_field->setCurrentIndex(type_field->findData(type));
    };

    auto* cutting = add_group("Dados de corte");
    auto add_cut_double = [&](const QString& label, double minimum, double maximum,
                              int decimals, const QString& suffix) {
        auto* field = new QDoubleSpinBox(editor_panel);
        field->setRange(minimum, maximum);
        field->setDecimals(decimals);
        field->setSuffix(suffix);
        cutting->addRow(label, field);
        return field;
    };
    auto* pass_depth_field = add_cut_double("Profundidade por passada", 0.0, 1000.0, 3, " mm");
    auto* stepover_field = add_cut_double("Passo lateral", 0.0, 1000.0, 3, " mm");
    auto* clearance_field = add_cut_double("Passo de desbaste", 0.0, 1000.0, 3, " mm");
    auto* final_stepover_field = add_cut_double("Passo final", 0.0, 1000.0, 3, " mm");
    auto* rpm_field = new QSpinBox(editor_panel);
    rpm_field->setRange(0, 100000);
    cutting->addRow("Rotacao", rpm_field);
    auto* feed_field = add_cut_double("Avanco XY", 0.0, 100000.0, 1, " mm/min");
    auto* plunge_field = add_cut_double("Mergulho Z", 0.0, 100000.0, 1, " mm/min");

    auto* notes_group = add_group("Notas");
    auto* notes_field = new QTextEdit(editor_panel);
    notes_field->setMinimumHeight(76);
    notes_field->setAcceptRichText(false);
    notes_group->addRow(notes_field);
    editor_layout->addStretch();

    splitter->addWidget(library_panel);
    splitter->addWidget(editor_scroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 690});

    auto* footer = new QWidget(&dialog);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(14, 9, 14, 9);
    auto* path_label = new QLabel(
        QString("Arquivo: %1").arg(QString::fromStdWString(database.path.wstring())), footer);
    path_label->setObjectName("MutedText");
    path_label->setWordWrap(true);
    path_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    footer_layout->addWidget(path_label, 1);
    auto* apply_button = new QPushButton("Aplicar", footer);
    apply_button->setObjectName("ApplyBtn");
    footer_layout->addWidget(apply_button);
    auto* dialog_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    dialog_buttons->button(QDialogButtonBox::Ok)->setText("OK");
    dialog_buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    footer_layout->addWidget(dialog_buttons);
    root->addWidget(footer);

    int current_row = -1;
    if (!preselected_tool_name.empty()) {
        for (size_t i = 0; i < database.tools.size(); ++i) {
            if (database.tools[i]->name == preselected_tool_name) {
                current_row = static_cast<int>(i);
                break;
            }
        }
        if (current_row < 0) {
            const QString search = QString::fromStdString(preselected_tool_name).trimmed().toLower();
            for (size_t i = 0; i < database.tools.size(); ++i) {
                if (QString::fromStdString(database.tools[i]->name).trimmed().toLower() == search) {
                    current_row = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (current_row < 0 && !database.tools.empty()) {
        current_row = 0;
    }
    bool loading = false;
    std::vector<std::pair<QWidget*, QWidget*>> geometry_rows;
    for (const auto& row : special_geometry_rows) geometry_rows.push_back({row.field, row.label});

    auto update_geometry_visibility = [&] {
        const auto kind = static_cast<cam::ToolType>(type_field->currentData().toInt());
        const bool v_tool = kind == cam::ToolType::VBit || kind == cam::ToolType::Engraving;
        const bool radius_tool = kind == cam::ToolType::Radiused;
        geometry_rows[0].first->setVisible(v_tool);
        geometry_rows[0].second->setVisible(v_tool);
        geometry_rows[1].first->setVisible(v_tool || kind == cam::ToolType::TaperedBall);
        geometry_rows[1].second->setVisible(v_tool || kind == cam::ToolType::TaperedBall);
        geometry_rows[2].first->setVisible(radius_tool);
        geometry_rows[2].second->setVisible(radius_tool);
    };

    auto write_editor = [&] {
        if (loading || current_row < 0 || current_row >= static_cast<int>(database.tools.size())) return;
        cam::Tool& tool = *database.tools[static_cast<size_t>(current_row)];
        tool.name = name_field->text().trimmed().toStdString();
        if (tool.name.empty()) tool.name = "Nova ferramenta";
        tool.tool_type = static_cast<cam::ToolType>(type_field->currentData().toInt());
        tool.tool_number = number_field->value();
        tool.num_flutes = flutes_field->value();
        tool.diameter = diameter_field->value();
        tool.v_angle = angle_field->value();
        tool.tip_diameter = tip_field->value();
        tool.corner_radius = radius_field->value();
        tool.pass_depth = pass_depth_field->value();
        tool.stepover = stepover_field->value();
        tool.clearance_stepover = clearance_field->value();
        tool.final_stepover = final_stepover_field->value();
        tool.spindle_speed = rpm_field->value();
        tool.feed_rate = feed_field->value();
        tool.plunge_rate = plunge_field->value();
        tool.notes = notes_field->toPlainText().toStdString();
        if (auto* item = tree->currentItem(); item &&
            item->data(0, Qt::UserRole).toInt() == current_row)
            item->setText(0, QString::fromStdString(tool.name));
    };

    auto load_editor = [&](int row) {
        if (row < 0 || row >= static_cast<int>(database.tools.size())) return;
        loading = true;
        current_row = row;
        const cam::Tool& tool = *database.tools[static_cast<size_t>(row)];
        name_field->setText(QString::fromStdString(tool.name));
        type_field->setCurrentIndex(std::max(0, type_field->findData(static_cast<int>(tool.tool_type))));
        number_field->setValue(tool.tool_number);
        flutes_field->setValue(tool.num_flutes);
        diameter_field->setValue(tool.diameter);
        angle_field->setValue(tool.v_angle);
        tip_field->setValue(tool.tip_diameter);
        radius_field->setValue(tool.corner_radius);
        pass_depth_field->setValue(tool.pass_depth);
        stepover_field->setValue(tool.stepover);
        clearance_field->setValue(tool.clearance_stepover);
        final_stepover_field->setValue(tool.final_stepover);
        rpm_field->setValue(tool.spindle_speed);
        feed_field->setValue(tool.feed_rate);
        plunge_field->setValue(tool.plunge_rate);
        notes_field->setPlainText(QString::fromStdString(tool.notes));
        loading = false;
        update_geometry_visibility();
    };

    auto icon_for_type = [](cam::ToolType kind) {
        return kind == cam::ToolType::Drill ? QString("tp_drill") : QString("tp_tools");
    };
    auto refresh_tree = [&] {
        QSignalBlocker blocker(tree);
        tree->clear();
        auto* root_item = new QTreeWidgetItem(tree, {"Biblioteca de ferramentas"});
        root_item->setIcon(0, ui::get_icon("tp_tools"));
        root_item->setExpanded(true);
        std::vector<QTreeWidgetItem*> groups;
        for (const auto& [label, kind] : tool_types) {
            auto* group = new QTreeWidgetItem(root_item, {label});
            group->setData(0, Qt::UserRole, -1);
            group->setExpanded(true);
            groups.push_back(group);
        }
        for (size_t i = 0; i < database.tools.size(); ++i) {
            const auto& tool = database.tools[i];
            int group_index = 0;
            for (size_t k = 0; k < tool_types.size(); ++k) {
                if (tool_types[k].second == tool->tool_type) {
                    group_index = static_cast<int>(k);
                    break;
                }
            }
            auto* item = new QTreeWidgetItem(groups[static_cast<size_t>(group_index)],
                                             {QString::fromStdString(tool->name)});
            item->setData(0, Qt::UserRole, static_cast<int>(i));
            item->setIcon(0, ui::get_icon(icon_for_type(tool->tool_type)));
            item->setToolTip(0, QString::fromStdString(tool->describe()));
        }
        if (current_row >= 0 && current_row < static_cast<int>(database.tools.size())) {
            for (int g = 0; g < root_item->childCount(); ++g) {
                auto* group = root_item->child(g);
                for (int i = 0; i < group->childCount(); ++i) {
                    auto* item = group->child(i);
                    if (item->data(0, Qt::UserRole).toInt() == current_row) {
                        tree->setCurrentItem(item);
                        tree->scrollToItem(item);
                        return;
                    }
                }
            }
        }
    };

    refresh_tree();
    if (current_row >= 0 && current_row < static_cast<int>(database.tools.size())) {
        load_editor(current_row);
    }

    connect(tree, &QTreeWidget::currentItemChanged, &dialog,
            [&](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (!item) return;
                const int row = item->data(0, Qt::UserRole).toInt();
                if (row < 0 || row >= static_cast<int>(database.tools.size()) || row == current_row) return;
                write_editor();
                load_editor(row);
            });
    connect(type_field, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&](int) { if (!loading) update_geometry_visibility(); });

    connect(new_button, &QPushButton::clicked, &dialog, [&] {
        write_editor();
        cam::Tool tool;
        tool.name = "Nova ferramenta";
        auto created = database.add(std::move(tool));
        current_row = static_cast<int>(std::distance(
            database.tools.begin(), std::find(database.tools.begin(), database.tools.end(), created)));
        refresh_tree();
        load_editor(current_row);
    });
    connect(copy_button, &QPushButton::clicked, &dialog, [&] {
        if (current_row < 0 || current_row >= static_cast<int>(database.tools.size())) return;
        write_editor();
        cam::Tool copy = *database.tools[static_cast<size_t>(current_row)];
        copy.name += " - copia";
        auto created = database.add(std::move(copy));
        current_row = static_cast<int>(std::distance(
            database.tools.begin(), std::find(database.tools.begin(), database.tools.end(), created)));
        refresh_tree();
        load_editor(current_row);
    });
    connect(delete_button, &QPushButton::clicked, &dialog, [&] {
        if (current_row < 0 || current_row >= static_cast<int>(database.tools.size())) return;
        if (database.tools.size() == 1) {
            QMessageBox::information(&dialog, "Banco de ferramentas",
                                     "Mantenha pelo menos uma ferramenta no banco.");
            return;
        }
        write_editor();
        if (QMessageBox::question(&dialog, "Apagar ferramenta",
                                  QString("Apagar %1?").arg(name_field->text())) != QMessageBox::Yes)
            return;
        database.remove(database.tools[static_cast<size_t>(current_row)]);
        current_row = std::min(current_row, static_cast<int>(database.tools.size()) - 1);
        refresh_tree();
        load_editor(current_row);
    });
    connect(import_button, &QPushButton::clicked, &dialog, [&] {
        write_editor();
        const QString path = QFileDialog::getOpenFileName(
            &dialog, "Importar banco de ferramentas do Aspire", {},
            "Banco Aspire (*.tool_db);;Todos os arquivos (*)");
        if (path.isEmpty()) return;
        try {
            const auto report = cam::import_aspire_tools(
                database, std::filesystem::path(path.toStdWString()));
            if (!report.imported.empty()) {
                current_row = static_cast<int>(std::distance(
                    database.tools.begin(), std::find(database.tools.begin(), database.tools.end(), report.imported.front())));
            }
            refresh_tree();
            if (current_row >= 0) load_editor(current_row);
            QMessageBox::information(
                &dialog, "Importacao concluida",
                QString("%1 ferramenta(s) importada(s).\n%2 duplicata(s) ignorada(s).\n%3 registro(s) sem suporte.")
                    .arg(static_cast<int>(report.imported.size()))
                    .arg(report.duplicates)
                    .arg(static_cast<int>(report.unsupported.size())));
        } catch (const std::exception& error) {
            QMessageBox::critical(&dialog, "Erro na importacao", QString::fromUtf8(error.what()));
        }
    });
    connect(export_button, &QPushButton::clicked, &dialog, [&] {
        write_editor();
        const QString path = QFileDialog::getSaveFileName(
            &dialog, "Exportar banco de ferramentas", "ferramentas.json",
            "Banco CNC-CAM (*.json);;Todos os arquivos (*)");
        if (path.isEmpty()) return;
        try {
            cam::ToolDatabase exported = database;
            exported.path = std::filesystem::path(path.toStdWString());
            exported.save();
            statusBar()->showMessage(QString("Banco exportado: %1").arg(path));
        } catch (const std::exception& error) {
            QMessageBox::critical(&dialog, "Erro ao exportar", QString::fromUtf8(error.what()));
        }
    });

    auto save_database = [&]() {
        write_editor();
        try {
            database.save();
            statusBar()->showMessage(QString("Banco salvo: %1 ferramenta(s)").arg(database.tools.size()));
            return true;
        } catch (const std::exception& error) {
            QMessageBox::critical(&dialog, "Erro ao salvar banco", QString::fromUtf8(error.what()));
            return false;
        }
    };
    connect(apply_button, &QPushButton::clicked, &dialog, [&] { save_database(); });
    connect(dialog_buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (save_database()) dialog.accept();
    });
    const bool accepted = (dialog.exec() == QDialog::Accepted);
    if (accepted && out_tool && current_row >= 0 && current_row < static_cast<int>(database.tools.size())) {
        *out_tool = *database.tools[static_cast<size_t>(current_row)];
    }
    return accepted;
}

void MainWindow::edit_material_setup() {
    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle("Definições do material");
    dialog.setMinimumWidth(360);
    auto* form = new QFormLayout(&dialog);

    const auto make_number = [&dialog](double value, double minimum, double maximum) {
        auto* box = new QDoubleSpinBox(&dialog);
        box->setRange(minimum, maximum);
        box->setDecimals(3);
        box->setValue(value);
        box->setSuffix(" mm");
        return box;
    };
    auto* width = make_number(document_.material.width, 0.01, 100000.0);
    auto* height = make_number(document_.material.height, 0.01, 100000.0);
    auto* thickness = make_number(document_.material.thickness, 0.01, 10000.0);
    auto* margin = make_number(document_.material.margin, 0.0, 100000.0);
    auto* clearance = make_number(document_.material.clearance_z, 0.0, 10000.0);
    auto* plunge_gap = make_number(document_.material.plunge_gap, 0.0, 10000.0);
    auto* zero_x = new QComboBox(&dialog);
    zero_x->addItem("Esquerda", "left");
    zero_x->addItem("Centro", "center");
    zero_x->addItem("Direita", "right");
    zero_x->setCurrentIndex(zero_x->findData(QString::fromStdString(document_.material.zero_x)));
    auto* zero_y = new QComboBox(&dialog);
    zero_y->addItem("Inferior", "bottom");
    zero_y->addItem("Centro", "center");
    zero_y->addItem("Superior", "top");
    zero_y->setCurrentIndex(zero_y->findData(QString::fromStdString(document_.material.zero_y)));
    auto* zero_z = new QComboBox(&dialog);
    zero_z->addItem("Topo", "top");
    zero_z->addItem("Base", "bottom");
    zero_z->setCurrentIndex(zero_z->findData(QString::fromStdString(document_.material.zero_z)));

    form->addRow("Largura", width);
    form->addRow("Altura", height);
    form->addRow("Espessura", thickness);
    form->addRow("Margem interna", margin);
    form->addRow("Altura de segurança", clearance);
    form->addRow("Distância de mergulho", plunge_gap);
    form->addRow("Origem X", zero_x);
    form->addRow("Origem Y", zero_y);
    form->addRow("Origem Z", zero_z);
    auto* material_guide = new ui::MachiningGuide("08_material", &dialog);
    material_guide->setMinimumHeight(255);
    auto* material_model = material_guide->model();
    const auto material_number = [&](QDoubleSpinBox* field, const QString& key) {
        material_model->setValue(key, field->value());
        connect(field, &QDoubleSpinBox::valueChanged, material_model,
            [material_model, key](double value) { material_model->setValue(key, value); });
    };
    material_number(width, "width"); material_number(height, "height");
    material_number(thickness, "thickness"); material_number(clearance, "clearance");
    const auto material_origin = [&](QComboBox* field, const QString& key) {
        material_model->setValue(key, field->currentIndex());
        connect(field, &QComboBox::currentIndexChanged, material_model,
            [material_model, key](int index) { material_model->setValue(key, index); });
        connect(material_model, &ui::GuideModel::valueChanged, field,
            [field, key](const QString& changed, const QVariant& value) {
                if (key == changed) field->setCurrentIndex(value.toInt());
            });
    };
    material_origin(zero_x, "originX"); material_origin(zero_y, "originY"); material_origin(zero_z, "zeroZ");
    form->addRow(material_guide);
    dialog.resize(690, 800);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;
    document_.material.width = width->value();
    document_.material.height = height->value();
    document_.material.thickness = thickness->value();
    document_.material.margin = margin->value();
    document_.material.clearance_z = clearance->value();
    document_.material.plunge_gap = plunge_gap->value();
    document_.material.zero_x = zero_x->currentData().toString().toStdString();
    document_.material.zero_y = zero_y->currentData().toString().toStdString();
    document_.material.zero_z = zero_z->currentData().toString().toStdString();
    document_.modified = true;
    for (const auto& job : document_.toolpaths) {
        job->results.clear();
        job->error.clear();
    }
    refresh_document_view();
    statusBar()->showMessage("Definições do material atualizadas; recalcule os percursos");
}

bool MainWindow::configure_toolpath(cam::ToolpathJob& destination) {
    auto job = destination; // Edits and tool changes remain a draft until Calculate.
    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle(
        QString("Percurso de %1").arg(QString::fromUtf8(cam::toolpath_label(job.kind))));
    const QSize screen_size = screen() ? screen()->availableGeometry().size() : QSize(1280, 800);
    dialog.setMinimumSize(360, 320);
    dialog.resize(std::min(960, screen_size.width() - 48),
                  std::min(840, screen_size.height() - 80));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* top_bar = new QWidget(&dialog);
    auto* top_bar_layout = new QHBoxLayout(top_bar);
    top_bar_layout->setContentsMargins(16, 10, 16, 4);
    top_bar_layout->setSpacing(8);

    auto* selection = new QLabel(
        QString("%1 vetor(es) selecionado(s)").arg(job.entity_ids.size()), top_bar);
    selection->setStyleSheet("color: #96a9c1; padding: 2px 0; font-weight: 500;");
    top_bar_layout->addWidget(selection);
    top_bar_layout->addStretch();

    root->addWidget(top_bar);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(16, 0, 16, 12);
    body_layout->setSpacing(10);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    std::vector<std::pair<QString, QString>> guide_topics;
    auto add_guide_topic = [&](const char* label, const char* image) {
        guide_topics.emplace_back(QString::fromUtf8(label), QString::fromUtf8(image));
    };
    switch (job.kind) {
        case cam::ToolpathType::Profile:
            add_guide_topic("Posição da fresa", "01_usinar_vetores");
            add_guide_topic("Profundidades", "02_profundidades");
            add_guide_topic("Pontes", "03_pontes");
            add_guide_topic("Entradas e saídas", "04_entradas_saidas");
            add_guide_topic("Rampas", "05_rampas");
            break;
        case cam::ToolpathType::Pocket:
            add_guide_topic("Estratégia de limpeza", "06_limpar_area");
            add_guide_topic("Ataque rampeado", "07_ataque_rampeado");
            add_guide_topic("Profundidades", "02_profundidades");
            add_guide_topic("Material e origem", "08_material");
            break;
        case cam::ToolpathType::Drill:
            add_guide_topic("Ferramentas", "09_ferramentas");
            add_guide_topic("Material e origem", "08_material");
            break;
        case cam::ToolpathType::VCarve:
            add_guide_topic("Fresas e gravação", "09_ferramentas");
            add_guide_topic("Profundidades", "02_profundidades");
            break;
        case cam::ToolpathType::Fluting:
            add_guide_topic("Usinar vetores", "01_usinar_vetores");
            add_guide_topic("Rampas", "05_rampas");
            break;
        case cam::ToolpathType::Texture:
            add_guide_topic("Geometria e padrão", "10_estrela");
            add_guide_topic("Material e origem", "08_material");
            break;
        case cam::ToolpathType::Keyhole:
            add_guide_topic("Fresa e rasgo", "09_ferramentas");
            add_guide_topic("Usinar vetores", "01_usinar_vetores");
            break;
        case cam::ToolpathType::Inlay:
            add_guide_topic("Encaixe e contorno", "01_usinar_vetores");
            add_guide_topic("Material e origem", "08_material");
            break;
        case cam::ToolpathType::Dragknife:
            add_guide_topic("Ferramenta e lâmina", "09_ferramentas");
            add_guide_topic("Usinar vetores", "01_usinar_vetores");
            break;
    }
    auto* guide_model = new ui::GuideModel(&dialog);
    guide_model->setProperty("toolReadOnly", true);
    guide_model->setValue("originX", QStringList{"left", "center", "right"}.indexOf(QString::fromStdString(document_.material.zero_x)));
    guide_model->setValue("originY", QStringList{"bottom", "center", "top"}.indexOf(QString::fromStdString(document_.material.zero_y)));
    guide_model->setValue("zeroZ", document_.material.zero_z == "bottom" ? 1 : 0);
    guide_model->setValue("thickness", document_.material.thickness);
    guide_model->setValue("width", document_.material.width);
    guide_model->setValue("height", document_.material.height);
    guide_model->setValue("clearance", document_.material.clearance_z);
    guide_model->setValue("diameter", job.tool.diameter);
    guide_model->setValue("pass", job.tool.pass_depth);
    guide_model->setValue("stepover", job.tool.stepover);
    guide_model->setValue("toolStepover", job.tool.stepover);
    guide_model->setValue("toolType", static_cast<int>(job.tool.tool_type));
    // Profile illustrations stay as didactic examples (inside/outside, lead-in,
    // ramps, etc.). Only the main 3D viewport and OS preview use live geometry.

    const QMap<QString, QString> guide_keys = {
        {"Cota inicial (D)", "start"}, {"Cota inicial", "start"}, {"Profundidade (C)", "depth"}, {"Profundidade de corte", "depth"},
        {"Posição da fresa", "side"}, {"Vetor aberto", "openSide"}, {"Direção", "direction"},
        {"Pontes de fixação", "tabs"}, {"Comprimento da ponte", "tabLength"},
        {"Espessura da ponte", "tabHeight"}, {"Quantidade de pontes", "tabCount"},
        {"Pontes 3D com subida em rampa", "tabs3d"}, {"Tipo de entrada", "lead"},
        {"Comprimento", "leadLength"}, {"Raio do arco", "leadRadius"}, {"Ângulo", "leadAngle"},
        {"Criar saída", "leadOut"}, {"Tipo de rampa", "ramp"}, {"Rampa", "ramp"},
        {"Distância", "rampDistance"}, {"Distância da rampa", "rampDistance"}, {"Distância da rampa (L)", "rampDistance"},
        {"Preenchimento", "fill"}, {"Ângulo do raster", "rasterAngle"}, {"Passo lateral", "stepover"},
        {"Sobremetal", "allowance"}
    };
    const auto bind_guide_field = [guide_model, guide_keys, &dialog](const QString& label, QWidget* field) {
        QString key = guide_keys.value(label);
        if ((label == "Ângulo" || label == "Ângulo da rampa") && field->parentWidget()) {
            const QGroupBox* section = nullptr;
            for (QWidget* p = field->parentWidget(); p; p = p->parentWidget()) {
                if (auto* gb = qobject_cast<QGroupBox*>(p)) {
                    section = gb;
                    break;
                }
            }
            if (section && section->property("sectionTitle").toString() == "Rampas") key = "rampAngle";
        }
        if (key.isEmpty()) return;
        field->setObjectName("CamParameter_" + key);
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(field)) {
            const auto update = [guide_model, key, spin] { guide_model->setValue(key, spin->value()); };
            update();
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, update);
            QObject::connect(guide_model, &ui::GuideModel::valueChanged, spin,
                [spin, key](const QString& changed, const QVariant& value) { if (key == changed) spin->setValue(value.toDouble()); });
        } else if (auto* integer = qobject_cast<QSpinBox*>(field)) {
            guide_model->setValue(key, integer->value());
            QObject::connect(integer, &QSpinBox::valueChanged, &dialog,
                [guide_model, key](int value) { guide_model->setValue(key, value); });
        } else if (auto* combo = qobject_cast<QComboBox*>(field)) {
            guide_model->setValue(key, combo->currentData());
            QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog,
                [guide_model, key, combo] { guide_model->setValue(key, combo->currentData()); });
            QObject::connect(guide_model, &ui::GuideModel::valueChanged, combo,
                [combo, key](const QString& changed, const QVariant& value) {
                    if (key == changed) { const int index = combo->findData(value.toInt()); if (index >= 0) combo->setCurrentIndex(index); }
                });
        } else if (auto* check = qobject_cast<QCheckBox*>(field)) {
            guide_model->setValue(key, check->isChecked());
            QObject::connect(check, &QCheckBox::toggled, &dialog,
                [guide_model, key](bool value) { guide_model->setValue(key, value); });
            QObject::connect(guide_model, &ui::GuideModel::valueChanged, check,
                [check, key](const QString& changed, const QVariant& value) { if (key == changed) check->setChecked(value.toBool()); });
        }
    };

    std::vector<std::function<void()>> apply;
    auto add_double = [&](QFormLayout* form, const QString& label, double value,
                          double minimum, double maximum, const QString& suffix,
                          const std::function<void(double)>& setter) -> QDoubleSpinBox* {
        auto* field = new ui::SafeDoubleSpinBox(body);
        field->setRange(minimum, maximum);
        field->setDecimals(3);
        field->setSuffix(suffix);
        field->setValue(value);
        form->addRow(label, field);
        apply.push_back([field, setter] { setter(field->value()); });
        bind_guide_field(label, field);
        return field;
    };
    auto add_int = [&](QFormLayout* form, const QString& label, int value,
                       int minimum, int maximum, const std::function<void(int)>& setter) -> QSpinBox* {
        auto* field = new ui::SafeSpinBox(body);
        field->setRange(minimum, maximum);
        field->setValue(value);
        form->addRow(label, field);
        apply.push_back([field, setter] { setter(field->value()); });
        bind_guide_field(label, field);
        return field;
    };
    auto add_bool = [&](QFormLayout* form, const QString& label, bool value,
                        const std::function<void(bool)>& setter) -> QCheckBox* {
        auto* field = new QCheckBox(label, body);
        field->setChecked(value);
        form->addRow(QString(), field);
        apply.push_back([field, setter] { setter(field->isChecked()); });
        bind_guide_field(label, field);
        return field;
    };
    auto add_enum = [&](QFormLayout* form, const QString& label, int current,
                        const std::vector<std::pair<QString, int>>& options,
                        const std::function<void(int)>& setter) -> QComboBox* {
        auto* field = new ui::SafeComboBox(body);
        for (const auto& [text, value] : options)
            field->addItem(text, value);
        field->setCurrentIndex(std::max(0, field->findData(current)));
        form->addRow(label, field);
        apply.push_back([field, setter] { setter(field->currentData().toInt()); });
        field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        field->setMinimumContentsLength(8);
        bind_guide_field(label, field);
        return field;
    };
    using GuideTopics = std::vector<std::pair<QString, QString>>;
    std::vector<InteractiveGuideWidget*> interactive_guides;
    const auto update_guide_mode = [&interactive_guides](const QString& kind, int mode) {
        if (kind == "07_ataque_rampeado") return; // full ramp enum is already bound to the draft
        for (auto* guide : interactive_guides) {
            if (guide && guide->kind() == kind) guide->set_mode(mode);
        }
    };
    const auto guide_for = [&interactive_guides](const QString& kind) -> InteractiveGuideWidget* {
        for (auto* guide : interactive_guides) {
            if (guide && guide->kind() == kind) return guide;
        }
        return nullptr;
    };

    auto add_group = [&](const QString& title, GuideTopics guides = {}, bool collapsible = true) {
        auto* box = new QGroupBox(title, body);
        box->setObjectName("CamConfigGroup");
        box->setProperty("sectionTitle", title);
        auto* box_layout = new QVBoxLayout(box);
        box_layout->setContentsMargins(10, 4, 10, 8);
        box_layout->setSpacing(4);

        QToolButton* toggle_btn = nullptr;
        if (collapsible) {
            auto* header_bar = new QWidget(box);
            auto* header_layout = new QHBoxLayout(header_bar);
            header_layout->setContentsMargins(0, 0, 0, 0);
            header_layout->setSpacing(6);
            header_layout->addStretch();
            toggle_btn = new QToolButton(header_bar);
            toggle_btn->setObjectName("GroupCollapseBtn");
            toggle_btn->setText("Minimizar");
            toggle_btn->setCursor(Qt::PointingHandCursor);
            toggle_btn->setToolTip("Recolher ou expandir esta seção");
            header_layout->addWidget(toggle_btn);
            box_layout->addWidget(header_bar);
        }

        auto* content = new QWidget(box);
        auto* section = new QHBoxLayout(content);
        section->setContentsMargins(0, 0, 0, 0);
        section->setSpacing(14);
        // The scroll viewport is the actual available width. A section can
        // otherwise keep its wide minimum size and never switch to one column.
        new ui::AdaptiveSection(scroll->viewport(), section, 600);

        auto* fields = new QWidget(content);
        auto* form = new QFormLayout(fields);
        form->setContentsMargins(4, 4, 4, 4);
        form->setSpacing(6);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        section->addWidget(fields, 1);

        if (!guides.empty()) {
            auto* card = new QWidget(content);
            card->setObjectName("CamGuideCard");
            auto* guide_layout = new QVBoxLayout(card);
            guide_layout->setContentsMargins(0, 0, 0, 0);
            guide_layout->setSpacing(4);
            auto* choice = new ui::SafeComboBox(card);
            choice->setObjectName("CamGuideTopic");
            choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            choice->setMinimumContentsLength(10);
            for (const auto& [label, key] : guides) choice->addItem(label, key);
            auto* image = new InteractiveGuideWidget(guides.front().second, card, guide_model);
            image->setPlaying(true);
            image->setMinimumHeight(75);
            image->setMaximumHeight(115);
            interactive_guides.push_back(image);
            if (guides.size() > 1) guide_layout->addWidget(choice);
            else choice->hide();
            guide_layout->addWidget(image, 1);
            connect(choice, &QComboBox::currentIndexChanged, image,
                [choice, image] { image->set_kind(choice->currentData().toString()); });
            section->addWidget(card, 1);
        }

        box_layout->addWidget(content);
        body_layout->addWidget(box);
        if (toggle_btn) {
            QObject::connect(toggle_btn, &QToolButton::clicked, box, [content, toggle_btn] {
                const bool expanded = content->isHidden();
                content->setVisible(expanded);
                toggle_btn->setText(expanded ? "Minimizar" : "Expandir");
            });
        }

        return form;
    };

    auto* tool_form = add_group("Ferramenta", {{"Geometria da fresa", "09_ferramentas"}});
    auto* selected_tool_name = new QLabel(QString::fromStdString(job.tool.name), body);
    selected_tool_name->setObjectName("MutedText");
    selected_tool_name->setWordWrap(true);
    auto* tool_actions = new QWidget(body);
    auto* tool_actions_layout = new QHBoxLayout(tool_actions);
    tool_actions_layout->setContentsMargins(0, 0, 0, 0);
    tool_actions_layout->setSpacing(6);
    auto* select_tool_button = new QPushButton("Selecionar...", tool_actions);
    auto* edit_tools_button = new QPushButton("Editar...", tool_actions);
    select_tool_button->setIcon(ui::get_icon("tp_tools"));
    edit_tools_button->setIcon(ui::get_icon("tp_edit"));
    tool_actions_layout->addWidget(select_tool_button);
    tool_actions_layout->addWidget(edit_tools_button);
    tool_actions_layout->addStretch();
    tool_form->addRow("Biblioteca", tool_actions);
    tool_form->addRow("Selecionada", selected_tool_name);
    QGroupBox* tool_group_box = nullptr;
    for (QWidget* p = tool_form->parentWidget(); p; p = p->parentWidget()) {
        if (auto* gb = qobject_cast<QGroupBox*>(p)) {
            tool_group_box = gb;
            break;
        }
    }
    if (tool_group_box) body_layout->removeWidget(tool_group_box);
    std::function<void()> refresh_pass_summary;

    // Os parametros da ferramenta sao editados somente no banco. Mantemos este
    // trecho antigo desativado para que o dialogo do percurso nao duplique campos.
#if 0
    auto* tool_type_field = add_enum(tool_form, "Tipo", static_cast<int>(job.tool.tool_type),
             {{"Fresa de topo", static_cast<int>(cam::ToolType::EndMill)},
              {"Fresa esférica", static_cast<int>(cam::ToolType::BallNose)},
              {"Fresa V", static_cast<int>(cam::ToolType::VBit)},
              {"Gravação", static_cast<int>(cam::ToolType::Engraving)},
              {"Fresa raio", static_cast<int>(cam::ToolType::Radiused)},
              {"Fresa cônica", static_cast<int>(cam::ToolType::TaperedBall)},
              {"Broca", static_cast<int>(cam::ToolType::Drill)}},
             [&job](int value) { job.tool.tool_type = static_cast<cam::ToolType>(value); });
    add_double(tool_form, "Diâmetro", job.tool.diameter, 0.01, 1000.0, " mm",
               [&job](double value) { job.tool.diameter = value; });
    add_double(tool_form, "Passo Z", job.tool.pass_depth, 0.001, 1000.0, " mm",
               [&job](double value) { job.tool.pass_depth = value; });
    add_double(tool_form, "Passo lateral", job.tool.stepover, 0.001, 1000.0, " mm",
               [&job](double value) { job.tool.stepover = value; });
    add_double(tool_form, "Avanço XY", job.tool.feed_rate, 0.01, 100000.0, " mm/min",
               [&job](double value) { job.tool.feed_rate = value; });
    add_double(tool_form, "Mergulho Z", job.tool.plunge_rate, 0.01, 100000.0, " mm/min",
               [&job](double value) { job.tool.plunge_rate = value; });
    add_int(tool_form, "RPM", job.tool.spindle_speed, 0, 100000,
            [&job](int value) { job.tool.spindle_speed = value; });
    add_int(tool_form, "Número da ferramenta", job.tool.tool_number, 0, 999,
            [&job](int value) { job.tool.tool_number = value; });

    auto* tool_diameter_field = qobject_cast<QDoubleSpinBox*>(tool_form->itemAt(3, QFormLayout::FieldRole)->widget());
    auto* tool_pass_depth_field = qobject_cast<QDoubleSpinBox*>(tool_form->itemAt(4, QFormLayout::FieldRole)->widget());
    auto* tool_stepover_field = qobject_cast<QDoubleSpinBox*>(tool_form->itemAt(5, QFormLayout::FieldRole)->widget());
    auto* tool_feed_field = qobject_cast<QDoubleSpinBox*>(tool_form->itemAt(6, QFormLayout::FieldRole)->widget());
    auto* tool_plunge_field = qobject_cast<QDoubleSpinBox*>(tool_form->itemAt(7, QFormLayout::FieldRole)->widget());
    auto* tool_rpm_field = qobject_cast<QSpinBox*>(tool_form->itemAt(8, QFormLayout::FieldRole)->widget());
    auto* tool_number_field = qobject_cast<QSpinBox*>(tool_form->itemAt(9, QFormLayout::FieldRole)->widget());

#endif
    auto choose_tool_from_database = [&]() {
        cam::ToolDatabase database = cam::ToolDatabase::load();
        ui::ResponsiveDialog chooser(&dialog);
        chooser.setWindowTitle("Selecionar ferramenta");
        chooser.setMinimumSize(680, 440);
        chooser.resize(780, 500);
        auto* chooser_layout = new QVBoxLayout(&chooser);
        chooser_layout->setContentsMargins(14, 14, 14, 12);
        auto* chooser_splitter = new QSplitter(Qt::Horizontal, &chooser);
        auto* tool_list = new QListWidget(chooser_splitter);
        tool_list->setObjectName("ToolpathList");
        auto* tool_info = new QLabel(chooser_splitter);
        tool_info->setObjectName("PropHint");
        tool_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        tool_info->setWordWrap(true);
        chooser_splitter->addWidget(tool_list);
        chooser_splitter->addWidget(tool_info);
        chooser_splitter->setStretchFactor(0, 1);
        chooser_splitter->setStretchFactor(1, 1);
        chooser_layout->addWidget(chooser_splitter, 1);

        auto refresh_tool_list = [&] {
            QSignalBlocker blocker(tool_list);
            tool_list->clear();
            int selected_row = 0;
            for (size_t i = 0; i < database.tools.size(); ++i) {
                const auto& tool = database.tools[i];
                auto* item = new QListWidgetItem(QString::fromStdString(tool->name), tool_list);
                item->setIcon(ui::get_icon(tool->tool_type == cam::ToolType::Drill
                                               ? "tp_drill" : "tp_tools"));
                item->setToolTip(QString::fromStdString(tool->describe()));
                if (tool->name == job.tool.name) {
                    selected_row = static_cast<int>(i);
                }
            }
            if (!database.tools.empty()) tool_list->setCurrentRow(selected_row);
        };
        auto update_tool_info = [&] {
            const int row = tool_list->currentRow();
            if (row < 0 || row >= static_cast<int>(database.tools.size())) {
                tool_info->clear();
                return;
            }
            const cam::Tool& selected = *database.tools[static_cast<size_t>(row)];
            tool_info->setText(
                QString("<h3>%1</h3>Tipo: %2<br>Diametro: %3 mm<br>Passo Z: %4 mm<br>"
                        "Passo lateral: %5 mm<br>Avanco XY: %6 mm/min<br>Rotacao: %7 RPM")
                    .arg(QString::fromStdString(selected.name))
                    .arg(QString::fromUtf8(cam::tool_type_label(selected.tool_type)))
                    .arg(selected.diameter, 0, 'f', 3)
                    .arg(selected.pass_depth, 0, 'f', 3)
                    .arg(selected.stepover, 0, 'f', 3)
                    .arg(selected.feed_rate, 0, 'f', 1)
                    .arg(selected.spindle_speed));
        };
        refresh_tool_list();
        update_tool_info();
        connect(tool_list, &QListWidget::currentRowChanged, &chooser,
                [&](int) { update_tool_info(); });
        connect(tool_list, &QListWidget::itemDoubleClicked, &chooser,
                [&](QListWidgetItem*) { if (tool_list->currentRow() >= 0) chooser.accept(); });

        auto* chooser_buttons = new QHBoxLayout();
        auto* edit_database_button = new QPushButton("Editar banco...", &chooser);
        chooser_buttons->addWidget(edit_database_button);
        chooser_buttons->addStretch();
        auto* chooser_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &chooser);
        chooser_box->button(QDialogButtonBox::Ok)->setText("Selecionar ferramenta");
        chooser_box->button(QDialogButtonBox::Cancel)->setText("Cancelar");
        chooser_buttons->addWidget(chooser_box);
        chooser_layout->addLayout(chooser_buttons);

        connect(edit_database_button, &QPushButton::clicked, &chooser, [&] {
            const int cur = tool_list->currentRow();
            const std::string cur_name = (cur >= 0 && cur < static_cast<int>(database.tools.size()))
                ? database.tools[static_cast<size_t>(cur)]->name : job.tool.name;
            edit_tool_database(cur_name);
            database = cam::ToolDatabase::load();
            refresh_tool_list();
            update_tool_info();
        });
        connect(chooser_box, &QDialogButtonBox::accepted, &chooser, [&] {
            if (tool_list->currentRow() >= 0) chooser.accept();
        });
        connect(chooser_box, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);

        if (chooser.exec() != QDialog::Accepted) return false;
        const int row = tool_list->currentRow();
        if (row < 0 || row >= static_cast<int>(database.tools.size())) return false;
        job.tool = *database.tools[static_cast<size_t>(row)];
        return true;
    };

    // Nao ha campos duplicados de ferramenta neste dialogo.
#if 0
    auto apply_selected_tool_to_form = [&] {
        QSignalBlocker type_blocker(tool_type_field);
        tool_type_field->setCurrentIndex(
            std::max(0, tool_type_field->findData(static_cast<int>(job.tool.tool_type))));
        tool_diameter_field->setValue(job.tool.diameter);
        tool_pass_depth_field->setValue(job.tool.pass_depth);
        tool_stepover_field->setValue(job.tool.stepover);
        tool_feed_field->setValue(job.tool.feed_rate);
        tool_plunge_field->setValue(job.tool.plunge_rate);
        tool_rpm_field->setValue(job.tool.spindle_speed);
        tool_number_field->setValue(job.tool.tool_number);
        selected_tool_name->setText(QString::fromStdString(job.tool.name));
    };
#endif
    auto sync_tool_to_dialog = [&] {
        selected_tool_name->setText(QString::fromStdString(job.tool.name));
        selected_tool_name->setToolTip(QString::fromStdString(job.tool.describe()));
        edit_tools_button->setToolTip(
            QString("Editar parâmetros da fresa selecionada (%1)").arg(QString::fromStdString(job.tool.name)));
        guide_model->setValue("diameter", job.tool.diameter);
        guide_model->setValue("pass", job.tool.pass_depth);
        guide_model->setValue("toolStepover", job.tool.stepover);
        guide_model->setValue("toolType", static_cast<int>(job.tool.tool_type));
        if (refresh_pass_summary) refresh_pass_summary();
    };
    sync_tool_to_dialog();

    connect(select_tool_button, &QPushButton::clicked, &dialog, [&] {
        if (choose_tool_from_database()) {
            sync_tool_to_dialog();
        }
    });
    connect(edit_tools_button, &QPushButton::clicked, &dialog, [&] {
        if (edit_tool_database(job.tool.name, &job.tool)) {
            sync_tool_to_dialog();
        }
    });

    const std::vector<std::pair<QString, int>> directions = {
        {"Convencional", static_cast<int>(cam::CutDirection::Conventional)},
        {"Concordante", static_cast<int>(cam::CutDirection::Climb)},
    };
    const std::vector<std::pair<QString, int>> orders = {
        {"Mais curto", static_cast<int>(cam::MachineOrder::Shortest)},
        {"Ordem da seleção", static_cast<int>(cam::MachineOrder::Selection)},
        {"Esquerda para direita", static_cast<int>(cam::MachineOrder::LeftRight)},
        {"Baixo para cima", static_cast<int>(cam::MachineOrder::BottomTop)},
        {"Grade", static_cast<int>(cam::MachineOrder::Grid)},
    };
    const std::vector<std::pair<QString, int>> ramps = {
        {"Nenhuma", static_cast<int>(cam::RampType::None)},
        {"Suave", static_cast<int>(cam::RampType::Smooth)},
        {"Zigue-zague", static_cast<int>(cam::RampType::Zigzag)},
        {"Espiral", static_cast<int>(cam::RampType::Spiral)},
    };

    auto add_pass_summary = [&](QDoubleSpinBox* depth, QCheckBox* exact_step = nullptr) {
        auto* box = new QGroupBox("Passagens", body);
        auto* layout = new QHBoxLayout(box);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(8);
        auto* label = new QLabel(box);
        label->setObjectName("PropHint");
        label->setWordWrap(true);
        layout->addWidget(label, 1);
        auto* edit_passes_button = new QPushButton("Editar passagens...", box);
        edit_passes_button->setIcon(ui::get_icon("tp_edit"));
        edit_passes_button->setToolTip("Alterar o número de passagens ou o passo Z para este percurso");
        layout->addWidget(edit_passes_button);
        body_layout->addWidget(box);
        auto refresh = [depth, label, &job, exact_step] {
            const auto passes = cam::compute_passes(
                depth->value(), job.tool.pass_depth, exact_step && exact_step->isChecked());
            if (passes.empty()) {
                label->setText("Informe uma profundidade e uma ferramenta com passo Z valido.");
                return;
            }
            const double average = depth->value() / static_cast<double>(passes.size());
            label->setText(QString("%1 passada(s) estimada(s) · %2 mm por nivel · passo Z da fresa: %3 mm")
                               .arg(passes.size()).arg(average, 0, 'f', 2)
                               .arg(job.tool.pass_depth, 0, 'f', 2));
        };
        connect(depth, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                [refresh](double) { refresh(); });
        if (exact_step)
            connect(exact_step, &QCheckBox::toggled, &dialog, [refresh](bool) { refresh(); });

        connect(edit_passes_button, &QPushButton::clicked, &dialog, [depth, &job, exact_step, refresh, guide_model, &dialog] {
            ui::ResponsiveDialog pass_dlg(&dialog);
            pass_dlg.setWindowTitle("Editar passagens do percurso");
            pass_dlg.setMinimumWidth(320);
            auto* pass_layout = new QVBoxLayout(&pass_dlg);
            pass_layout->setContentsMargins(14, 14, 14, 14);
            pass_layout->setSpacing(10);

            auto* pass_form = new QFormLayout();
            pass_form->setSpacing(8);

            auto* total_depth_lbl = new QLabel(QString("%1 mm").arg(depth->value(), 0, 'f', 3), &pass_dlg);
            total_depth_lbl->setStyleSheet("font-weight: bold; color: #d9ecff;");
            pass_form->addRow("Profundidade total (C):", total_depth_lbl);

            auto* step_input = new ui::SafeDoubleSpinBox(&pass_dlg);
            step_input->setRange(0.001, 1000.0);
            step_input->setDecimals(3);
            step_input->setSuffix(" mm");
            step_input->setValue(job.tool.pass_depth);
            pass_form->addRow("Passo Z por nível:", step_input);

            const auto current_passes = cam::compute_passes(
                depth->value(), job.tool.pass_depth, exact_step && exact_step->isChecked());
            auto* count_input = new ui::SafeSpinBox(&pass_dlg);
            count_input->setRange(1, 200);
            count_input->setValue(std::max(1, static_cast<int>(current_passes.size())));
            pass_form->addRow("Número de passadas:", count_input);

            pass_layout->addLayout(pass_form);

            auto updating = std::make_shared<bool>(false);
            connect(count_input, qOverload<int>(&QSpinBox::valueChanged), &pass_dlg, [count_input, step_input, depth, updating](int count) {
                if (*updating || count <= 0) return;
                *updating = true;
                step_input->setValue(depth->value() / static_cast<double>(count));
                *updating = false;
            });
            connect(step_input, qOverload<double>(&QDoubleSpinBox::valueChanged), &pass_dlg, [count_input, step_input, depth, exact_step, updating](double step) {
                if (*updating || step <= 0.0) return;
                *updating = true;
                const auto passes = cam::compute_passes(depth->value(), step, exact_step && exact_step->isChecked());
                if (!passes.empty()) count_input->setValue(static_cast<int>(passes.size()));
                *updating = false;
            });

            auto* pass_btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &pass_dlg);
            pass_btn_box->button(QDialogButtonBox::Ok)->setText("Aplicar");
            pass_btn_box->button(QDialogButtonBox::Cancel)->setText("Cancelar");
            pass_layout->addWidget(pass_btn_box);
            connect(pass_btn_box, &QDialogButtonBox::accepted, &pass_dlg, &QDialog::accept);
            connect(pass_btn_box, &QDialogButtonBox::rejected, &pass_dlg, &QDialog::reject);

            if (pass_dlg.exec() == QDialog::Accepted) {
                job.tool.pass_depth = step_input->value();
                guide_model->setValue("pass", job.tool.pass_depth);
                refresh();
            }
        });

        refresh_pass_summary = refresh;
        refresh();
    };

    bool request_manual_bridge_mode = false;
    bool clear_manuals_on_accept = false;
    bool undo_last_manual_on_accept = false;
    std::visit([&](auto& settings) {
        using S = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<S, cam::ProfileSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Cota inicial (D)", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            auto* profile_depth = add_double(depth_form, "Profundidade (C)", settings.cut_depth,
                                             0, 1000, " mm",
                                             [&settings](double v) { settings.cut_depth = v; });
            auto* exact_step = add_bool(depth_form, "Manter passo Z exato", settings.maintain_exact_step,
                                       [&settings](bool v) { settings.maintain_exact_step = v; });
            auto* advanced_toggle = new QCheckBox("Mostrar opções avançadas", body);
            depth_form->addRow(QString(), advanced_toggle);
            body_layout->addWidget(tool_group_box);
            add_pass_summary(profile_depth, exact_step);

            auto* form = add_group("Usinar vetores...",
                                   {{"Posição da fresa", "01_usinar_vetores"}});
            auto* profile_side = add_enum(form, "Posição da fresa", static_cast<int>(settings.side),
                     {{"Fora", static_cast<int>(cam::ProfileSide::Outside)},
                      {"Dentro", static_cast<int>(cam::ProfileSide::Inside)},
                      {"Sobre a linha", static_cast<int>(cam::ProfileSide::On)}},
                     [&settings](int v) { settings.side = static_cast<cam::ProfileSide>(v); });
            connect(profile_side, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("01_usinar_vetores", value); });
            if (auto* guide = guide_for("01_usinar_vetores")) {
                guide->mode_clicked = [profile_side](int value) { profile_side->setCurrentIndex(value); };
            }
            auto* open_side = add_enum(form, "Vetor aberto", static_cast<int>(settings.open_side),
                     {{"Sobre a linha", static_cast<int>(cam::OpenSide::On)},
                      {"Esquerda", static_cast<int>(cam::OpenSide::Left)},
                      {"Direita", static_cast<int>(cam::OpenSide::Right)}},
                     [&settings](int v) { settings.open_side = static_cast<cam::OpenSide>(v); });
            add_enum(form, "Direção", static_cast<int>(settings.direction), directions,
                     [&settings](int v) { settings.direction = static_cast<cam::CutDirection>(v); });
            auto* allowance = add_double(form, "Sobremetal", settings.allowance, -1000, 1000, " mm",
                                         [&settings](double v) { settings.allowance = v; });
            auto* tolerance = add_double(form, "Tolerância", settings.tolerance, 0, 5, " mm",
                                         [&settings](double v) { settings.tolerance = v; });
            auto* use_start_points = add_bool(form, "Usar pontos iniciais (não otimizar)",
                                              settings.use_start_points,
                                              [&settings](bool v) { settings.use_start_points = v; });
            auto advanced_fields = std::make_shared<std::vector<std::pair<QWidget*, QWidget*>>>();
            const auto register_advanced_field = [advanced_fields, advanced_toggle](
                QFormLayout* owner, QWidget* field) {
                QWidget* label = owner->labelForField(field);
                advanced_fields->push_back({label, field});
                const bool visible = advanced_toggle->isChecked();
                if (label) label->setVisible(visible);
                field->setVisible(visible);
            };
            advanced_fields->push_back({form->labelForField(open_side), open_side});
            advanced_fields->push_back({form->labelForField(allowance), allowance});
            advanced_fields->push_back({form->labelForField(tolerance), tolerance});
            advanced_fields->push_back({form->labelForField(use_start_points), use_start_points});
            for (const auto& [label, field] : *advanced_fields) {
                if (label) label->setVisible(false);
                field->setVisible(false);
            }
            connect(advanced_toggle, &QCheckBox::toggled, &dialog,
                    [advanced_fields](bool visible) {
                for (const auto& [label, field] : *advanced_fields) {
                    if (label) label->setVisible(visible);
                    field->setVisible(visible);
                }
            });

            form = add_group("Último passe", {{"Profundidades", "02_profundidades"}});
            auto* separate_last = add_bool(form, "Passe final separado",
                                           settings.separate_last_pass,
                                           [&settings](bool v) { settings.separate_last_pass = v; });
            auto* last_allowance = add_double(form, "Material deixado",
                                              settings.last_pass_allowance, 0, 1000, " mm",
                                              [&settings](double v) { settings.last_pass_allowance = v; });
            auto* reverse_last = add_bool(form, "Inverter direção", settings.reverse_last_pass,
                                          [&settings](bool v) { settings.reverse_last_pass = v; });
            last_allowance->setEnabled(settings.separate_last_pass);
            reverse_last->setEnabled(settings.separate_last_pass);
            connect(separate_last, &QCheckBox::toggled, &dialog,
                    [last_allowance, reverse_last](bool enabled) {
                last_allowance->setEnabled(enabled);
                reverse_last->setEnabled(enabled);
            });
            auto* profile_tabs = new QTabBar(body);
            profile_tabs->setObjectName("ProfileOptionTabs");
            profile_tabs->setExpanding(true);
            profile_tabs->setDrawBase(false);
            profile_tabs->setStyleSheet(
                "QTabBar::tab { background:#182536; color:#a9bfd6; border:1px solid #304861; "
                "border-bottom:0; border-top-left-radius:6px; border-top-right-radius:6px; "
                "padding:7px 10px; min-height:20px; }"
                "QTabBar::tab:selected { background:#1d4567; color:#f3f8ff; border-color:#168be0; }"
                "QTabBar::tab:hover:!selected { background:#24384d; color:#ffffff; }");
            body_layout->addWidget(profile_tabs);
            std::array<QGroupBox*, 4> profile_pages{};
            int profile_page_count = 0;
            const auto add_profile_tab = [&](const QString& tab, const QString& title,
                                             const QString& guide_title, const QString& guide_kind) {
                auto* page_form = add_group(title, {{guide_title, guide_kind}}, false);
                QGroupBox* page = nullptr;
                for (QWidget* parent = page_form->parentWidget(); parent; parent = parent->parentWidget()) {
                    page = qobject_cast<QGroupBox*>(parent);
                    if (page) break;
                }
                if (page) {
                    page->setTitle({});
                    page->setStyleSheet("QGroupBox#CamConfigGroup { margin-top:0; padding-top:0; "
                                        "border-top-left-radius:0; border-top-right-radius:0; }");
                    profile_pages[profile_page_count] = page;
                    if (profile_page_count > 0) page->hide();
                }
                ++profile_page_count;
                profile_tabs->addTab(tab);
                return page_form;
            };
            form = add_profile_tab("Pontes", "Pontes", "Pontes de fixação", "03_pontes");
            auto* bridge_enabled = add_bool(form, "Ativar pontes de fixação da peça", settings.tabs,
                                            [&settings](bool v) { settings.tabs = v; });
            auto* bridge_length = add_double(form, "Comprimento da ponte", settings.tab_length, 0, 1000, " mm",
                                             [&settings](double v) { settings.tab_length = v; });
            auto* bridge_thickness = add_double(form, "Espessura da ponte", settings.tab_thickness, 0, 1000, " mm",
                                                [&settings](double v) { settings.tab_thickness = v; });
            auto* bridges_3d = add_bool(form, "Pontes 3D com subida em rampa", settings.tabs_3d,
                                        [&settings](bool v) { settings.tabs_3d = v; });
            auto* bridge_distribution = new ui::SafeComboBox(body);
            bridge_distribution->addItem("Quantidade fixa por contorno", 0);
            bridge_distribution->addItem("Uma ponte a cada espacamento", 1);
            bridge_distribution->setCurrentIndex(settings.tab_spacing > 0.0 ? 1 : 0);
            form->addRow("Distribuicao", bridge_distribution);
            auto* bridge_count = add_int(form, "Quantidade de pontes", settings.tab_count, 1, 200,
                                         [&settings](int v) { settings.tab_count = v; });
            auto* bridge_spacing = add_double(form, "Espacamento entre pontes", settings.tab_spacing,
                                              0.5, 5000, " mm",
                                              [&settings](double v) { settings.tab_spacing = v; });
            apply.push_back([&settings, bridge_distribution, bridge_spacing] {
                settings.tab_spacing = bridge_distribution->currentData().toInt() == 0
                                           ? 0.0
                                           : std::max(0.5, bridge_spacing->value());
            });
            auto* bridge_min = add_int(form, "Minimo de pontes", settings.tab_min_count, 1, 200,
                                       [&settings](int v) { settings.tab_min_count = v; });
            auto* bridge_max = add_int(form, "Maximo de pontes", settings.tab_max_count, 1, 400,
                                       [&settings](int v) { settings.tab_max_count = v; });
            auto* first_bridge_at_start = add_bool(
                form, "Primeira ponte no inicio do vetor", settings.tab_first_at_start,
                [&settings](bool v) { settings.tab_first_at_start = v; });
            auto* manual_only = add_bool(form, "Usar somente pontes posicionadas manualmente",
                                         settings.tab_manual_only,
                                         [&settings](bool v) { settings.tab_manual_only = v; });

            auto* bridge_actions = new QWidget(body);
            bridge_actions->setObjectName("ProfileBridgeActions");
            bridge_actions->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            bridge_actions->setStyleSheet(
                "QWidget#ProfileBridgeActions QPushButton { "
                "  background-color: #203144; color: #d7e4f2; border: 1px solid #36506d; "
                "  border-radius: 4px; min-height: 28px; padding: 4px 6px; font-size: 11px; "
                "} "
                "QWidget#ProfileBridgeActions QPushButton:hover { "
                "  background-color: #2a415a; color: #ffffff; border-color: #4b6e96; "
                "} "
                "QWidget#ProfileBridgeActions QPushButton:disabled { "
                "  background-color: #162230; color: #556b82; border-color: #243547; "
                "} "
                "QWidget#ProfileBridgeActions QPushButton[primary=\"true\"] { "
                "  background-color: #1976d2; color: #ffffff; border: 1px solid #42a5f5; "
                "  border-radius: 4px; min-height: 32px; padding: 6px 12px; font-size: 12px; font-weight: bold; "
                "} "
                "QWidget#ProfileBridgeActions QPushButton[primary=\"true\"]:hover { "
                "  background-color: #1e88e5; border-color: #64b5f6; "
                "} "
                "QWidget#ProfileBridgeActions QPushButton[primary=\"true\"]:pressed { "
                "  background-color: #1565c0; "
                "}");
            auto* bridge_actions_layout = new QGridLayout(bridge_actions);
            bridge_actions_layout->setContentsMargins(0, 4, 0, 4);
            bridge_actions_layout->setSpacing(6);

            auto* mark_bridges = new QPushButton("Marcar no desenho", bridge_actions);
            mark_bridges->setProperty("primary", true);
            mark_bridges->setIcon(ui::get_icon("tp_edit"));
            mark_bridges->setMinimumHeight(32);
            mark_bridges->setCursor(Qt::PointingHandCursor);
            mark_bridges->setToolTip("Ativar modo interativo para adicionar, remover e mover pontes diretamente no desenho");
            mark_bridges->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            auto* undo_manual_bridge = new QPushButton("Desfazer", bridge_actions);
            undo_manual_bridge->setMinimumHeight(28);
            undo_manual_bridge->setToolTip("Desfazer última ponte manual inserida");
            undo_manual_bridge->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            auto* clear_manual_bridges = new QPushButton("Limpar", bridge_actions);
            clear_manual_bridges->setMinimumHeight(28);
            clear_manual_bridges->setToolTip("Limpar todas as pontes manuais");
            clear_manual_bridges->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            auto* restore_auto_bridges = new QPushButton("Redistribuir", bridge_actions);
            restore_auto_bridges->setMinimumHeight(28);
            restore_auto_bridges->setToolTip("Restaurar distribuição automática de pontes");
            restore_auto_bridges->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            bridge_actions_layout->addWidget(mark_bridges, 0, 0, 1, 3);
            bridge_actions_layout->addWidget(undo_manual_bridge, 1, 0);
            bridge_actions_layout->addWidget(clear_manual_bridges, 1, 1);
            bridge_actions_layout->addWidget(restore_auto_bridges, 1, 2);
            // Reserve enough vertical space for both action rows. The old fixed
            // 70 px minimum was smaller than the styled buttons plus spacing,
            // so Qt laid the lower row over the primary button on compact docks.
            bridge_actions_layout->setColumnStretch(0, 1);
            bridge_actions_layout->setColumnStretch(1, 1);
            bridge_actions_layout->setColumnStretch(2, 1);
            bridge_actions_layout->activate();
            bridge_actions->setMinimumHeight(
                std::max(bridge_actions_layout->sizeHint().height(), 92));
            form->addRow(bridge_actions);

            auto* bridge_help = new QLabel(
                "Use Marcar no desenho para adicionar pontes. Arraste os marcadores para ajustar; "
                "pressione Esc para encerrar a marcacao.", body);
            bridge_help->setWordWrap(true);
            bridge_help->setObjectName("MutedText");
            form->addRow(bridge_help);
            // These flags outlive std::visit; callbacks run during dialog.exec().
            apply.push_back([&settings, &clear_manuals_on_accept, &undo_last_manual_on_accept] {
                if (clear_manuals_on_accept) {
                    settings.manual_tabs.clear();
                    settings.tab_manual_only = false;
                } else if (undo_last_manual_on_accept && !settings.manual_tabs.empty()) {
                    settings.manual_tabs.pop_back();
                }
            });
            connect(mark_bridges, &QPushButton::clicked, &dialog, [&, bridge_enabled] {
                request_manual_bridge_mode = true;
                bridge_enabled->setChecked(true);
                dialog.accept();
            });
            connect(clear_manual_bridges, &QPushButton::clicked, &dialog,
                    [&, manual_only, clear_manual_bridges, restore_auto_bridges, undo_manual_bridge] {
                clear_manuals_on_accept = true;
                undo_last_manual_on_accept = false;
                manual_only->setChecked(false);
                undo_manual_bridge->setEnabled(false);
                clear_manual_bridges->setEnabled(false);
                restore_auto_bridges->setEnabled(false);
            });
            connect(restore_auto_bridges, &QPushButton::clicked, &dialog,
                    [&, manual_only, undo_manual_bridge] {
                clear_manuals_on_accept = true;
                undo_last_manual_on_accept = false;
                manual_only->setChecked(false);
                undo_manual_bridge->setEnabled(false);
            });
            connect(undo_manual_bridge, &QPushButton::clicked, &dialog,
                    [&, undo_manual_bridge] {
                clear_manuals_on_accept = false;
                undo_last_manual_on_accept = true;
                undo_manual_bridge->setEnabled(false);
            });
            auto update_bridge_controls = [=, &settings, &clear_manuals_on_accept,
                                           &undo_last_manual_on_accept] {
                const bool enabled = bridge_enabled->isChecked();
                const bool spacing_mode = bridge_distribution->currentData().toInt() == 1;
                bridge_count->setEnabled(enabled && !spacing_mode);
                bridge_spacing->setEnabled(enabled && spacing_mode);
                bridge_min->setEnabled(enabled && spacing_mode);
                bridge_max->setEnabled(enabled && spacing_mode);
                first_bridge_at_start->setEnabled(enabled && !spacing_mode);
                bridges_3d->setEnabled(enabled);
                manual_only->setEnabled(enabled);
                // Clicking this action also enables bridges, so it must be
                // available when the checkbox is initially off.
                mark_bridges->setEnabled(true);
                undo_manual_bridge->setEnabled(!clear_manuals_on_accept &&
                                               !undo_last_manual_on_accept &&
                                               !settings.manual_tabs.empty());
                clear_manual_bridges->setEnabled(!clear_manuals_on_accept && !settings.manual_tabs.empty());
                restore_auto_bridges->setEnabled(manual_only->isChecked() ||
                                                 (!clear_manuals_on_accept && !settings.manual_tabs.empty()));
                const int automatic_count = enabled && !manual_only->isChecked()
                    ? (spacing_mode ? bridge_min->value() : bridge_count->value()) : 0;
                const int manual_count = enabled && !clear_manuals_on_accept
                    ? std::max(0, static_cast<int>(settings.manual_tabs.size()) - (undo_last_manual_on_accept ? 1 : 0)) : 0;
                guide_model->setValue("tabCount", automatic_count + manual_count);
                guide_model->setValue("tabLength", bridge_length->value());
                guide_model->setValue("tabHeight", bridge_thickness->value());
            };
            connect(bridge_enabled, &QCheckBox::toggled, &dialog, update_bridge_controls);
            connect(bridge_distribution, qOverload<int>(&QComboBox::currentIndexChanged),
                    &dialog, update_bridge_controls);
            connect(bridge_count, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridge_min, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridge_max, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridge_spacing, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridge_length, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridge_thickness, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                    update_bridge_controls);
            connect(bridges_3d, &QCheckBox::toggled, &dialog, update_bridge_controls);
            connect(bridges_3d, &QCheckBox::toggled, &dialog,
                    [update_guide_mode](bool value) { update_guide_mode("03_pontes", value ? 1 : 0); });
            if (auto* guide = guide_for("03_pontes")) {
                guide->mode_clicked = [bridges_3d](int value) { bridges_3d->setChecked(value != 0); };
            }
            connect(manual_only, &QCheckBox::toggled, &dialog, update_bridge_controls);
            connect(clear_manual_bridges, &QPushButton::clicked, &dialog, update_bridge_controls);
            connect(restore_auto_bridges, &QPushButton::clicked, &dialog, update_bridge_controls);
            connect(undo_manual_bridge, &QPushButton::clicked, &dialog, update_bridge_controls);
            update_bridge_controls();

            form = add_profile_tab("Entradas", "Entradas e saídas", "Entradas e saídas", "04_entradas_saidas");
            auto* lead_type = add_enum(form, "Tipo de entrada", static_cast<int>(settings.lead),
                     {{"Nenhuma", static_cast<int>(cam::LeadType::None)},
                      {"Linear", static_cast<int>(cam::LeadType::Linear)},
                      {"Em arco", static_cast<int>(cam::LeadType::Arc)}},
                     [&settings](int v) { settings.lead = static_cast<cam::LeadType>(v); });
            connect(lead_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("04_entradas_saidas", value); });
            if (auto* guide = guide_for("04_entradas_saidas")) {
                guide->mode_clicked = [lead_type](int value) { lead_type->setCurrentIndex(value); };
            }
            auto* lead_length = add_double(form, "Comprimento", settings.lead_length, 0, 1000, " mm",
                                           [&settings](double v) { settings.lead_length = v; });
            auto* lead_radius = add_double(form, "Raio do arco", settings.lead_radius, 0, 1000, " mm",
                                           [&settings](double v) { settings.lead_radius = v; });
            auto* lead_angle = add_double(form, "Ângulo", settings.lead_angle, -180, 180, " °",
                                          [&settings](double v) { settings.lead_angle = v; });
            add_bool(form, "Criar saída", settings.lead_out,
                     [&settings](bool v) { settings.lead_out = v; });
            auto* overcut = add_double(form, "Sobrecurso em cantos", settings.overcut, 0, 1000, " mm",
                                       [&settings](double v) { settings.overcut = v; });
            for (QWidget* field : {static_cast<QWidget*>(lead_length), static_cast<QWidget*>(lead_radius),
                                   static_cast<QWidget*>(lead_angle), static_cast<QWidget*>(overcut)})
                register_advanced_field(form, field);
            auto update_lead_fields = [lead_type, lead_length, lead_radius, lead_angle, overcut] {
                const int type = lead_type->currentData().toInt();
                lead_length->setEnabled(type != static_cast<int>(cam::LeadType::None));
                lead_radius->setEnabled(type == static_cast<int>(cam::LeadType::Arc));
                lead_angle->setEnabled(type != static_cast<int>(cam::LeadType::None));
                overcut->setEnabled(type != static_cast<int>(cam::LeadType::None));
            };
            connect(lead_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_lead_fields](int) { update_lead_fields(); });
            update_lead_fields();

            form = add_profile_tab("Rampas", "Rampas", "Rampas", "05_rampas");
            auto* ramp_type = add_enum(form, "Tipo de rampa", static_cast<int>(settings.ramp), ramps,
                     [&settings](int v) { settings.ramp = static_cast<cam::RampType>(v); });
            connect(ramp_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("05_rampas", value); });
            if (auto* guide = guide_for("05_rampas")) {
                guide->mode_clicked = [ramp_type](int value) { ramp_type->setCurrentIndex(value); };
            }
            if (settings.ramp_distance <= 0.0) settings.ramp_distance = 50.0;
            auto* ramp_distance = add_double(form, "Distância da rampa (L)", settings.ramp_distance, 0, 10000, " mm",
                                             [&settings](double v) { settings.ramp_distance = v; });
            auto* ramp_angle = add_double(form, "Ângulo da rampa", settings.ramp_angle, 0, 90, " °",
                                          [&settings](double v) { settings.ramp_angle = v; });
            auto* ramp_on_lead = add_bool(form, "Aplicar rampa na entrada", settings.ramp_on_lead,
                                          [&settings](bool v) { settings.ramp_on_lead = v; });

            auto update_ramp_fields = [ramp_type, ramp_distance, ramp_angle, ramp_on_lead] {
                const bool has_ramp = ramp_type->currentData().toInt() != static_cast<int>(cam::RampType::None);
                ramp_distance->setEnabled(has_ramp);
                ramp_angle->setEnabled(has_ramp);
                ramp_on_lead->setEnabled(has_ramp);
            };
            connect(ramp_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_ramp_fields](int) { update_ramp_fields(); });
            update_ramp_fields();

            form = add_profile_tab("Ordem", "Ordem", "Ordem de usinagem", "01_usinar_vetores");
            add_enum(form, "Ordenar por", static_cast<int>(settings.order), orders,
                     [&settings](int v) { settings.order = static_cast<cam::MachineOrder>(v); });
            add_bool(form, "Separar interiores", settings.separate_interior,
                     [&settings](bool v) { settings.separate_interior = v; });
            connect(profile_tabs, &QTabBar::currentChanged, &dialog, [profile_pages](int index) {
                for (size_t i = 0; i < profile_pages.size(); ++i)
                    if (profile_pages[i]) profile_pages[i]->setVisible(static_cast<int>(i) == index);
            });
        } else if constexpr (std::is_same_v<S, cam::PocketSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Cota inicial (D)", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            auto* pocket_depth = add_double(depth_form, "Profundidade (C)", settings.cut_depth,
                                            0, 1000, " mm",
                                            [&settings](double v) { settings.cut_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(pocket_depth);
            auto* form = add_group("Bolso", guide_topics);
            auto* fill_type = add_enum(form, "Preenchimento", static_cast<int>(settings.fill),
                     {{"Offset", static_cast<int>(cam::FillType::Offset)},
                      {"Raster", static_cast<int>(cam::FillType::Raster)}},
                     [&settings](int v) { settings.fill = static_cast<cam::FillType>(v); });
            connect(fill_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("06_limpar_area", value); });
            if (auto* guide = guide_for("06_limpar_area")) {
                guide->mode_clicked = [fill_type](int value) { fill_type->setCurrentIndex(value); };
            }
            add_double(form, "Ângulo do raster", settings.raster_angle, -360, 360, " °",
                       [&settings](double v) { settings.raster_angle = v; });
            add_enum(form, "Direção", static_cast<int>(settings.direction), directions,
                     [&settings](int v) { settings.direction = static_cast<cam::CutDirection>(v); });
            add_double(form, "Passo lateral", settings.stepover, 0, 1000, " mm",
                       [&settings](double v) { settings.stepover = v; });
            add_enum(form, "Passada de perfil", static_cast<int>(settings.profile_pass),
                     {{"Primeira", static_cast<int>(cam::ProfilePass::First)},
                      {"Última", static_cast<int>(cam::ProfilePass::Last)},
                      {"Nenhuma", static_cast<int>(cam::ProfilePass::None)}},
                     [&settings](int v) { settings.profile_pass = static_cast<cam::ProfilePass>(v); });
            add_double(form, "Sobremetal", settings.allowance, -1000, 1000, " mm",
                       [&settings](double v) { settings.allowance = v; });
            auto* pocket_ramp = add_enum(form, "Rampa", static_cast<int>(settings.ramp), ramps,
                     [&settings](int v) { settings.ramp = static_cast<cam::RampType>(v); });
            connect(pocket_ramp, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("07_ataque_rampeado", value == 0 ? 0 : 1); });
            if (auto* guide = guide_for("07_ataque_rampeado")) {
                guide->mode_clicked = [pocket_ramp](int value) {
                    pocket_ramp->setCurrentIndex(value == 0 ? 0 : 1);
                };
            }
            add_double(form, "Distância da rampa", settings.ramp_distance, 0, 10000, " mm",
                       [&settings](double v) { settings.ramp_distance = v; });
            add_bool(form, "Manter passo Z exato", settings.maintain_exact_step,
                     [&settings](bool v) { settings.maintain_exact_step = v; });
            add_enum(form, "Ordenar por", static_cast<int>(settings.order), orders,
                     [&settings](int v) { settings.order = static_cast<cam::MachineOrder>(v); });
        } else if constexpr (std::is_same_v<S, cam::DrillSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Cota inicial (D)", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            auto* drill_depth = add_double(depth_form, "Profundidade (C)", settings.cut_depth,
                                           0, 1000, " mm",
                                           [&settings](double v) { settings.cut_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(drill_depth);
            auto* form = add_group("Furação", guide_topics);
            add_bool(form, "Furação peck", settings.peck,
                     [&settings](bool v) { settings.peck = v; });
            add_double(form, "Recuo entre passadas", settings.retract_gap, 0, 1000, " mm",
                       [&settings](double v) { settings.retract_gap = v; });
            add_enum(form, "Modo de recuo", static_cast<int>(settings.retract_mode),
                     {{"Acima do início", static_cast<int>(cam::RetractMode::AboveStart)},
                      {"Acima da passada anterior", static_cast<int>(cam::RetractMode::AbovePrevious)}},
                     [&settings](int v) { settings.retract_mode = static_cast<cam::RetractMode>(v); });
            add_bool(form, "Pausa no fundo", settings.dwell,
                     [&settings](bool v) { settings.dwell = v; });
            add_double(form, "Tempo da pausa", settings.dwell_time, 0, 1000, " s",
                       [&settings](double v) { settings.dwell_time = v; });
            add_bool(form, "Helicoidal para furos maiores", settings.helical,
                     [&settings](bool v) { settings.helical = v; });
            add_enum(form, "Direção helicoidal", static_cast<int>(settings.helical_direction), directions,
                     [&settings](int v) { settings.helical_direction = static_cast<cam::CutDirection>(v); });
            add_enum(form, "Ordenar por", static_cast<int>(settings.order), orders,
                     [&settings](int v) { settings.order = static_cast<cam::MachineOrder>(v); });
        } else if constexpr (std::is_same_v<S, cam::VCarveSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Cota inicial (D)", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            add_double(depth_form, "Profundidade plana", settings.flat_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.flat_depth = v; });
            auto* vcarve_depth = add_double(depth_form, "Profundidade máxima", settings.max_depth,
                                            0, 1000, " mm",
                                            [&settings](double v) { settings.max_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(vcarve_depth);
            auto* form = add_group("Entalhe V", guide_topics);
            add_enum(form, "Modo", static_cast<int>(settings.mode),
                     {{"Automático", static_cast<int>(cam::VCarveMode::Automatic)},
                      {"Manual", static_cast<int>(cam::VCarveMode::Manual)}},
                     [&settings](int v) { settings.mode = static_cast<cam::VCarveMode>(v); });
            add_enum(form, "Material", static_cast<int>(settings.material_profile),
                     {{"MDF", static_cast<int>(cam::VCarveMaterial::Mdf)},
                      {"MDP", static_cast<int>(cam::VCarveMaterial::Mdp)},
                      {"Compensado", static_cast<int>(cam::VCarveMaterial::Plywood)},
                      {"PVC expandido", static_cast<int>(cam::VCarveMaterial::ExpandedPvc)},
                      {"Acrílico", static_cast<int>(cam::VCarveMaterial::Acrylic)},
                      {"Outro", static_cast<int>(cam::VCarveMaterial::Other)}},
                     [&settings](int v) { settings.material_profile = static_cast<cam::VCarveMaterial>(v); });
            add_enum(form, "Preenchimento", static_cast<int>(settings.clearance_fill),
                     {{"Offset", static_cast<int>(cam::FillType::Offset)},
                      {"Raster", static_cast<int>(cam::FillType::Raster)}},
                     [&settings](int v) { settings.clearance_fill = static_cast<cam::FillType>(v); });
            add_double(form, "Passo de desbaste", settings.clearance_stepover, 0, 1000, " mm",
                       [&settings](double v) { settings.clearance_stepover = v; });
            add_double(form, "Passo de acabamento", settings.finish_stepover, 0, 1000, " mm",
                       [&settings](double v) { settings.finish_stepover = v; });
            add_double(form, "Sobremetal de desbaste", settings.clearance_allowance, -1000, 1000, " mm",
                       [&settings](double v) { settings.clearance_allowance = v; });
            add_bool(form, "Ignorar regiões planas", settings.skip_flat_regions,
                     [&settings](bool v) { settings.skip_flat_regions = v; });
            add_enum(form, "Direção", static_cast<int>(settings.direction), directions,
                     [&settings](int v) { settings.direction = static_cast<cam::CutDirection>(v); });
            add_enum(form, "Rampa", static_cast<int>(settings.ramp), ramps,
                     [&settings](int v) { settings.ramp = static_cast<cam::RampType>(v); });
            add_double(form, "Distância da rampa", settings.ramp_distance, 0, 10000, " mm",
                       [&settings](double v) { settings.ramp_distance = v; });
            add_enum(form, "Ordenar por", static_cast<int>(settings.order), orders,
                     [&settings](int v) { settings.order = static_cast<cam::MachineOrder>(v); });
        } else if constexpr (std::is_same_v<S, cam::FlutingSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Cota inicial", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            auto* flute_depth = add_double(depth_form, "Profundidade do canal", settings.cut_depth,
                                           0, 1000, " mm",
                                           [&settings](double v) { settings.cut_depth = v; });
            add_double(depth_form, "Profundidade final", settings.end_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.end_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(flute_depth);
            auto* form = add_group("Canais", guide_topics);
            add_double(form, "Rampa de entrada", settings.ramp_in, 0, 10000, " mm",
                       [&settings](double v) { settings.ramp_in = v; });
            add_double(form, "Rampa de saída", settings.ramp_out, 0, 10000, " mm",
                       [&settings](double v) { settings.ramp_out = v; });
        } else if constexpr (std::is_same_v<S, cam::TextureSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            auto* texture_depth = add_double(depth_form, "Profundidade", settings.depth, 0, 1000, " mm",
                                             [&settings](double v) { settings.depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(texture_depth);
            auto* form = add_group("Textura", guide_topics);
            add_double(form, "Raio da ferramenta", settings.tool_radius, 0.01, 1000, " mm",
                       [&settings](double v) { settings.tool_radius = v; });
            add_double(form, "Comprimento", settings.length, 0.01, 10000, " mm",
                       [&settings](double v) { settings.length = v; });
            add_double(form, "Variação do comprimento", settings.length_var, 0, 100, " %",
                       [&settings](double v) { settings.length_var = v; });
            add_double(form, "Variação da profundidade", settings.depth_var, 0, 100, " %",
                       [&settings](double v) { settings.depth_var = v; });
            add_double(form, "Espaçamento", settings.stepover, 0.01, 10000, " mm",
                       [&settings](double v) { settings.stepover = v; });
            add_double(form, "Variação do espaçamento", settings.stepover_var, 0, 100, " %",
                       [&settings](double v) { settings.stepover_var = v; });
            add_double(form, "Sobreposição", settings.overlap, 0, 100, " %",
                       [&settings](double v) { settings.overlap = v; });
            add_double(form, "Variação da sobreposição", settings.overlap_var, 0, 100, " %",
                       [&settings](double v) { settings.overlap_var = v; });
            add_double(form, "Ângulo", settings.angle, -360, 360, " °",
                       [&settings](double v) { settings.angle = v; });
            add_double(form, "Variação do ângulo", settings.angle_var, 0, 360, " °",
                       [&settings](double v) { settings.angle_var = v; });
            add_int(form, "Semente aleatória", static_cast<int>(settings.seed), 0, 2147483647,
                    [&settings](int v) { settings.seed = v; });
        } else if constexpr (std::is_same_v<S, cam::KeyholeSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            add_double(depth_form, "Profundidade de entrada", settings.plunge_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.plunge_depth = v; });
            auto* keyhole_depth = add_double(depth_form, "Profundidade do rasgo", settings.slot_depth,
                                             0, 1000, " mm",
                       [&settings](double v) { settings.slot_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(keyhole_depth);
        } else if constexpr (std::is_same_v<S, cam::InlaySettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            auto* inlay_depth = add_double(depth_form, "Profundidade do bolso", settings.pocket_depth,
                                           0, 1000, " mm",
                       [&settings](double v) { settings.pocket_depth = v; });
            add_double(depth_form, "Profundidade do plug", settings.plug_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.plug_depth = v; });
            add_double(depth_form, "Cota inicial", settings.start_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.start_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(inlay_depth);
            auto* form = add_group("Ajuste do encaixe", guide_topics);
            add_double(form, "Sobra para cola", settings.glue_allowance, -1000, 1000, " mm",
                       [&settings](double v) { settings.glue_allowance = v; });
        } else if constexpr (std::is_same_v<S, cam::DragknifeSettings>) {
            auto* depth_form = add_group("Profundidades de corte",
                                         {{"Profundidades", "02_profundidades"}});
            auto* knife_depth = add_double(depth_form, "Profundidade de corte", settings.cut_depth,
                                           0, 1000, " mm",
                                           [&settings](double v) { settings.cut_depth = v; });
            body_layout->addWidget(tool_group_box);
            add_pass_summary(knife_depth);
            auto* form = add_group("Faca de arraste", guide_topics);
            add_double(form, "Offset da lâmina", settings.blade_offset, 0, 1000, " mm",
                       [&settings](double v) { settings.blade_offset = v; });
            add_double(form, "Ângulo de tolerância", settings.tolerance_angle, 0, 180, " °",
                       [&settings](double v) { settings.tolerance_angle = v; });
            add_double(form, "Profundidade de giro", settings.swivel_depth, 0, 1000, " mm",
                       [&settings](double v) { settings.swivel_depth = v; });
            add_int(form, "Passadas", settings.passes, 1, 100,
                    [&settings](int v) { settings.passes = v; });
        }
    }, job.settings);

    auto* identity = add_group("Percurso");
    auto* name = new QLineEdit(QString::fromStdString(job.name), body);
    name->setObjectName("ToolpathNameInput");
    identity->addRow("Nome", name);
    apply.push_back([&job, name] { job.name = name->text().toStdString(); });

    body_layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* calculate_button = buttons->button(QDialogButtonBox::Save);
    calculate_button->setText("Calcular");
    calculate_button->setIcon(ui::get_icon("tp_recalc"));
    calculate_button->setIconSize(QSize(18, 18));
    calculate_button->setMinimumWidth(132);
    calculate_button->setProperty("primary", true);
    calculate_button->setToolTip("Salvar as configurações e calcular este percurso");
    calculate_button->setEnabled(!job.entity_ids.empty());
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    if (const auto* profile = std::get_if<cam::ProfileSettings>(&job.settings)) {
        update_guide_mode("01_usinar_vetores", static_cast<int>(profile->side));
        update_guide_mode("03_pontes", profile->tabs_3d ? 1 : 0);
        update_guide_mode("04_entradas_saidas", static_cast<int>(profile->lead));
        update_guide_mode("05_rampas", static_cast<int>(profile->ramp));
    } else if (const auto* pocket = std::get_if<cam::PocketSettings>(&job.settings)) {
        update_guide_mode("06_limpar_area", static_cast<int>(pocket->fill));
        update_guide_mode("07_ataque_rampeado", pocket->ramp == cam::RampType::None ? 0 : 1);
    }
    auto* button_bar = new QWidget(&dialog);
    auto* button_layout = new QHBoxLayout(button_bar);
    button_layout->setContentsMargins(16, 8, 16, 12);
    button_layout->addWidget(buttons);
    root->addWidget(button_bar);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    for (const auto& change : apply) change();
    cam::set_settings_name(job.settings, job.name);
    job.results.clear();
    job.error.clear();
    destination = job;
    if (request_manual_bridge_mode) {
        if (auto* profile = std::get_if<cam::ProfileSettings>(&job.settings)) {
            profile->tabs = true;
            destination = job;
            bridge_target_job_id_ = job.id;
            canvas_widget_->canvas->manual_tab_mode = true;
            canvas_widget_->canvas->setCursor(Qt::CrossCursor);
            if (view_tabs_) view_tabs_->setCurrentIndex(0);
            if (bridge_mode_banner_) bridge_mode_banner_->setVisible(true);
        }
    }
    return true;
}

void MainWindow::add_toolpath_from_panel() {
    const auto selected = document_.selected_entities();
    std::vector<std::string> ids;
    for (const auto& entity : selected) ids.push_back(entity->id);

    const auto kind = toolpath_type_combo_
                          ? static_cast<cam::ToolpathType>(toolpath_type_combo_->currentData().toInt())
                          : toolpath_panel_kind_;
    auto job = std::make_shared<cam::ToolpathJob>();
    job->kind = kind;
    job->settings = cam::default_settings(kind);
    job->name = document_.unique_toolpath_name(QString::fromUtf8(cam::toolpath_label(kind)).toStdString());
    job->entity_ids = std::move(ids);
    const auto database = cam::ToolDatabase::load();
    std::vector<cam::Tool> tools;
    for (const auto& tool : database.tools)
        if (tool) tools.push_back(*tool);
    if (tools.empty()) tools = cam::default_tools();
    if (!tools.empty()) {
        job->tool = tools.front();
        if (kind == cam::ToolpathType::Drill) {
            for (const auto& tool : tools) {
                if (tool.tool_type == cam::ToolType::Drill) {
                    job->tool = tool;
                    break;
                }
            }
        }
    }
    if (!configure_toolpath(*job)) return;
    document_.add_toolpath(job);
    const auto errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    if (canvas_widget_ && canvas_widget_->canvas && canvas_widget_->canvas->manual_tab_mode) {
        if (view_tabs_) view_tabs_->setCurrentIndex(0);
        refresh_bridge_markers();
        if (bridge_mode_banner_) bridge_mode_banner_->setVisible(true);
        statusBar()->showMessage(
            "Modo de pontes ativo: clique no contorno para adicionar ou remover pontes; arraste para mover. Pressione Esc ou Concluir para finalizar.");
    } else if (errors.empty()) {
        statusBar()->showMessage(QString("Percurso criado: %1").arg(QString::fromStdString(job->name)));
        open_order_service_page();
    } else {
        statusBar()->showMessage(QString::fromStdString(errors.front()));
    }
}

void MainWindow::edit_current_toolpath() {
    if (!toolpath_list_) return;
    const int row = toolpath_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(document_.toolpaths.size())) return;
    auto& job = document_.toolpaths[static_cast<size_t>(row)];
    cam::ToolpathJob draft = *job;
    if (!configure_toolpath(draft)) return;
    *job = std::move(draft);
    document_.modified = true;
    const auto draft_errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    if (canvas_widget_ && canvas_widget_->canvas && canvas_widget_->canvas->manual_tab_mode) {
        if (view_tabs_) view_tabs_->setCurrentIndex(0);
        refresh_bridge_markers();
        if (bridge_mode_banner_) bridge_mode_banner_->setVisible(true);
        statusBar()->showMessage(
            "Modo de pontes ativo: clique no contorno para adicionar ou remover pontes; arraste para mover. Pressione Esc ou Concluir para finalizar.");
    } else if (draft_errors.empty()) {
        statusBar()->showMessage("Percurso atualizado");
    } else {
        statusBar()->showMessage(QString::fromStdString(draft_errors.front()));
    }
    return;

    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle(QString("Editar percurso — %1").arg(QString::fromStdString(job->name)));
    dialog.setMinimumWidth(380);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(QString::fromStdString(job->name), &dialog);
    auto* diameter = new QDoubleSpinBox(&dialog);
    diameter->setRange(0.01, 1000.0);
    diameter->setDecimals(3);
    diameter->setSuffix(" mm");
    diameter->setValue(job->tool.diameter);
    auto* pass_depth = new QDoubleSpinBox(&dialog);
    pass_depth->setRange(0.001, 1000.0);
    pass_depth->setDecimals(3);
    pass_depth->setSuffix(" mm");
    pass_depth->setValue(job->tool.pass_depth);
    auto* feed = new QDoubleSpinBox(&dialog);
    feed->setRange(0.01, 100000.0);
    feed->setDecimals(1);
    feed->setSuffix(" mm/min");
    feed->setValue(job->tool.feed_rate);
    auto* depth = new QDoubleSpinBox(&dialog);
    depth->setRange(0.0, 100000.0);
    depth->setDecimals(3);
    depth->setSuffix(" mm");
    double current_depth = 0.0;
    std::visit([&](auto& settings) {
        using S = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<S, cam::ProfileSettings> ||
                      std::is_same_v<S, cam::PocketSettings> ||
                      std::is_same_v<S, cam::DrillSettings>) current_depth = settings.cut_depth;
    }, job->settings);
    depth->setValue(current_depth);
    form->addRow("Nome", name);
    form->addRow("Diâmetro da ferramenta", diameter);
    form->addRow("Profundidade por passada", pass_depth);
    form->addRow("Avanço XY", feed);
    form->addRow("Profundidade total", depth);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    job->name = name->text().toStdString();
    job->tool.diameter = diameter->value();
    job->tool.pass_depth = pass_depth->value();
    job->tool.feed_rate = feed->value();
    std::visit([&](auto& settings) {
        using S = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<S, cam::ProfileSettings> ||
                      std::is_same_v<S, cam::PocketSettings> ||
                      std::is_same_v<S, cam::DrillSettings>) settings.cut_depth = depth->value();
    }, job->settings);
    cam::set_settings_name(job->settings, job->name);
    job->results.clear();
    job->error.clear();
    document_.modified = true;
    const auto errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    if (errors.empty()) statusBar()->showMessage("Percurso atualizado");
    else statusBar()->showMessage(QString::fromStdString(errors.front()));
}

void MainWindow::duplicate_current_toolpath() {
    if (!toolpath_list_) return;
    const int row = toolpath_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(document_.toolpaths.size())) return;
    const auto& source = document_.toolpaths[static_cast<size_t>(row)];
    auto clone = std::make_shared<cam::ToolpathJob>(*source);
    clone->id = new_id();
    clone->name = document_.unique_toolpath_name(source->name);
    clone->results.clear();
    clone->error.clear();
    document_.add_toolpath(clone);
    const auto errors = document_.recalculate_toolpaths(clone);
    refresh_toolpaths();
    toolpath_list_->setCurrentRow(toolpath_list_->count() - 1);
    if (errors.empty()) statusBar()->showMessage("Percurso duplicado");
    else statusBar()->showMessage(QString::fromStdString(errors.front()));
}

void MainWindow::delete_current_toolpath() {
    if (!toolpath_list_) return;
    const int row = toolpath_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(document_.toolpaths.size())) return;
    document_.remove_toolpath(document_.toolpaths[static_cast<size_t>(row)]);
    refresh_toolpaths();
    statusBar()->showMessage("Percurso excluído");
}

void MainWindow::toggle_current_toolpath() {
    if (!toolpath_list_) return;
    const int row = toolpath_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(document_.toolpaths.size())) return;
    auto& job = document_.toolpaths[static_cast<size_t>(row)];
    job->set_visible(!job->visible);
    refresh_toolpaths();
}

void MainWindow::move_current_toolpath(int delta) {
    if (!toolpath_list_) return;
    const int row = toolpath_list_->currentRow();
    if (row < 0 || row >= static_cast<int>(document_.toolpaths.size())) return;
    const auto job = document_.toolpaths[static_cast<size_t>(row)];
    document_.move_toolpath(job, delta);
    refresh_toolpaths();
    toolpath_list_->setCurrentRow(std::clamp(row + delta, 0, toolpath_list_->count() - 1));
}

void MainWindow::recalculate_toolpaths() {
    if (document_.toolpaths.empty()) {
        statusBar()->showMessage("Nenhum percurso para recalcular");
        return;
    }
    const auto errors = document_.recalculate_toolpaths();
    refresh_toolpaths();
    if (errors.empty()) statusBar()->showMessage("Percursos recalculados");
    else statusBar()->showMessage(QString::fromStdString(errors.front()));
}

void MainWindow::toggle_simulation() {
    show_right_page(2);
    if (simulation_timer_ && simulation_timer_->isActive()) {
        pause_simulation();
        return;
    }
    if (!simulation_) begin_simulation("visible");
    play_simulation();
}

void MainWindow::begin_simulation(const QString& scope) {
    if (simulation_timer_) simulation_timer_->stop();

    std::vector<std::shared_ptr<cam::ToolpathJob>> jobs;
    if (scope == "selected") {
        const int row = toolpath_list_ ? toolpath_list_->currentRow() : -1;
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            jobs.push_back(document_.toolpaths[static_cast<size_t>(row)]);
    } else {
        for (const auto& job : document_.toolpaths) {
            if (scope == "all" || job->visible) jobs.push_back(job);
        }
    }

    std::vector<sim::SimulationOperation> operations;
    for (const auto& job : jobs) {
        if (!job || !job->calculated() || !job->error.empty()) continue;
        for (const auto& path : job->results)
            operations.push_back({path, simulation_tool_for_result(*job, path)});
    }
    if (operations.empty()) {
        statusBar()->showMessage("Calcule pelo menos um percurso antes de simular.");
        show_right_page(2);
        return;
    }

    const auto& material = document_.material;
    const auto config = sim::HeightmapConfig::for_stock(
        material.width, material.height, material.thickness, material.surface_z());
    simulation_ = std::make_unique<sim::ToolpathSimulation>(
        std::move(operations), config, material.surface_z() - material.thickness);

    if (simulation_operation_label_) {
        simulation_operation_label_->setText(
            QString("%1 percurso(s) · %2 · estimativa %3")
                .arg(static_cast<int>(simulation_->operations.size()))
                .arg(QString::fromStdString(simulation_->operations.front().tool.name))
                .arg(format_simulation_time(simulation_->total_seconds)));
    }
    show_3d_view();
    show_right_page(2);
    refresh_3d_view();
    refresh_simulation_view();
    QTimer::singleShot(120, this, [this] { refresh_simulation_view(); });
    statusBar()->showMessage("Simulação pronta — pressione ▶ para iniciar");
}

void MainWindow::play_simulation() {
    if (!simulation_ || simulation_->finished() || !simulation_timer_) return;
    simulation_last_tick_ = std::chrono::steady_clock::now();
    simulation_timer_->start();
    refresh_simulation_status();
}

void MainWindow::pause_simulation() {
    if (simulation_timer_) simulation_timer_->stop();
    refresh_simulation_status();
}

void MainWindow::step_simulation() {
    pause_simulation();
    if (!simulation_) return;
    simulation_->step();
    refresh_simulation_view();
}

void MainWindow::finish_simulation() {
    pause_simulation();
    if (!simulation_) return;
    simulation_->advance(simulation_->remaining_seconds());
    refresh_simulation_view();
}

void MainWindow::restart_simulation() {
    pause_simulation();
    if (!simulation_) return;
    simulation_->reset();
    refresh_simulation_view();
}

void MainWindow::update_simulation() {
    if (!simulation_) {
        pause_simulation();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - simulation_last_tick_).count();
    simulation_last_tick_ = now;
    const double delta = std::clamp(elapsed, 0.0, 0.1);
    const double speed = simulation_speed_slider_ ? simulation_speed_slider_->value() / 10.0 : 1.0;
    const bool finished = simulation_->advance(delta * speed);
    refresh_simulation_view();
    if (finished) {
        pause_simulation();
        statusBar()->showMessage("Simulação concluída");
    }
}

void MainWindow::refresh_simulation_view() {
    if (!simulation_ || !viewport_3d_ || !viewport_3d_->isValid()) {
        refresh_simulation_status();
        return;
    }
    const auto mesh = simulation_->stock_mesh(material_display_color(document_.material));
    viewport_3d_->set_simulation_mesh(mesh.vertices, mesh.indices,
                                      std::make_pair(simulation_->config.grid_height(),
                                                     simulation_->config.grid_width()));
    viewport_3d_->set_simulation_path(simulation_->path_vertices());
    if (simulation_->current_operation < simulation_->operations.size()) {
        const auto& operation = simulation_->operations[simulation_->current_operation];
        viewport_3d_->set_simulation_tool(QVector3D(static_cast<float>(simulation_->cursor[0]),
                                                     static_cast<float>(simulation_->cursor[1]),
                                                     static_cast<float>(simulation_->cursor[2])),
                                                     operation.tool.diameter);
    }
    refresh_simulation_status();
}

void MainWindow::refresh_simulation_status() {
    if (!simulation_info_label_ || !simulation_progress_) return;
    if (!simulation_) {
        simulation_info_label_->setText("Pronto");
        simulation_progress_->setValue(0);
        if (simulation_operation_label_)
            simulation_operation_label_->setText("Calcule um percurso para simular.");
        for (auto* button : {simulation_play_button_, simulation_pause_button_, simulation_step_button_,
                             simulation_finish_button_, simulation_restart_button_}) {
            if (button) button->setEnabled(false);
        }
        return;
    }
    const double progress = simulation_->progress();
    QString operation = "Concluída";
    if (!simulation_->finished() && simulation_->current_operation < simulation_->operations.size())
        operation = QString::fromStdString(
            simulation_->operations[simulation_->current_operation].toolpath.name);
    simulation_info_label_->setText(QString("%1 · %2 / %3")
                                        .arg(operation)
                                        .arg(format_simulation_time(simulation_->elapsed_seconds()))
                                        .arg(format_simulation_time(simulation_->total_seconds)));
    simulation_progress_->setValue(static_cast<int>(std::clamp(progress, 0.0, 1.0) * 1000.0));
    const bool playing = simulation_timer_ && simulation_timer_->isActive();
    const bool finished = simulation_->finished();
    if (simulation_play_button_) simulation_play_button_->setEnabled(!playing && !finished);
    if (simulation_pause_button_) simulation_pause_button_->setEnabled(playing);
    if (simulation_step_button_) simulation_step_button_->setEnabled(!playing && !finished);
    if (simulation_finish_button_) simulation_finish_button_->setEnabled(!playing && !finished);
    if (simulation_restart_button_) simulation_restart_button_->setEnabled(true);
}

void MainWindow::set_right_panel_collapsed(bool collapsed) {
    if (!toolpath_dock_ || !right_panel_scroll_) return;
    if (collapsed) {
        right_panel_scroll_->setVisible(false);
        toolpath_dock_->setFixedWidth(44);
        if (selection_nav_button_) selection_nav_button_->setChecked(false);
        if (toolpath_nav_button_) toolpath_nav_button_->setChecked(false);
        if (simulation_nav_button_) simulation_nav_button_->setChecked(false);
        if (automatic_nav_button_) automatic_nav_button_->setChecked(false);
    } else {
        const int dock_width = std::clamp(width() / 4, 240, 370);
        toolpath_dock_->setMinimumWidth(246);
        toolpath_dock_->setMaximumWidth(370);
        resizeDocks({toolpath_dock_}, {dock_width}, Qt::Horizontal);
        toolpath_dock_->resize(dock_width, toolpath_dock_->height());
        right_panel_scroll_->setVisible(true);
        const int idx = right_panel_stack_ ? right_panel_stack_->currentIndex() : 0;
        if (selection_nav_button_) selection_nav_button_->setChecked(idx == 0);
        if (toolpath_nav_button_) toolpath_nav_button_->setChecked(idx == 1);
        if (simulation_nav_button_) simulation_nav_button_->setChecked(idx == 2);
        if (automatic_nav_button_) automatic_nav_button_->setChecked(idx == 3);
        refresh_guide_previews();
        refresh_automatic_cut_panel();
    }
}

void MainWindow::show_right_page(int index) {
    if (!right_panel_stack_ || index < 0 || index >= right_panel_stack_->count()) return;
    right_panel_stack_->setCurrentIndex(index);
    set_right_panel_collapsed(false);
    const std::array<QToolButton*, 4> buttons = {
        selection_nav_button_, toolpath_nav_button_, simulation_nav_button_, automatic_nav_button_};
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        if (buttons[static_cast<size_t>(i)]) buttons[static_cast<size_t>(i)]->setChecked(i == index);
    }
}

void MainWindow::refresh_automatic_cut_panel() {
    if (!automatic_cut_combo_ || !automatic_cut_info_label_ ||
        !automatic_cut_combo_->isVisible() || !automatic_cut_panel_dirty_) return;
    automatic_cut_panel_dirty_ = false;
    // Selecting the matching preset must not recursively rescan the document.
    const QSignalBlocker combo_blocker(automatic_cut_combo_);
    const auto& presets = cam::house_presets();
    const double thickness = document_.material.thickness;
    size_t target_count = 0;
    for (const auto& entity : document_.entities)
        if (entity->selected && document_.is_editable(*entity)) ++target_count;
    if (target_count == 0) {
        for (const cad::EntityPtr& entity : document_.entities) {
            if (!cad::is_vector(*entity) || cad::is_a<cad::Text>(*entity) ||
                cad::is_a<cad::ConstructionLine>(*entity) ||
                !document_.is_editable(*entity)) continue;
            const cad::Layer* layer = document_.get_layer(entity->layer);
            if (layer && !layer->visible) continue;
            ++target_count;
        }
    }
    int matching_count = 0;
    int matching_combo_index = -1;
    for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
        const auto& preset = presets[static_cast<size_t>(index)];
        const bool matches = std::fabs(preset.thickness - thickness) < 0.01;
        if (matches) {
            ++matching_count;
            matching_combo_index = index + 1;
        }
        automatic_cut_combo_->setItemText(
            index + 1, QString("%1 %2 mm%3")
                           .arg(QString::fromStdString(preset.material))
                           .arg(preset.thickness, 0, 'g', 10)
                           .arg(matches ? "  ← chapa atual" : ""));
    }
    if (automatic_cut_combo_->currentData().toInt() < 0) {
        const auto exact = cam::find_preset(document_.material.material_profile, thickness);
        if (exact) {
            for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
                const auto& candidate = presets[static_cast<size_t>(index)];
                if (candidate.material == exact->material &&
                    std::fabs(candidate.thickness - exact->thickness) < 0.01) {
                    automatic_cut_combo_->setCurrentIndex(index + 1);
                    break;
                }
            }
        } else if (matching_count == 1) {
            automatic_cut_combo_->setCurrentIndex(matching_combo_index);
        }
    }

    const int preset_index = automatic_cut_combo_->currentData().toInt();
    if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) {
        automatic_cut_info_label_->setText(
            QString("O trabalho está com chapa de %1 mm. Escolha um material cadastrado.")
                .arg(thickness, 0, 'g', 10));
        automatic_cut_info_label_->setText(
            automatic_cut_info_label_->text() + QString("\n\nContornos sem texto encontrados: %1.").arg(target_count));
        if (automatic_create_button_) automatic_create_button_->setEnabled(false);
        return;
    }
    const auto& preset = presets[static_cast<size_t>(preset_index)];
    QString description = QString::fromStdString(preset.describe());
    if (std::fabs(preset.thickness - thickness) >= 0.01) {
        description += QString("\n\n⚠ A chapa atual tem %1 mm; este programa foi feito para %2 mm.")
                           .arg(thickness, 0, 'g', 10)
                           .arg(preset.thickness, 0, 'g', 10);
    }
    description += QString("\n\nVetores alvo: %1").arg(target_count);
    automatic_cut_info_label_->setText(description);
    if (automatic_create_button_) automatic_create_button_->setEnabled(true);
}

void MainWindow::create_automatic_cut() {
    if (!automatic_cut_combo_) return;
    const int preset_index = automatic_cut_combo_->currentData().toInt();
    const auto& presets = cam::house_presets();
    if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) return;

    std::vector<cad::EntityPtr> selected = document_.selected_entities();
    if (selected.empty()) {
        for (const cad::EntityPtr& entity : document_.entities) {
            if (!cad::is_vector(*entity) || cad::is_a<cad::Text>(*entity) ||
                cad::is_a<cad::ConstructionLine>(*entity) ||
                !document_.is_editable(*entity)) continue;
            const cad::Layer* layer = document_.get_layer(entity->layer);
            if (layer && !layer->visible) continue;
            selected.push_back(entity);
        }
    }
    if (selected.empty()) {
        QMessageBox::information(this, "Corte automático",
                                 "Selecione os vetores que devem receber o corte.");
        return;
    }
    const auto& preset = presets[static_cast<size_t>(preset_index)];
    if (std::fabs(preset.thickness - document_.material.thickness) >= 0.01) {
        const QString warning =
            QString("A chapa do trabalho está com %1 mm, mas %2 foi programado para %3 mm.\n\n"
                    "O corte será feito até %4 mm de profundidade. Continuar?")
                .arg(document_.material.thickness, 0, 'g', 10)
                .arg(QString::fromStdString(preset.name()))
                .arg(preset.thickness, 0, 'g', 10)
                .arg(preset.cut_depth(), 0, 'g', 10);
        if (QMessageBox::question(this, "Espessura diferente", warning) != QMessageBox::Yes) return;
    }

    auto job = std::make_shared<cam::ToolpathJob>();
    job->kind = cam::ToolpathType::Profile;
    job->name = document_.unique_toolpath_name(preset.name());
    job->tool = preset.tool();
    job->settings = preset.settings();
    cam::set_settings_name(job->settings, job->name);
    for (const auto& entity : selected) job->entity_ids.push_back(entity->id);
    document_.add_toolpath(job);
    const auto errors = document_.recalculate_toolpaths(job);
    refresh_toolpaths();
    if (toolpath_list_) toolpath_list_->setCurrentRow(toolpath_list_->count() - 1);
    show_right_page(1);
    if (errors.empty()) {
        statusBar()->showMessage(
            QString("Corte automático criado e calculado: %1").arg(QString::fromStdString(job->name)));
    } else {
        statusBar()->showMessage(QString::fromStdString(errors.front()));
    }
}

void MainWindow::export_gcode() {
    const auto paths = cam::calculated_toolpaths(document_.toolpaths);
    if (paths.empty()) {
        statusBar()->showMessage("Recalcule um percurso antes de gerar G-Code");
        return;
    }

    QSettings preferences;
    ui::ResponsiveDialog settings_dialog(this);
    settings_dialog.setWindowTitle("Perfil de máquina / pós-processador");
    settings_dialog.setMinimumWidth(470);
    auto* settings_layout = new QVBoxLayout(&settings_dialog);
    auto* settings_form = new QFormLayout();
    auto* post = new QComboBox(&settings_dialog);
    post->setEditable(true);
    post->setInsertPolicy(QComboBox::NoInsert);
    post->completer()->setFilterMode(Qt::MatchContains);
    post->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    for (const auto profile : {cam::PostProcessor::Grbl, cam::PostProcessor::Mach3,
                                cam::PostProcessor::RichAutoA11, cam::PostProcessor::RichAutoA18,
                                cam::PostProcessor::NcStudio, cam::PostProcessor::LinuxCnc})
        post->addItem(QString::fromUtf8(cam::post_label(profile)), static_cast<int>(profile));
    const int saved_post = preferences.value("gcode/post", static_cast<int>(cam::PostProcessor::Grbl)).toInt();
    const auto vectric_posts = cam::installed_vectric_post_files();
    for (const auto& profile : vectric_posts) {
        post->addItem(QString::fromStdString(profile.name), QString::fromStdString(profile.path));
        post->setItemData(post->count() - 1, QString::fromStdString(profile.extension), Qt::UserRole + 1);
    }
    const QString saved_vectric_post = preferences.value("gcode/vectricPostFile").toString();
    const int saved_vectric_index = saved_vectric_post.isEmpty() ? -1 : post->findData(saved_vectric_post);
    if (saved_vectric_index >= 0) post->setCurrentIndex(saved_vectric_index);
    else post->setCurrentIndex(std::max(0, post->findData(saved_post)));
    const auto selected_post = [post] {
        const QVariant data = post->currentData();
        return data.metaType().id() == QMetaType::QString
            ? cam::PostProcessor::VectricAspire
            : static_cast<cam::PostProcessor>(data.toInt());
    };
    auto* tool_change = new QCheckBox("Emitir Tn M6 na troca de ferramenta", &settings_dialog);
    auto* line_numbers = new QCheckBox("Numerar linhas", &settings_dialog);
    auto* comments = new QCheckBox("Incluir comentários", &settings_dialog);
    auto* return_home = new QCheckBox("Retornar ao ponto inicial no fim", &settings_dialog);
    connect(post, qOverload<int>(&QComboBox::currentIndexChanged), &settings_dialog,
            [tool_change, line_numbers, comments, return_home, selected_post] {
        const auto selected = selected_post();
        const bool vectric = selected == cam::PostProcessor::VectricAspire;
        tool_change->setEnabled(selected != cam::PostProcessor::Grbl && !vectric);
        tool_change->setChecked(selected == cam::PostProcessor::Mach3 ||
                                 selected == cam::PostProcessor::LinuxCnc);
        line_numbers->setEnabled(!vectric);
        comments->setEnabled(!vectric);
        return_home->setEnabled(!vectric);
    });
    settings_form->addRow("Controladora", post);
    settings_form->addRow("Troca de ferramenta", tool_change);
    settings_form->addRow(line_numbers);
    settings_form->addRow(comments);
    settings_form->addRow(return_home);
    settings_layout->addLayout(settings_form);
    auto* compatibility = new QLabel(
        "RichAuto A11/A18: o arquivo usa XYZ, seleciona G54 e omite G94. Configure o DSP para ler G54 "
        "e confira G40/G49. A18 aqui não gera movimentos rotativos no eixo A. M6 fica desligado por padrão "
        "nos perfis RichAuto/NCStudio; ative só se a troca estiver configurada. Simule e faça teste no ar antes do corte.",
        &settings_dialog);
    compatibility->setWordWrap(true);
    compatibility->setObjectName("MutedText");
    settings_layout->addWidget(compatibility);
    const auto initial_post = selected_post();
    const QString tool_change_key = QString("gcode/toolChange/%1").arg(static_cast<int>(initial_post));
    const bool default_tool_change = initial_post == cam::PostProcessor::Mach3 ||
                                     initial_post == cam::PostProcessor::LinuxCnc;
    const bool saved_tool_change = preferences.value(tool_change_key, default_tool_change).toBool();
    tool_change->setChecked(initial_post != cam::PostProcessor::Grbl &&
                             initial_post != cam::PostProcessor::VectricAspire && saved_tool_change);
    tool_change->setEnabled(initial_post != cam::PostProcessor::Grbl &&
                            initial_post != cam::PostProcessor::VectricAspire);
    const bool initial_vectric = initial_post == cam::PostProcessor::VectricAspire;
    line_numbers->setEnabled(!initial_vectric);
    comments->setEnabled(!initial_vectric);
    return_home->setEnabled(!initial_vectric);
    line_numbers->setChecked(preferences.value("gcode/lineNumbers", false).toBool());
    comments->setChecked(preferences.value("gcode/comments", true).toBool());
    return_home->setChecked(preferences.value("gcode/returnHome", true).toBool());
    auto* settings_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                   &settings_dialog);
    settings_buttons->button(QDialogButtonBox::Ok)->setText("Continuar");
    connect(settings_buttons, &QDialogButtonBox::accepted, &settings_dialog, &QDialog::accept);
    connect(settings_buttons, &QDialogButtonBox::rejected, &settings_dialog, &QDialog::reject);
    settings_layout->addWidget(settings_buttons);
    if (settings_dialog.exec() != QDialog::Accepted) return;
    if (post->currentIndex() < 0 || !post->currentData().isValid()) {
        QMessageBox::warning(this, "Pós-processador", "Selecione um pós-processador válido da lista.");
        return;
    }

    cam::GCodeSettings settings;
    settings.post = selected_post();
    if (settings.post == cam::PostProcessor::VectricAspire)
        settings.vectric_post_file = post->currentData().toString().toStdString();
    if (settings.post == cam::PostProcessor::VectricAspire)
        settings.vectric_post_extension = post->currentData(Qt::UserRole + 1).toString().toStdString();
    settings.tool_change = tool_change->isChecked();
    settings.line_numbers = line_numbers->isChecked();
    settings.comments = comments->isChecked();
    settings.home_at_end = return_home->isChecked();
    preferences.setValue("gcode/post", static_cast<int>(settings.post));
    if (!settings.vectric_post_file.empty())
        preferences.setValue("gcode/vectricPostFile", QString::fromStdString(settings.vectric_post_file));
    if (!settings.vectric_post_extension.empty())
        preferences.setValue("gcode/vectricPostExtension", QString::fromStdString(settings.vectric_post_extension));
    preferences.setValue(QString("gcode/toolChange/%1").arg(static_cast<int>(settings.post)), settings.tool_change);
    preferences.setValue("gcode/lineNumbers", settings.line_numbers);
    preferences.setValue("gcode/comments", settings.comments);
    preferences.setValue("gcode/returnHome", settings.home_at_end);

    QString file_filter = "G-Code (*.nc *.tap *.gcode);;Todos os arquivos (*)";
    if (!settings.vectric_post_extension.empty())
        file_filter = QString("Pós Aspire (*.%1);;Todos os arquivos (*)")
                          .arg(QString::fromStdString(settings.vectric_post_extension));
    const QString path = QFileDialog::getSaveFileName(this, "Salvar G-Code", {}, file_filter);
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Erro ao salvar G-Code", output.errorString());
        return;
    }
    std::string code;
    try {
        code = cam::generate_gcode(paths, document_.material, settings, document_.title());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Pós-processador Aspire", QString::fromUtf8(error.what()));
        return;
    }
    output.write(QByteArray::fromStdString(code));
    output.close();
    statusBar()->showMessage(QString("G-Code salvo: %1").arg(path));
}

void MainWindow::export_setup_sheet() {
    if (recalculate_order_service_if_geometry_changed()) refresh_toolpaths();
    // 1. Build default G-code settings from preferences
    cam::GCodeSettings gcode_settings;
    QSettings preferences;
    gcode_settings.post = static_cast<cam::PostProcessor>(preferences.value("gcode/post", 0).toInt());
    gcode_settings.vectric_post_file = preferences.value("gcode/vectricPostFile").toString().toStdString();
    gcode_settings.vectric_post_extension = preferences.value("gcode/vectricPostExtension").toString().toStdString();
    gcode_settings.tool_change = preferences.value(QString("gcode/toolChange/%1").arg(static_cast<int>(gcode_settings.post)), false).toBool();
    gcode_settings.line_numbers = preferences.value("gcode/lineNumbers", true).toBool();
    gcode_settings.comments = preferences.value("gcode/comments", true).toBool();
    gcode_settings.home_at_end = preferences.value("gcode/returnHome", true).toBool();

    // 2. Register/update all currently calculated toolpaths in job_history_
    for (const auto& job : document_.toolpaths) {
        if (!job || job->results.empty()) continue;
        auto it = std::find_if(job_history_.begin(), job_history_.end(), [&](const auto& rec) {
            return rec.id == job->id;
        });
        std::vector<const cam::Toolpath*> job_paths;
        std::vector<cam::SetupSheetOperation> ops;
        double job_secs = 0.0, job_cut = 0.0;
        for (const auto& res : job->results) {
            if (res.visible) {
                job_paths.push_back(&res);
                ops.push_back({&res, job.get()});
                job_secs += res.estimated_seconds();
                auto [c_len, r_len] = res.lengths();
                (void)r_len;
                job_cut += c_len;
            }
        }
        if (ops.empty()) continue;

        std::string code;
        if (!job_paths.empty()) {
            try {
                code = cam::generate_gcode(job_paths, document_.material, gcode_settings, job->name);
            } catch (const std::exception& error) {
                QMessageBox::critical(this, "Pós-processador Aspire", QString::fromUtf8(error.what()));
                return;
            }
        }

        if (it != job_history_.end()) {
            it->name = job->name;
            it->gcode = std::move(code);
            it->total_seconds = job_secs;
            it->total_cut_m = job_cut / 1000.0;
            it->operations = std::move(ops);
            it->material = document_.material;
            if (it->part_name.empty()) it->part_name = document_.title().empty() ? job->name : document_.title();
            if (it->material_profile.empty()) it->material_profile = document_.material.material_profile.empty() ? "MDF 15mm" : document_.material.material_profile;
            it->width = document_.material.width > 0 ? document_.material.width : 1000.0;
            it->height = document_.material.height > 0 ? document_.material.height : 1000.0;
            it->thickness = document_.material.thickness > 0 ? document_.material.thickness : 15.0;
            it->area_m2 = std::format("{:.3f}", (it->width * it->height) / 1000000.0);
        } else {
            RegisteredJobRecord rec;
            rec.id = job->id;
            rec.name = job->name;
            rec.part_name = document_.title().empty() ? job->name : document_.title();
            rec.material_profile = document_.material.material_profile.empty() ? "MDF 15mm" : document_.material.material_profile;
            rec.width = document_.material.width > 0 ? document_.material.width : 1000.0;
            rec.height = document_.material.height > 0 ? document_.material.height : 1000.0;
            rec.thickness = document_.material.thickness > 0 ? document_.material.thickness : 15.0;
            rec.area_m2 = std::format("{:.3f}", (rec.width * rec.height) / 1000000.0);
            rec.gcode = std::move(code);
            rec.total_seconds = job_secs;
            rec.total_cut_m = job_cut / 1000.0;
            rec.timestamp = QDateTime::currentDateTime();
            rec.operations = std::move(ops);
            rec.material = document_.material;
            rec.info.part_name = rec.part_name;
            rec.info.material_type = rec.material_profile;
            rec.info.area_m2 = rec.area_m2;
            job_history_.push_back(std::move(rec));
        }
    }

    if (job_history_.empty()) {
        statusBar()->showMessage("Recalcule um percurso antes de abrir a central de ordens de serviço");
        return;
    }

    QString logo_path = preferences.value("jobSheet/logoPath").toString();
    QImage logo_image;
    if (!logo_path.isEmpty() && QFileInfo::exists(logo_path)) {
        QImageReader reader(logo_path);
        logo_image = reader.read();
        if (logo_image.isNull()) logo_path.clear();
    } else {
        logo_path.clear();
    }

    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle("Central de Ordens de Serviço & G-Code — UNY CV");
    dialog.setObjectName("WorkOrderDialog");
    dialog.resize(960, 700);
    dialog.setMinimumSize(860, 580);
    dialog.setStyleSheet("QDialog#WorkOrderDialog { background: #101923; }");
    dialog.installEventFilter(new ui::SafeWheelFilter(&dialog));

    auto* main_layout = new QVBoxLayout(&dialog);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(14, 12, 14, 12);

    // Header with App / Company branding
    auto* header_widget = new QWidget(&dialog);
    auto* header_layout = new QHBoxLayout(header_widget);
    header_layout->setContentsMargins(0, 0, 0, 0);

    auto* title_box = new QVBoxLayout();
    title_box->setSpacing(2);
    auto* title_lbl = new QLabel("📋 CENTRAL DE ORDENS DE SERVIÇO & G-CODE", header_widget);
    title_lbl->setStyleSheet("font-size: 16pt; font-weight: 700; color: #8cc8ff;");
    auto* subtitle_lbl = new QLabel(
        QString("Projeto: %1 · %2 percurso(s) acumulados no histórico")
            .arg(QString::fromStdString(document_.title().empty() ? "Sem título" : document_.title()))
            .arg(job_history_.size()), header_widget);
    subtitle_lbl->setObjectName("MutedText");
    title_box->addWidget(title_lbl);
    title_box->addWidget(subtitle_lbl);
    header_layout->addLayout(title_box, 1);

    auto* logo_badge = new QLabel(logo_path.isEmpty() ? "UNY CV" : QFileInfo(logo_path).fileName(), header_widget);
    logo_badge->setStyleSheet("background: #1d2d40; color: #d8eaff; border: 1px solid #385573; font-weight: 700; padding: 8px 14px; border-radius: 7px;");
    header_layout->addWidget(logo_badge);
    main_layout->addWidget(header_widget);

    // Job Selector Bar (History Accumulation Bar)
    auto* selector_card = new QFrame(&dialog);
    selector_card->setObjectName("OSSelectorCard");
    selector_card->setStyleSheet("QFrame#OSSelectorCard { background: #172535; border: 1px solid #304861; border-radius: 8px; }");
    auto* selector_layout = new QHBoxLayout(selector_card);
    selector_layout->setContentsMargins(4, 4, 4, 4);
    selector_layout->setSpacing(8);

    auto* sel_lbl = new QLabel("Folha / Percurso Ativo:", selector_card);
    sel_lbl->setStyleSheet("font-weight: 700; color: #b9d8f4; font-size: 9pt; padding: 0 8px;");
    auto* job_combo = new ui::SafeComboBox(selector_card);
    job_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    for (size_t i = 0; i < job_history_.size(); ++i) {
        const auto& rec = job_history_[i];
        job_combo->addItem(QString("[%1] %2 — %3 (%4 min · %5 m)")
            .arg(i + 1)
            .arg(QString::fromStdString(rec.name))
            .arg(rec.timestamp.toString("dd/MM HH:mm"))
            .arg(rec.total_seconds / 60.0, 0, 'f', 1)
            .arg(rec.total_cut_m, 0, 'f', 2),
            static_cast<int>(i));
    }
    const int combined_index = static_cast<int>(job_history_.size());
    if (job_history_.size() > 1) {
        job_combo->addItem("★ [TODOS OS PERCURSOS COMBINADOS] (Visão Geral do Projeto)", combined_index);
    }
    job_combo->setCurrentIndex(static_cast<int>(job_history_.size() - 1));

    selector_layout->addWidget(sel_lbl);
    selector_layout->addWidget(job_combo, 1);
    main_layout->addWidget(selector_card);

    // Metric Cards Bar
    auto* metrics_widget = new QWidget(&dialog);
    auto* metrics_layout = new QHBoxLayout(metrics_widget);
    metrics_layout->setSpacing(8);
    metrics_layout->setContentsMargins(0, 0, 0, 0);

    auto create_card = [&](const QString& label_text, const QString& accent_color) {
        auto* card = new QFrame(metrics_widget);
        card->setObjectName("OSMetricCard");
        card->setStyleSheet(QString(
            "QFrame#OSMetricCard { background: #172535; border: 1px solid #304861; border-top: 3px solid %1; border-radius: 8px; padding: 4px 8px; }"
        ).arg(accent_color));
        auto* cl = new QVBoxLayout(card);
        cl->setSpacing(1);
        cl->setContentsMargins(4, 3, 4, 3);
        auto* l = new QLabel(label_text, card);
        l->setStyleSheet("font-size: 7.5pt; font-weight: 700; color: #9cb4cc; text-transform: uppercase;");
        auto* v = new QLabel("—", card);
        v->setStyleSheet("font-size: 12pt; font-weight: 700; color: #f0f7ff;");
        cl->addWidget(l);
        cl->addWidget(v);
        metrics_layout->addWidget(card);
        return v;
    };

    QLabel* area_card_val = create_card("Área da Chapa (m²)", "#0284c7");
    QLabel* dims_card_val = create_card("Dimensões Chapa (X × Y × Z)", "#10b981");
    QLabel* time_card_val = create_card("Tempo de Corte Estimado", "#f59e0b");
    QLabel* cut_card_val = create_card("Comprimento de Corte", "#8b5cf6");
    main_layout->addWidget(metrics_widget);

    auto* tabs = new QTabWidget(&dialog);
    tabs->setStyleSheet(R"QSS(
        QTabWidget::pane {
            border: 1px solid #334a64;
            background: #141d27;
            border-radius: 8px;
            top: -1px;
        }
        QTabBar::tab {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #223244, stop:1 #192533);
            color: #b8c9dc;
            border: 1px solid #364e6b;
            border-bottom: none;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            padding: 9px 20px;
            margin-right: 5px;
            font-weight: 700;
            font-size: 9.5pt;
        }
        QTabBar::tab:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2e435c, stop:1 #213346);
            color: #ffffff;
            border-color: #52759e;
        }
        QTabBar::tab:selected {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1e3a5f, stop:1 #142232);
            color: #60a5fa;
            border: 1px solid #3b82f6;
            border-top: 3px solid #60a5fa;
            border-bottom: 2px solid #142232;
            font-weight: 800;
        }
    )QSS");

    // Tab 1: Operations Table
    auto* tab_table = new QWidget();
    auto* table_layout = new QVBoxLayout(tab_table);
    table_layout->setContentsMargins(6, 6, 6, 6);

    auto* table = new QTableWidget(tab_table);
    table->setColumnCount(13);
    table->setHorizontalHeaderLabels({
        "#", "Operação", "Tipo", "Ferramenta", "Ø (mm)", "Avanço XY",
        "Mergulho Z", "RPM", "Faixa Z", "Passo Z", "Pontes", "Corte", "Tempo"
    });
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setStyleSheet(R"QSS(
        QTableWidget {
            background-color: #17212d;
            color: #e2e8f0;
            gridline-color: #27374b;
            border: 1px solid #334a64;
            border-radius: 8px;
            selection-background-color: #e2e8f0;
            selection-color: #0f172a;
            outline: none;
            font-size: 9pt;
        }
        QTableWidget::item {
            padding: 7px 5px;
            border-bottom: 1px solid #202d3e;
        }
        QTableWidget::item:selected {
            background-color: #e2e8f0;
            color: #0f172a;
            font-weight: 700;
        }
        QTableWidget::item:hover:!selected {
            background-color: #223245;
            color: #ffffff;
        }
        QHeaderView::section {
            background-color: #0d1520;
            color: #94a3b8;
            font-weight: 700;
            font-size: 8.5pt;
            padding: 7px 4px;
            border: 1px solid #1a2636;
            border-top: none;
        }
    )QSS");
    table_layout->addWidget(table);
    tabs->addTab(tab_table, "📋 Tabela de Operações");

    // Tab 2: Visual Preview (Desenho Percurso)
    auto* tab_preview = new QWidget();
    auto* preview_layout = new QVBoxLayout(tab_preview);
    preview_layout->setContentsMargins(6, 6, 6, 6);
    preview_layout->setSpacing(6);

    auto* preview_toolbar = new QWidget(tab_preview);
    auto* pt_layout = new QHBoxLayout(preview_toolbar);
    pt_layout->setContentsMargins(0, 0, 0, 0);

    auto* pt_title = new QLabel("📐 Ilustração Técnica do Percurso e Posicionamento na Chapa", preview_toolbar);
    pt_title->setStyleSheet("font-weight: bold; color: #60a5fa; font-size: 9pt;");
    auto* copy_img_btn = new QPushButton("📋 Copiar Ilustração", preview_toolbar);
    copy_img_btn->setIcon(ui::get_icon("copy"));
    auto* save_img_btn = new QPushButton("💾 Salvar Imagem HD (.png)...", preview_toolbar);
    save_img_btn->setIcon(ui::get_icon("tp_gcode"));

    pt_layout->addWidget(pt_title, 1);
    pt_layout->addWidget(copy_img_btn);
    pt_layout->addWidget(save_img_btn);
    preview_layout->addWidget(preview_toolbar);

    auto* preview_scroll = new QScrollArea(tab_preview);
    preview_scroll->setWidgetResizable(true);
    preview_scroll->setStyleSheet("QScrollArea { background-color: #0b1118; border: 1px solid #233448; border-radius: 8px; }");
    auto* preview_label = new QLabel(preview_scroll);
    preview_label->setAlignment(Qt::AlignCenter);
    preview_scroll->setWidget(preview_label);
    preview_layout->addWidget(preview_scroll, 1);
    tabs->addTab(tab_preview, "📐 Desenho Percurso");

    // Tab 3: Stored G-Code Copy
    auto* tab_gcode = new QWidget();
    auto* gcode_layout = new QVBoxLayout(tab_gcode);
    gcode_layout->setContentsMargins(6, 6, 6, 6);
    gcode_layout->setSpacing(6);

    auto* gcode_status_bar = new QWidget(tab_gcode);
    auto* gcode_status_layout = new QHBoxLayout(gcode_status_bar);
    gcode_status_layout->setContentsMargins(0, 0, 0, 0);

    auto* gcode_status_lbl = new QLabel("✓ Cópia do G-Code gravada e pronta para uso", gcode_status_bar);
    gcode_status_lbl->setStyleSheet("font-weight: bold; color: #0284c7;");
    auto* copy_gcode_btn = new QPushButton("📋 Copiar G-Code", gcode_status_bar);
    copy_gcode_btn->setIcon(ui::get_icon("copy"));
    auto* save_gcode_btn = new QPushButton("💾 Salvar Cópia do G-Code (.nc)...", gcode_status_bar);
    save_gcode_btn->setIcon(ui::get_icon("tp_gcode"));

    gcode_status_layout->addWidget(gcode_status_lbl, 1);
    gcode_status_layout->addWidget(copy_gcode_btn);
    gcode_status_layout->addWidget(save_gcode_btn);
    gcode_layout->addWidget(gcode_status_bar);

    auto* gcode_editor = new QPlainTextEdit(tab_gcode);
    gcode_editor->setReadOnly(true);
    gcode_editor->setStyleSheet("font-family: Consolas, monospace; font-size: 9pt; background: #0f172a; color: #38bdf8;");
    gcode_layout->addWidget(gcode_editor, 1);
    tabs->addTab(tab_gcode, "💾 Cópia do G-Code Guardada");

    // Tab 4: Material and Identificação
    auto* tab_info = new QWidget();
    auto* info_scroll = new QScrollArea(tab_info);
    info_scroll->setWidgetResizable(true);
    auto* info_container = new QWidget(info_scroll);
    auto* info_layout = new QVBoxLayout(info_container);

    auto* info_form = new QFormLayout();
    info_form->setSpacing(8);

    auto* part_name = new QLineEdit(info_container);
    auto* order_number = new QLineEdit(info_container);
    auto* customer = new QLineEdit(info_container);
    auto* contact = new QLineEdit(info_container);
    auto* operator_name = new QLineEdit(info_container);
    auto* revision = new QLineEdit("1.0", info_container);

    auto* material_combo = new ui::SafeComboBox(info_container);
    material_combo->setEditable(true);
    material_combo->addItems({
        "MDF 15mm", "MDF 18mm", "MDF 6mm", "MDF 3mm",
        "Compensado Naval 15mm", "Compensado Pinus 18mm",
        "Acrílico Cristal 3mm", "Acrílico Cristal 5mm",
        "ACM (Alumínio Composto) 4mm",
        "PVC Expandido 10mm", "PVC Expandido 15mm",
        "Alumínio 3mm", "Madeira Maciça"
    });

    auto* mat_dim_row = new QWidget(info_container);
    auto* mat_dim_layout = new QHBoxLayout(mat_dim_row);
    mat_dim_layout->setContentsMargins(0, 0, 0, 0);

    auto* width_spin = new ui::SafeDoubleSpinBox(mat_dim_row);
    width_spin->setRange(1.0, 100000.0);
    width_spin->setDecimals(1);
    width_spin->setSuffix(" mm");

    auto* height_spin = new ui::SafeDoubleSpinBox(mat_dim_row);
    height_spin->setRange(1.0, 100000.0);
    height_spin->setDecimals(1);
    height_spin->setSuffix(" mm");

    auto* thickness_spin = new ui::SafeDoubleSpinBox(mat_dim_row);
    thickness_spin->setRange(0.1, 1000.0);
    thickness_spin->setDecimals(1);
    thickness_spin->setSuffix(" mm");

    mat_dim_layout->addWidget(new QLabel("X:"));
    mat_dim_layout->addWidget(width_spin);
    mat_dim_layout->addWidget(new QLabel("Y:"));
    mat_dim_layout->addWidget(height_spin);
    mat_dim_layout->addWidget(new QLabel("Z:"));
    mat_dim_layout->addWidget(thickness_spin);

    auto* notes = new QTextEdit(info_container);
    notes->setMaximumHeight(70);

    info_form->addRow("Nome do Arquivo / Peça", part_name);
    info_form->addRow("Material", material_combo);
    info_form->addRow("Dimensões da Chapa (X / Y / Z)", mat_dim_row);
    info_form->addRow("Cliente", customer);
    info_form->addRow("Contato", contact);
    info_form->addRow("Número da OS / Pedido", order_number);
    info_form->addRow("Operador", operator_name);
    info_form->addRow("Revisão", revision);
    info_form->addRow("Observações", notes);

    auto* logo_row = new QWidget(info_container);
    auto* logo_layout = new QHBoxLayout(logo_row);
    logo_layout->setContentsMargins(0, 0, 0, 0);
    auto* logo_status = new QLabel(logo_path.isEmpty() ? "Logo UNY CV padrão" : QFileInfo(logo_path).fileName(), logo_row);
    logo_status->setObjectName("MutedText");
    auto* choose_logo = new QPushButton("Escolher Imagem...", logo_row);
    auto* remove_logo = new QPushButton("Remover", logo_row);
    logo_layout->addWidget(logo_status, 1);
    logo_layout->addWidget(choose_logo);
    logo_layout->addWidget(remove_logo);
    info_form->addRow("Logotipo da Empresa", logo_row);

    info_layout->addLayout(info_form);
    info_scroll->setWidget(info_container);
    auto* tab_info_layout = new QVBoxLayout(tab_info);
    tab_info_layout->setContentsMargins(6, 6, 6, 6);
    tab_info_layout->addWidget(info_scroll);
    tabs->addTab(tab_info, "🛠 Material e Identificação");
    tabs->setCurrentIndex(1);

    main_layout->addWidget(tabs, 1);

    // Active selection state
    std::vector<cam::SetupSheetOperation> active_operations;
    cad::MaterialSetup active_material = document_.material;
    std::string active_gcode;
    QImage current_preview_img;

    auto populate_active_job = [&](int combo_idx) {
        if (combo_idx < 0) return;
        active_operations.clear();

        if (combo_idx < static_cast<int>(job_history_.size())) {
            const auto& rec = job_history_[static_cast<size_t>(combo_idx)];
            active_operations = rec.operations;
            active_material = rec.material;
            active_gcode = rec.gcode;

            part_name->setText(QString::fromStdString(rec.part_name));
            if (material_combo->findText(QString::fromStdString(rec.material_profile)) == -1)
                material_combo->insertItem(0, QString::fromStdString(rec.material_profile));
            material_combo->setCurrentText(QString::fromStdString(rec.material_profile));
            width_spin->setValue(rec.width);
            height_spin->setValue(rec.height);
            thickness_spin->setValue(rec.thickness);

            const int total_m = static_cast<int>(rec.total_seconds / 60.0);
            const int total_s = static_cast<int>(rec.total_seconds) % 60;
            area_card_val->setText(QString::fromStdString(rec.area_m2) + " m²");
            dims_card_val->setText(QString("%1 × %2 × %3 mm").arg(rec.width, 0, 'f', 1).arg(rec.height, 0, 'f', 1).arg(rec.thickness, 0, 'f', 1));
            time_card_val->setText(QString("%1 min %2 s").arg(total_m).arg(total_s, 2, 10, QChar('0')));
            cut_card_val->setText(QString::number(rec.total_cut_m, 'f', 2) + " m");
        } else {
            // Combined view of all accumulated jobs
            double comb_secs = 0.0, comb_cut = 0.0;
            std::string combined_gcode;
            for (const auto& rec : job_history_) {
                for (const auto& op : rec.operations) active_operations.push_back(op);
                comb_secs += rec.total_seconds;
                comb_cut += rec.total_cut_m;
                if (!combined_gcode.empty()) combined_gcode += "\n\n( ===== PROXIMA OPERACAO ===== )\n";
                combined_gcode += rec.gcode;
            }
            active_material = document_.material;
            active_gcode = combined_gcode;

            part_name->setText(QString::fromStdString(document_.title().empty() ? "Projeto_Completo" : document_.title()));
            const double w = active_material.width > 0 ? active_material.width : 1000.0;
            const double h = active_material.height > 0 ? active_material.height : 1000.0;
            const double t = active_material.thickness > 0 ? active_material.thickness : 15.0;
            width_spin->setValue(w);
            height_spin->setValue(h);
            thickness_spin->setValue(t);

            const int total_m = static_cast<int>(comb_secs / 60.0);
            const int total_s = static_cast<int>(comb_secs) % 60;
            area_card_val->setText(QString::number((w * h) / 1000000.0, 'f', 3) + " m²");
            dims_card_val->setText(QString("%1 × %2 × %3 mm").arg(w, 0, 'f', 1).arg(h, 0, 'f', 1).arg(t, 0, 'f', 1));
            time_card_val->setText(QString("%1 min %2 s").arg(total_m).arg(total_s, 2, 10, QChar('0')));
            cut_card_val->setText(QString::number(comb_cut, 'f', 2) + " m");
        }

        // Fill Table
        table->setRowCount(static_cast<int>(active_operations.size()));
        for (int i = 0; i < static_cast<int>(active_operations.size()); ++i) {
            const auto& op = active_operations[static_cast<size_t>(i)];
            const cam::Toolpath* path = op.path;
            if (!path) continue;

            double cut_len = 0.0, rapid_len = 0.0;
            std::tie(cut_len, rapid_len) = path->lengths();
            const double op_secs = path->estimated_seconds();
            const int op_m = static_cast<int>(op_secs / 60.0);
            const int op_s = static_cast<int>(op_secs) % 60;

            auto [z_min, z_max] = path->depth_range();
            const cam::Tool* tool = op.job ? &op.job->tool : nullptr;

            table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
            table->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(path->name)));
            table->setItem(i, 2, new QTableWidgetItem(op.job ? QString::fromUtf8(op.job->type_label()) : "Percurso"));
            table->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(path->tool_name)));
            table->setItem(i, 4, new QTableWidgetItem(tool ? QString::number(tool->diameter, 'f', 2) : "—"));
            table->setItem(i, 5, new QTableWidgetItem(QString::number(path->feed_rate, 'f', 0) + " mm/min"));
            table->setItem(i, 6, new QTableWidgetItem(QString::number(path->plunge_rate, 'f', 0) + " mm/min"));
            table->setItem(i, 7, new QTableWidgetItem(QString::number(path->spindle_speed)));
            table->setItem(i, 8, new QTableWidgetItem(QString("%1 a %2 mm").arg(z_min, 0, 'f', 1).arg(z_max, 0, 'f', 1)));
            table->setItem(i, 9, new QTableWidgetItem(tool ? QString::number(tool->pass_depth, 'f', 2) + " mm" : "—"));
            table->setItem(i, 10, new QTableWidgetItem(QString::fromStdString(cam::bridge_summary(op.job))));
            table->setItem(i, 11, new QTableWidgetItem(QString::number(cut_len / 1000.0, 'f', 2) + " m"));
            table->setItem(i, 12, new QTableWidgetItem(QString("%1 min %2 s").arg(op_m).arg(op_s, 2, 10, QChar('0'))));
        }
        table->clearSelection();

        // Render Preview
        current_preview_img = make_job_layout_preview(active_operations, &active_material);
        preview_label->setPixmap(QPixmap::fromImage(current_preview_img));

        // Update G-Code Viewer
        gcode_editor->setPlainText(QString::fromStdString(active_gcode));
        const int line_count = static_cast<int>(std::count(active_gcode.begin(), active_gcode.end(), '\n') + 1);
        gcode_status_lbl->setText(QString("✓ Cópia do G-Code gravada e pronta para uso (%1 linhas geradas)").arg(line_count));
    };

    populate_active_job(job_combo->currentIndex());
    connect(job_combo, &QComboBox::currentIndexChanged, &dialog, populate_active_job);

    auto update_dimensions_and_area = [&]() {
        const double w = width_spin->value();
        const double h = height_spin->value();
        const double t = thickness_spin->value();
        active_material.width = w;
        active_material.height = h;
        active_material.thickness = t;
        active_material.material_profile = material_combo->currentText().toStdString();

        const double area_m2 = (w * h) / 1000000.0;
        area_card_val->setText(QString::number(area_m2, 'f', 3) + " m²");
        dims_card_val->setText(QString("%1 × %2 × %3 mm").arg(w, 0, 'f', 1).arg(h, 0, 'f', 1).arg(t, 0, 'f', 1));

        int cur_idx = job_combo->currentIndex();
        if (cur_idx >= 0 && cur_idx < static_cast<int>(job_history_.size())) {
            auto& rec = job_history_[static_cast<size_t>(cur_idx)];
            rec.width = w; rec.height = h; rec.thickness = t;
            rec.material_profile = active_material.material_profile;
            rec.area_m2 = std::format("{:.3f}", area_m2);
            rec.material = active_material;
        }

        current_preview_img = make_job_layout_preview(active_operations, &active_material);
        preview_label->setPixmap(QPixmap::fromImage(current_preview_img));
    };

    QObject::connect(width_spin, &QDoubleSpinBox::valueChanged, &dialog, update_dimensions_and_area);
    QObject::connect(height_spin, &QDoubleSpinBox::valueChanged, &dialog, update_dimensions_and_area);
    QObject::connect(thickness_spin, &QDoubleSpinBox::valueChanged, &dialog, update_dimensions_and_area);
    QObject::connect(material_combo, &QComboBox::currentTextChanged, &dialog, update_dimensions_and_area);

    connect(copy_img_btn, &QPushButton::clicked, &dialog, [&] {
        if (!current_preview_img.isNull()) {
            QGuiApplication::clipboard()->setImage(current_preview_img);
            QMessageBox::information(&dialog, "Ilustração Copiada", "Ilustração técnica copiada para a área de transferência com sucesso!");
        }
    });

    connect(save_img_btn, &QPushButton::clicked, &dialog, [&] {
        if (current_preview_img.isNull()) return;
        QString default_filename = QString::fromStdString(document_.title().empty() ? "Ilustracao_Percurso" : document_.title()) + "_Preview.png";
        QString save_path = QFileDialog::getSaveFileName(&dialog, "Salvar Ilustração Técnica em HD", default_filename, "Imagem PNG (*.png)");
        if (!save_path.isEmpty()) {
            if (QFileInfo(save_path).suffix().isEmpty()) save_path += ".png";
            current_preview_img.save(save_path, "PNG");
            QMessageBox::information(&dialog, "Ilustração Salva", QString("Ilustração técnica salva com sucesso:\n%1").arg(save_path));
        }
    });

    connect(choose_logo, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog, "Selecionar logo", logo_path,
            "Imagens PNG ou JPG (*.png *.jpg *.jpeg)");
        if (selected.isEmpty()) return;
        QImageReader reader(selected);
        QImage loaded = reader.read();
        if (loaded.isNull()) {
            QMessageBox::warning(&dialog, "Logo inválido", "Não foi possível ler esta imagem.");
            return;
        }
        logo_path = selected;
        logo_image = std::move(loaded);
        preferences.setValue("jobSheet/logoPath", logo_path);
        logo_status->setText(QFileInfo(logo_path).fileName());
        logo_badge->setText(QFileInfo(logo_path).fileName());
    });
    connect(remove_logo, &QPushButton::clicked, &dialog, [&] {
        logo_path.clear();
        logo_image = {};
        preferences.remove("jobSheet/logoPath");
        logo_status->setText("Logo UNY CV padrão");
        logo_badge->setText("UNY CV");
    });

    // G-Code actions (copy and save)
    auto save_current_gcode = [&]() {
        if (active_gcode.empty()) {
            QMessageBox::information(&dialog, "Sem G-Code", "Nenhum G-Code gerado para salvar.");
            return;
        }
        QString default_name = (part_name->text().isEmpty() ? "Percurso" : part_name->text().trimmed()) + ".nc";
        QString save_path = QFileDialog::getSaveFileName(&dialog, "Salvar Cópia do G-Code", default_name,
                                                        "G-Code (*.nc *.tap *.gcode);;Todos os arquivos (*)");
        if (save_path.isEmpty()) return;
        QFile file(save_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::critical(&dialog, "Erro ao salvar G-Code", file.errorString());
            return;
        }
        file.write(QByteArray::fromStdString(active_gcode));
        file.close();
        statusBar()->showMessage(QString("Cópia do G-Code salva: %1").arg(save_path));
        QMessageBox::information(&dialog, "G-Code Salvo", QString("G-Code salvo com sucesso em:\n%1").arg(save_path));
    };

    connect(save_gcode_btn, &QPushButton::clicked, &dialog, save_current_gcode);
    connect(copy_gcode_btn, &QPushButton::clicked, &dialog, [&] {
        QGuiApplication::clipboard()->setText(QString::fromStdString(active_gcode));
        QMessageBox::information(&dialog, "G-Code Copiado", "O código G-Code foi copiado para a área de transferência.");
    });

    // Bottom Action Buttons Bar
    auto* bottom_bar = new QWidget(&dialog);
    auto* bottom_layout = new QHBoxLayout(bottom_bar);
    bottom_layout->setContentsMargins(0, 4, 0, 0);

    auto* hint_label = new QLabel("Dica: Você pode continuar acumulando percursos; o PDF de OS é limitado a uma folha.", bottom_bar);
    hint_label->setObjectName("MutedText");
    bottom_layout->addWidget(hint_label, 1);

    auto* close_btn = new QPushButton("Fechar", bottom_bar);
    close_btn->setMinimumHeight(38);
    close_btn->setMinimumWidth(80);

    auto* save_gcode_quick_btn = new QPushButton("💾 Salvar G-Code", bottom_bar);
    save_gcode_quick_btn->setMinimumHeight(38);
    save_gcode_quick_btn->setToolTip("Salvar o arquivo de corte .nc deste percurso para usar no futuro");

    auto* gen_pdf_btn = new QPushButton("📄 GERAR PDF DA OS", bottom_bar);
    gen_pdf_btn->setProperty("primary", true);
    gen_pdf_btn->setIcon(ui::get_icon("tp_sheet"));
    gen_pdf_btn->setIconSize(QSize(20, 20));
    gen_pdf_btn->setMinimumHeight(40);
    gen_pdf_btn->setMinimumWidth(180);
    gen_pdf_btn->setStyleSheet("font-weight: bold; font-size: 9.5pt;");

    QPushButton* gen_all_pdf_btn = nullptr;
    if (job_history_.size() > 1) {
        gen_all_pdf_btn = new QPushButton("📚 GERAR 1ª OS (PDF DE 1 FOLHA)", bottom_bar);
        gen_all_pdf_btn->setIcon(ui::get_icon("tp_sheet"));
        gen_all_pdf_btn->setIconSize(QSize(20, 20));
        gen_all_pdf_btn->setMinimumHeight(40);
        gen_all_pdf_btn->setMinimumWidth(230);
        gen_all_pdf_btn->setStyleSheet("font-weight: bold; font-size: 9.5pt; color: #166534; background: #dcfce7; border: 1px solid #86efac;");
        gen_all_pdf_btn->setToolTip("Exporta somente a primeira Ordem de Serviço acumulada. O PDF é limitado a uma folha A4 horizontal.");
    }

    bottom_layout->addWidget(close_btn);
    bottom_layout->addWidget(save_gcode_quick_btn);
    bottom_layout->addWidget(gen_pdf_btn);
    if (gen_all_pdf_btn) bottom_layout->addWidget(gen_all_pdf_btn);
    main_layout->addWidget(bottom_bar);

    connect(close_btn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save_gcode_quick_btn, &QPushButton::clicked, &dialog, save_current_gcode);

    // Generate Single Sheet PDF (Fazer na hora)
    connect(gen_pdf_btn, &QPushButton::clicked, &dialog, [&] {
        cam::SetupSheetInfo info;
        info.customer = customer->text().toStdString();
        info.contact = contact->text().toStdString();
        info.order_number = order_number->text().toStdString();
        info.part_name = part_name->text().toStdString();
        info.revision = revision->text().toStdString();
        info.operator_name = operator_name->text().toStdString();
        info.notes = notes->toPlainText().toStdString();
        info.material_type = material_combo->currentText().toStdString();

        const double w = width_spin->value();
        const double h = height_spin->value();
        const double t = thickness_spin->value();
        const double area_val = (w * h) / 1000000.0;
        info.area_m2 = std::format("{:.3f}", area_val);

        cad::MaterialSetup sheet_mat = active_material;
        sheet_mat.width = w;
        sheet_mat.height = h;
        sheet_mat.thickness = t;
        sheet_mat.material_profile = info.material_type;

        document_.material = sheet_mat;
        document_.modified = true;

        if (!logo_image.isNull()) info.logo_source = "job-logo";

        QString default_filename = (info.part_name.empty()
                                        ? "Ordem_de_Servico"
                                        : QString::fromStdString(info.part_name)) + "_OS.pdf";
        QString save_path = QFileDialog::getSaveFileName(
            &dialog, "Salvar Ordem de Serviço em PDF (A4)",
            default_filename, "PDF (*.pdf)");
        if (save_path.isEmpty()) return;
        if (QFileInfo(save_path).suffix().isEmpty()) save_path += ".pdf";

        QImage layout_img = make_job_layout_preview(active_operations, &sheet_mat);
        QString pdf_error;
        if (!cam::write_job_setup_sheet_pdf(save_path, active_operations, sheet_mat, document_.title(), info,
                                            layout_img, logo_image, {}, &pdf_error)) {
            QMessageBox::critical(&dialog, "Erro ao gerar PDF", pdf_error);
            return;
        }
        statusBar()->showMessage(QString("Ordem de serviço PDF criada: %1").arg(save_path));
        QMessageBox::information(&dialog, "PDF da OS Gerado",
            QString("Ordem de Serviço salva em uma folha A4 horizontal:\n%1")
                .arg(save_path));
        dialog.accept();
    });

    // Export only the first accumulated OS; the PDF contract is one page.
    if (gen_all_pdf_btn) {
        connect(gen_all_pdf_btn, &QPushButton::clicked, &dialog, [&] {
            std::vector<cam::MultiJobSheetEntry> entries;
            for (const auto& rec : job_history_) {
                cam::MultiJobSheetEntry entry;
                entry.operations = rec.operations;
                entry.material = rec.material;
                entry.job_name = rec.name;
                entry.info = rec.info;
                entry.info.customer = customer->text().toStdString();
                entry.info.contact = contact->text().toStdString();
                entry.info.order_number = order_number->text().toStdString();
                entry.info.operator_name = operator_name->text().toStdString();
                entry.info.revision = revision->text().toStdString();
                entry.info.notes = notes->toPlainText().toStdString();
                if (!logo_image.isNull()) entry.info.logo_source = "job-logo";
                entry.layout_preview = make_job_layout_preview(rec.operations, &rec.material);
                entries.push_back(std::move(entry));
            }

            QString default_filename = QString::fromStdString(document_.title().empty() ? "Ordem_de_Servico" : document_.title()) + "_OS.pdf";
            QString save_path = QFileDialog::getSaveFileName(
                &dialog, "Salvar a Primeira Ordem de Serviço em PDF (1 folha)",
                default_filename, "PDF (*.pdf)");
            if (save_path.isEmpty()) return;
            if (QFileInfo(save_path).suffix().isEmpty()) save_path += ".pdf";

            QString pdf_error;
            if (!cam::write_multi_job_setup_sheet_pdf(save_path, entries, logo_image, &pdf_error)) {
                QMessageBox::critical(&dialog, "Erro ao gerar PDF", pdf_error);
                return;
            }
            statusBar()->showMessage(QString("PDF de uma folha salvo: %1").arg(save_path));
            QMessageBox::information(&dialog, "PDF da Primeira OS Gerado",
                QString("A primeira Ordem de Serviço foi salva em uma única folha A4 horizontal:\n%1")
                    .arg(save_path));
            dialog.accept();
        });
    }

    dialog.exec();
}

void MainWindow::export_svg() {
    const QString path = QFileDialog::getSaveFileName(this, "Exportar SVG", {},
                                                       "SVG (*.svg);;Todos os arquivos (*)");
    if (path.isEmpty()) return;
    if (!io::export_svg(std::filesystem::path(path.toStdWString()), document_)) {
        QMessageBox::critical(this, "Erro ao exportar SVG", "Não foi possível gravar o arquivo.");
        return;
    }
    statusBar()->showMessage(QString("SVG exportado: %1").arg(path));
}

void MainWindow::import_files(const QStringList& paths) {
    if (paths.isEmpty()) return;
    if (paths.size() == 1) {
        import_file(paths.first());
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::vector<cad::EntityPtr> all_entities;
    int success_count = 0;
    QStringList failed_files;

    for (const QString& path : paths) {
        if (path.isEmpty()) continue;
        try {
            auto entities = io::import_file(std::filesystem::path(path.toStdWString()));
            if (!entities.empty()) {
                if (auto label = io::create_filename_label(QFileInfo(path).fileName().toStdString(), entities)) {
                    if (!document_.get_layer(label->layer)) {
                        auto& lay = document_.add_layer(label->layer);
                        lay.color = {0.0, 0.0, 0.0};
                    }
                    entities.push_back(label);
                }
                all_entities.insert(all_entities.end(), entities.begin(), entities.end());
                ++success_count;
            }
        } catch (const std::exception& error) {
            failed_files.append(QFileInfo(path).fileName() + ": " + QString::fromUtf8(error.what()));
        }
    }

    if (!all_entities.empty()) {
        tool_context_.add(all_entities, QString("Importar %1 arquivos").arg(success_count).toStdString(), true);
        refresh_document_view();
        canvas_widget_->canvas->fit_view();
    }
    QApplication::restoreOverrideCursor();

    if (!failed_files.isEmpty()) {
        QMessageBox::warning(this, "Avisos na importação",
                             QString("%1 arquivo(s) importado(s) com sucesso.\nFalhas:\n%2")
                                 .arg(success_count)
                                 .arg(failed_files.join("\n")));
    } else if (success_count > 0) {
        statusBar()->showMessage(QString("%1 arquivo(s) importado(s) com %2 vetores")
                                     .arg(success_count)
                                     .arg(static_cast<qlonglong>(all_entities.size())));
    }
}

void MainWindow::import_file(const QString& path) {
    if (path.isEmpty()) return;
    try {
        auto entities = io::import_file(std::filesystem::path(path.toStdWString()));
        if (entities.empty()) {
            QMessageBox::information(this, "Importar", "Nenhum vetor encontrado no arquivo.");
            return;
        }

        if (auto label = io::create_filename_label(QFileInfo(path).fileName().toStdString(), entities)) {
            if (!document_.get_layer(label->layer)) {
                auto& lay = document_.add_layer(label->layer);
                lay.color = {0.0, 0.0, 0.0};
            }
            entities.push_back(label);
        }

        const QString suffix = QFileInfo(path).suffix().toUpper();
        tool_context_.add(entities, QString("Importar %1").arg(suffix).toStdString(), true);
        refresh_document_view();
        canvas_widget_->canvas->fit_view();
        statusBar()->showMessage(QString("%1 vetor(es) importado(s) de %2")
                                     .arg(static_cast<qlonglong>(entities.size()))
                                     .arg(QFileInfo(path).fileName()));
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Erro na importação", QString::fromUtf8(error.what()));
    }
}

void MainWindow::import_folder() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Importar pasta em lote");
    if (folder.isEmpty()) return;
    try {
        const auto result = io::import_folder(std::filesystem::path(folder.toStdWString()));
        if (result.imported == 0) {
            QString message = QString("Nenhum arquivo importado de %1 encontrado(s).")
                                  .arg(static_cast<qlonglong>(result.total_files));
            if (!result.failed.empty()) {
                message += "\n\nErros:\n";
                for (const auto& error : result.failed) message += QString::fromStdString(error) + "\n";
            }
            QMessageBox::information(this, "Importação em lote", message);
            return;
        }

        std::vector<cad::EntityPtr> entities;
        for (const auto& [layer, layer_entities] : result.entities_by_file) {
            if (!document_.get_layer(layer)) document_.add_layer(layer);
            entities.insert(entities.end(), layer_entities.begin(), layer_entities.end());
            if (auto label = io::create_filename_label(layer, layer_entities, layer)) {
                entities.push_back(label);
            }
        }
        tool_context_.add(entities, "Importar em lote", true);
        refresh_document_view();
        canvas_widget_->canvas->fit_view();
        QString status = QString("Importados %1 arquivo(s) de %2")
                             .arg(static_cast<qlonglong>(result.imported))
                             .arg(static_cast<qlonglong>(result.total_files));
        if (!result.failed.empty()) status += QString(" · %1 com erro").arg(static_cast<qlonglong>(result.failed.size()));
        statusBar()->showMessage(status);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Erro na importação em lote", QString::fromUtf8(error.what()));
    }
}

void MainWindow::create_tool_panel() {
    auto* dock = new QDockWidget("Ferramentas", this);
    dock->setObjectName("ToolPanel");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(212);

    auto* panel = new QWidget(dock);
    panel->setMinimumWidth(0);
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(panel);
    header->setObjectName("BrandHeader");
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(16, 12, 16, 12);
    header_layout->setSpacing(11);
    auto* logo = new QLabel(header);
    logo->setObjectName("BrandLogo");
    logo->setFixedSize(128, 37);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(":/assets/cnc-cam-logo.png"));
    header_layout->addWidget(logo, 1);
    auto* mode = new QToolButton(header);
    mode->setObjectName("ModeBadge");
    mode->setText("2D / 3D");
    mode->setCursor(Qt::PointingHandCursor);
    mode->setToolTip("Alternar entre as vistas 2D e 3D");
    connect(mode, &QToolButton::clicked, this, [this] {
        if (view_tabs_) view_tabs_->setCurrentIndex(view_tabs_->currentIndex() == 0 ? 1 : 0);
    });
    header_layout->addWidget(mode, 0, Qt::AlignVCenter);
    root->addWidget(header);

    auto* scroll = new QScrollArea(panel);
    scroll->setObjectName("ToolPanelScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    body->setObjectName("ToolPanelBody");
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(10, 8, 10, 12);
    layout->setSpacing(3);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    auto* group = new QButtonGroup(body);
    group->setExclusive(true);
    const std::map<QString, QString> tool_tips = {
        {"import", "Importar vetores (PDF/DXF/SVG)"},
        {"export", "Exportar desenho em SVG"},
        {"guide_gallery", "Abrir a galeria dos 10 desenhos interativos"},
        {"preview_3d", "Abrir prévia 3D"},
        {"measure", "Medir / Inspecionar"},
        {"grid", "Mostrar ou ocultar a grade 2D"},
        {"workspace_setup", "Configurar material e área de trabalho"},
        {"select", "Seleção"},
        {"circle", "Círculo"}, {"ellipse", "Elipse"}, {"rectangle", "Retângulo"},
        {"polygon", "Polígono"}, {"star", "Estrela"}, {"polyline", "Polilinha"},
        {"text", "Texto"}, {"line", "Linha"}, {"arc", "Arco"},
        {"construction_line", "Linha de construção em dois cliques"},
        {"freehand", "Desenho livre"}, {"drill_point", "Adicionar ponto de furação"},
        {"vector_texture", "Textura vetorial"}, {"text_on_curve", "Texto em curva"},
        {"dimension", "Dimensões / Cotas"}, {"trace_bitmap", "Vetorizar bitmap"},
        {"move", "Mover seleção"}, {"distort", "Distorcer"}, {"mirror", "Espelhar"},
        {"scale", "Redimensionar"}, {"array_copy", "Cópia em array"},
        {"circular_copy", "Cópia circular"}, {"rotate", "Rotacionar"},
        {"node_edit", "Edição de nós"}, {"fillet", "Filete / Arredondamento"},
        {"trim", "Aparar objetos"}, {"interactive_trim", "Aparar interativo"},
        {"extend", "Estender vetores"}, {"fit_curves", "Ajustar curvas"},
        {"join", "Unir vetores abertos"}, {"weld", "Soldar vetores"},
        {"subtract", "Subtrair vetores"}, {"intersect", "Interseção de vetores"},
        {"group", "Agrupar (Ctrl+G)"}, {"ungroup", "Desagrupar (Ctrl+U)"},
        {"offset", "Offset de vetores"}, {"boundary", "Contorno vetorial"},
        {"convert_text", "Converter texto em curvas"},
        {"align_left", "Alinhar à esquerda"},
        {"align_center_h", "Centralizar horizontalmente"},
        {"align_right", "Alinhar à direita"}, {"align_top", "Alinhar ao topo"},
        {"align_center_v", "Centralizar verticalmente"},
        {"align_bottom", "Alinhar à base"},
        {"layout_optimizer", "Organizar material: otimizar e encaixar as peças"},
    };
    const auto add_separator = [&] {
        auto* sep = new QFrame(body);
        sep->setObjectName("ToolSep");
        sep->setFrameShape(QFrame::NoFrame);
        sep->setFixedHeight(1);
        layout->addWidget(sep);
    };
    const auto add_section = [&](const QString& title,
                                 const std::vector<std::pair<QString, QString>>& entries,
                                 int columns) {
        auto* section = new QLabel(title, body);
        section->setObjectName("SectionHeader");
        layout->addWidget(section);
        Q_UNUSED(columns);
        auto* grid = new ui::FlowLayout(3);
        grid->setContentsMargins(0, 2, 0, 4);
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& [name, label] = entries[i];
            auto* tile = new ToolTileWidget(body);
            tile->setObjectName("ToolTile");
            tile->setFixedWidth(38);
            auto* tile_layout = new QVBoxLayout(tile);
            tile_layout->setContentsMargins(0, 0, 0, 0);
            tile_layout->setSpacing(2);
            auto* button = new QToolButton(tile);
            button->setObjectName("ToolBtn");
            button->setFixedSize(34, 34);
            button->setIcon(ui::get_icon(name == "guide_gallery" ? "tp_preview" : name));
            button->setIconSize(QSize(28, 28));
            const auto tip = tool_tips.find(name);
            const QString description = tip != tool_tips.end() ? tip->second : label;
            button->setToolTip(description);
            button->setAccessibleName(description);
            button->setCursor(Qt::PointingHandCursor);
            const bool direct_action = name == "import" || name == "export" ||
                                       name == "preview_3d" || name == "grid" ||
                                       name == "workspace_setup" || name == "guide_gallery";
            button->setCheckable(!direct_action || name == "grid");
            button->setProperty("toolName", name);
            if (!direct_action) group->addButton(button);
            tile->set_button(button);
            tile_layout->addWidget(button, 0, Qt::AlignHCenter);
            auto* caption = new QLabel(label, tile);
            caption->setObjectName("ToolCaption");
            caption->setStyleSheet("font-size: 9px;");
            caption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            caption->setWordWrap(true);
            caption->setAttribute(Qt::WA_TransparentForMouseEvents);
            caption->setVisible(!label.isEmpty());
            tile_layout->addWidget(caption);
            grid->addWidget(tile);
            connect(button, &QToolButton::clicked, this, [this, name, button] {
                if (name == "import") {
                    const QStringList paths = QFileDialog::getOpenFileNames(
                        this, "Importar vetores", {}, QString::fromUtf8(io::kImportFilter));
                    if (!paths.isEmpty()) import_files(paths);
                }
                else if (name == "export") export_svg();
                else if (name == "preview_3d") view_tabs_->setCurrentIndex(1);
                else if (name == "grid") {
                    canvas_widget_->canvas->set_grid_visible(!canvas_widget_->canvas->show_grid);
                    button->setChecked(canvas_widget_->canvas->show_grid);
                }
                else if (name == "workspace_setup") edit_material_setup();
                else if (name == "guide_gallery") show_guides_gallery();
                else if (name == "zoom_fit") canvas_widget_->canvas->fit_view();
                else if (name == "zoom_in") canvas_widget_->canvas->zoom_by(1.25);
                else if (name == "zoom_out") canvas_widget_->canvas->zoom_by(0.8);
                else if (name == "pan") {
                    canvas_widget_->canvas->setCursor(Qt::OpenHandCursor);
                    statusBar()->showMessage("Arraste com o botão do meio do mouse para mover a vista");
                }
                else if (name != "pan") activate_tool(name);
            });
        }
        layout->addLayout(grid);
    };

    add_section("ARQUIVO", {
        {"import", "Importar"}, {"export", "Exportar"},
        {"workspace_setup", "Area de trabalho"}}, 4);
    add_separator();
    add_section("CONTROLE DE VISTA 2D", {
        {"pan", "Mover vista"}, {"measure", "Medir"}, {"grid", "Grade"},
        {"select", "Selecao"}, {"preview_3d", "Vista 3D"},
        {"guide_gallery", "Guias"}}, 6);
    add_separator();

    auto* layer_row = new QVBoxLayout();
    layer_row->setContentsMargins(0, 0, 0, 0);
    layer_row->setSpacing(10);
    auto* layer_title = new QLabel("CAMADAS", body);
    layer_title->setObjectName("SectionHeader");
    layer_row->addWidget(layer_title);
    auto* layer_bar = new QFrame(body);
    layer_bar->setObjectName("LayerBar");
    auto* layer_layout = new QHBoxLayout(layer_bar);
    layer_layout->setContentsMargins(8, 0, 6, 0);
    layer_layout->setSpacing(6);
    auto* layer_manager = new QToolButton(layer_bar);
    layer_manager->setObjectName("LayerToggle");
    layer_manager->setIcon(ui::get_icon("layers"));
    layer_manager->setIconSize(QSize(18, 18));
    layer_manager->setToolTip("Gerenciar nomes, cores e estados das camadas");
    layer_manager->setAccessibleName("Gerenciar camadas");
    layer_manager->setCursor(Qt::PointingHandCursor);
    layer_layout->addWidget(layer_manager);
    layer_combo_ = new QComboBox(layer_bar);
    layer_combo_->setObjectName("LayerCombo");
    layer_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layer_combo_->setMinimumContentsLength(3);
    layer_combo_->setMinimumWidth(30);
    layer_layout->addWidget(layer_combo_, 1);
    auto* layer_lock = new QToolButton(layer_bar);
    layer_lock->setObjectName("LayerToggle");
    layer_lock->setCheckable(true);
    layer_lock->setIcon(ui::get_icon("lock_open"));
    layer_lock->setIconSize(QSize(18, 18));
    layer_lock->setToolTip("Travar a camada atual");
    layer_locked_ = layer_lock;
    layer_layout->addWidget(layer_lock);
    auto* layer_visible = new QToolButton(layer_bar);
    layer_visible->setObjectName("LayerToggle");
    layer_visible->setCheckable(true);
    layer_visible->setChecked(true);
    layer_visible->setIcon(ui::get_icon("eye"));
    layer_visible->setIconSize(QSize(18, 18));
    layer_visible->setToolTip("Esconder a camada atual");
    layer_visible_ = layer_visible;
    layer_layout->addWidget(layer_visible);
    auto* new_layer = new QToolButton(layer_bar);
    new_layer->setObjectName("LayerToggle");
    new_layer->setText("+");
    new_layer->setToolTip("Criar nova camada");
    new_layer->setCursor(Qt::PointingHandCursor);
    new_layer->setFixedSize(24, 24);
    layer_layout->addWidget(new_layer);
    auto* remove_layer = new QToolButton(layer_bar);
    remove_layer->setObjectName("LayerToggle");
    remove_layer->setText("−");
    remove_layer->setToolTip("Excluir camada atual");
    remove_layer->setCursor(Qt::PointingHandCursor);
    remove_layer->setFixedSize(24, 24);
    layer_layout->addWidget(remove_layer);
    layer_row->addWidget(layer_bar, 1);
    layout->addLayout(layer_row);
    add_separator();

    add_section("CRIAR VETORES", {
        {"circle", ""}, {"ellipse", ""}, {"rectangle", ""}, {"polygon", ""},
        {"star", ""}, {"polyline", "Polilinha"}, {"text", "Texto"}, {"line", ""},
        {"arc", ""}, {"construction_line", ""}, {"freehand", "Mao livre"},
        {"drill_point", "Furacao"}, {"vector_texture", "Textura"}, {"text_on_curve", ""},
        {"dimension", "Cotas"}, {"trace_bitmap", "Vetorizar"}}, 6);
    add_separator();
    add_section("TRANSFORMAR OBJETOS", {
        {"move", ""}, {"distort", "Distorcer"}, {"mirror", "Espelhar"},
        {"scale", ""}, {"rotate", ""}}, 5);
    add_separator();
    add_section("EDITAR OBJETOS", {
        {"node_edit", "Nos"}, {"fillet", "Filete"}, {"trim", "Aparar"},
        {"interactive_trim", "Aparar interativo"}, {"extend", ""}, {"fit_curves", "Ajustar curvas"},
        {"weld", "Soldar"}, {"subtract", ""}, {"intersect", ""},
        {"join", "Unir"}, {"group", "Agrupar"}, {"ungroup", ""},
        {"convert_text", "Texto"}}, 6);
    add_separator();
    add_section("OFFSET E LAYOUT", {
        {"offset", "Offset"}, {"boundary", "Contorno"}, {"array_copy", "Copia array"},
        {"circular_copy", "Copia circular"}, {"layout_optimizer", "Organizar"},
        {"align_left", ""}, {"align_center_h", ""}, {"align_right", ""},
        {"align_top", ""}, {"align_center_v", ""}, {"align_bottom", ""}}, 6);
    layout->addStretch();

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(layer_combo_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (name.isEmpty()) return;
        tool_context_.current_layer = name.toStdString();
        const cad::Layer* layer = document_.get_layer(tool_context_.current_layer);
        if (!layer) return;
        QSignalBlocker visible_blocker(layer_visible_);
        QSignalBlocker locked_blocker(layer_locked_);
        layer_visible_->setChecked(layer->visible);
        layer_locked_->setChecked(layer->locked);
        layer_visible_->setIcon(ui::get_icon(layer->visible ? "eye" : "eye_closed"));
        layer_locked_->setIcon(ui::get_icon(layer->locked ? "lock" : "lock_open"));
        layer_visible_->setToolTip(layer->visible ? "Esconder a camada atual" : "Mostrar a camada atual");
        layer_locked_->setToolTip(layer->locked ? "Destravar a camada atual" : "Travar a camada atual");
        canvas_widget_->canvas->refresh_entities();
    });
    connect(layer_visible_, &QAbstractButton::toggled, this, [this](bool visible) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->visible = visible;
            layer_visible_->setIcon(ui::get_icon(visible ? "eye" : "eye_closed"));
            layer_visible_->setToolTip(visible ? "Esconder a camada atual" : "Mostrar a camada atual");
            if (!visible) {
                for (const auto& entity : document_.entities_on_layer(layer->name))
                    document_.set_selected(entity, false);
            }
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
            statusBar()->showMessage(QString("Camada %1 %2")
                                         .arg(QString::fromStdString(layer->name),
                                              visible ? "visível" : "escondida"));
        }
    });
    connect(layer_locked_, &QAbstractButton::toggled, this, [this](bool locked) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->locked = locked;
            layer_locked_->setIcon(ui::get_icon(locked ? "lock" : "lock_open"));
            layer_locked_->setToolTip(locked ? "Destravar a camada atual" : "Travar a camada atual");
            if (locked) {
                for (const auto& entity : document_.entities_on_layer(layer->name))
                    document_.set_selected(entity, false);
            }
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
            statusBar()->showMessage(QString("Camada %1 %2")
                                         .arg(QString::fromStdString(layer->name),
                                              locked ? "travada" : "destravada"));
        }
    });
    connect(layer_manager, &QToolButton::clicked, this, &MainWindow::manage_layers);
    connect(new_layer, &QToolButton::clicked, this, &MainWindow::add_layer_from_panel);
    connect(remove_layer, &QToolButton::clicked, this, &MainWindow::remove_current_layer);
    connect(canvas_widget_->canvas, &vp::Canvas2D::shortcut_requested, this,
            [this](const QString& name) {
                if (name == "zoom_fit") canvas_widget_->canvas->fit_view();
                else activate_tool(name);
            });
    connect(canvas_widget_->canvas, &vp::Canvas2D::cancel_requested, this,
            [this] { activate_tool("select"); });
    refresh_layers();
}

void MainWindow::create_legacy_tool_panel() {
    auto* dock = new QDockWidget("Ferramentas", this);
    dock->setObjectName("ToolPanel");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(320);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 12);
    layout->setSpacing(3);

    auto* title = new QLabel("DESENHO", panel);
    title->setObjectName("SectionHeader");
    layout->addWidget(title);

    auto* group = new QButtonGroup(panel);
    group->setExclusive(true);
    const std::vector<std::pair<QString, QString>> buttons = {
        {"select", "Seleção"}, {"line", "Linha"}, {"polyline", "Polilinha"},
        {"rectangle", "Retângulo"}, {"circle", "Círculo"}, {"ellipse", "Elipse"},
        {"arc", "Arco"}, {"drill_point", "Ponto de furação"}, {"node_edit", "Edição de nós"},
    };
    for (const auto& [name, label] : buttons) {
        auto* button = new QPushButton(label, panel);
        button->setObjectName("ToolBtn");
        button->setCheckable(true);
        button->setProperty("toolName", name);
        group->addButton(button);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name] { activate_tool(name); });
    }

    layout->addSpacing(10);
    auto* transform_title = new QLabel("TRANSFORMAÇÃO", panel);
    transform_title->setObjectName("SectionHeader");
    layout->addWidget(transform_title);
    const std::vector<std::pair<QString, QString>> transforms = {
        {"move", "Mover"}, {"rotate", "Rotacionar"}, {"scale", "Redimensionar"},
        {"mirror", "Espelhar"},
    };
    for (const auto& [name, label] : transforms) {
        auto* button = new QPushButton(label, panel);
        button->setObjectName("ToolBtn");
        button->setCheckable(true);
        button->setProperty("toolName", name);
        group->addButton(button);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name] { activate_tool(name); });
    }

    layout->addSpacing(10);
    auto* layer_title = new QLabel("CAMADA ATUAL", panel);
    layer_title->setObjectName("SectionHeader");
    layout->addWidget(layer_title);
    layer_combo_ = new QComboBox(panel);
    layer_combo_->setObjectName("LayerCombo");
    layout->addWidget(layer_combo_);

    layer_visible_ = new QCheckBox("Visível", panel);
    layer_locked_ = new QCheckBox("Bloqueada", panel);
    layout->addWidget(layer_visible_);
    layout->addWidget(layer_locked_);
    auto* layer_row = new QHBoxLayout();
    auto* new_layer = new QPushButton("Nova", panel);
    auto* remove_layer = new QPushButton("Excluir", panel);
    layer_row->addWidget(new_layer);
    layer_row->addWidget(remove_layer);
    layout->addLayout(layer_row);

    connect(layer_combo_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (name.isEmpty()) return;
        tool_context_.current_layer = name.toStdString();
        const cad::Layer* layer = document_.get_layer(tool_context_.current_layer);
        if (!layer) return;
        {
            QSignalBlocker visible_blocker(layer_visible_);
            QSignalBlocker locked_blocker(layer_locked_);
            layer_visible_->setChecked(layer->visible);
            layer_locked_->setChecked(layer->locked);
        }
        canvas_widget_->canvas->refresh_entities();
    });
    connect(layer_visible_, &QCheckBox::toggled, this, [this](bool visible) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->visible = visible;
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
        }
    });
    connect(layer_locked_, &QCheckBox::toggled, this, [this](bool locked) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->locked = locked;
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
        }
    });
    connect(new_layer, &QPushButton::clicked, this, &MainWindow::add_layer_from_panel);
    connect(remove_layer, &QPushButton::clicked, this, &MainWindow::remove_current_layer);

    layout->addStretch();
    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(canvas_widget_->canvas, &vp::Canvas2D::shortcut_requested, this,
            [this](const QString& name) {
                if (name == "zoom_fit") canvas_widget_->canvas->fit_view();
                else activate_tool(name);
            });
    connect(canvas_widget_->canvas, &vp::Canvas2D::cancel_requested, this,
            [this] { activate_tool("select"); });
    refresh_layers();
}

void MainWindow::refresh_layers() {
    if (!layer_combo_) return;
    QSignalBlocker blocker(layer_combo_);
    layer_combo_->clear();
    int selected = 0;
    for (size_t i = 0; i < document_.layers.size(); ++i) {
        const auto& layer = document_.layers[i];
        layer_combo_->addItem(QString::fromStdString(layer.name));
        if (layer.name == tool_context_.current_layer) selected = static_cast<int>(i);
    }
    if (layer_combo_->count() == 0) return;
    layer_combo_->setCurrentIndex(selected);
    tool_context_.current_layer = layer_combo_->currentText().toStdString();
    const cad::Layer* layer = document_.get_layer(tool_context_.current_layer);
    if (layer) {
        QSignalBlocker visible_blocker(layer_visible_);
        QSignalBlocker locked_blocker(layer_locked_);
        layer_visible_->setChecked(layer->visible);
        layer_locked_->setChecked(layer->locked);
        layer_visible_->setIcon(ui::get_icon(layer->visible ? "eye" : "eye_closed"));
        layer_locked_->setIcon(ui::get_icon(layer->locked ? "lock" : "lock_open"));
        layer_visible_->setToolTip(layer->visible ? "Esconder a camada atual" : "Mostrar a camada atual");
        layer_locked_->setToolTip(layer->locked ? "Destravar a camada atual" : "Travar a camada atual");
    }
}

void MainWindow::add_layer_from_panel() {
    int number = static_cast<int>(document_.layers.size()) + 1;
    std::string name;
    do {
        name = "Camada " + std::to_string(number++);
    } while (document_.get_layer(name));
    document_.add_layer(name);
    tool_context_.current_layer = name;
    refresh_layers();
    canvas_widget_->canvas->refresh_entities();
    statusBar()->showMessage(QString("Camada criada: %1").arg(QString::fromStdString(name)));
}

void MainWindow::remove_current_layer() {
    if (document_.layers.size() <= 1) {
        statusBar()->showMessage("O projeto precisa manter pelo menos uma camada");
        return;
    }
    const std::string old = tool_context_.current_layer;
    if (!document_.remove_layer(old)) return;
    tool_context_.current_layer = document_.layers.front().name;
    refresh_layers();
    canvas_widget_->canvas->refresh_entities();
}

void MainWindow::manage_layers() {
    ui::LayerManagerDialog dialog(document_.layers, tool_context_.current_layer, this);
    if (dialog.exec() != QDialog::Accepted) return;

    document_.layers = dialog.result_layers();
    if (document_.layers.empty()) document_.layers.push_back(cad::Layer{});
    const std::string fallback = document_.layers.front().name;
    for (const auto& entity : document_.entities) {
        if (!document_.get_layer(entity->layer)) entity->layer = fallback;
    }
    if (!document_.get_layer(tool_context_.current_layer)) tool_context_.current_layer = fallback;
    document_.modified = true;
    refresh_document_view();
    statusBar()->showMessage("Camadas atualizadas");
}

void MainWindow::quick_edit_entity(const QString& entity_id) {
    const cad::EntityPtr entity = document_.find_entity(entity_id.toStdString());
    if (!entity) return;
    if (!ui::QuickEditPopup::supports(*entity)) {
        statusBar()->showMessage(
            QString("%1: use Editar nós para alterar ponto a ponto")
                .arg(ui::QuickEditPopup::entity_name(*entity)));
        return;
    }
    if (quick_edit_popup_) quick_edit_popup_->close();
    const QString entity_name = ui::QuickEditPopup::entity_name(*entity);
    auto* popup = new ui::QuickEditPopup(
        entity,
        [this, entity, entity_name](ui::QuickEditPopup::Transform transform,
                                    const QString& summary) {
            tool_context_.transform({entity}, std::move(transform),
                                    QString("Editar %1").arg(entity_name).toStdString());
            refresh_toolpaths();
            QString capitalized = entity_name;
            if (!capitalized.isEmpty()) capitalized[0] = capitalized[0].toUpper();
            statusBar()->showMessage(QString("%1: %2").arg(capitalized, summary));
        },
        this);
    quick_edit_popup_ = popup;
    connect(popup, &QObject::destroyed, this, [this, popup] {
        if (quick_edit_popup_ == popup) quick_edit_popup_.clear();
    });
    popup->show_at(canvas_widget_->canvas->entity_anchor(*entity));
}

void MainWindow::show_canvas_context_menu(const QPoint& global_pos, double wx, double wy) {
    Q_UNUSED(wx);
    Q_UNUSED(wy);
    const auto selected = document_.selected_entities();
    const bool has_selection = !selected.empty();
    const auto texts = [&] {
        std::vector<cad::EntityPtr> result;
        for (const auto& entity : selected)
            if (cad::is_a<cad::Text>(entity)) result.push_back(entity);
        return result;
    }();
    bool has_text_block = false;
    bool has_group = false;
    bool has_open_polyline = false;
    for (const auto& entity : selected) {
        has_group = has_group || (entity->group && !entity->group->empty());
        if (const auto* text = cad::as<cad::Text>(entity))
            has_text_block = has_text_block || text->content.find('\n') != std::string::npos;
        if (const auto* polyline = cad::as<cad::Polyline>(entity))
            has_open_polyline = has_open_polyline ||
                                (!polyline->is_closed && polyline->points.size() >= 3);
    }

    std::vector<cad::EntityPtr> guides;
    for (const auto& entity : document_.entities)
        if (cad::is_a<cad::ConstructionLine>(entity)) guides.push_back(entity);

    auto* menu = new QMenu(this);
    menu->setObjectName("CanvasContextMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    const auto action = [menu](const QString& label, const std::function<void()>& callback,
                               bool enabled = true, const QKeySequence& shortcut = {}) {
        QAction* item = menu->addAction(label);
        item->setEnabled(enabled);
        if (!shortcut.isEmpty()) {
            item->setShortcut(shortcut);
            item->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        }
        QObject::connect(item, &QAction::triggered, menu, callback);
        return item;
    };
    action(QString("Desfazer %1").arg(QString::fromStdString(undo_stack_.undo_description())).trimmed(),
           [this] {
               if (undo_stack_.undo()) {
                   refresh_document_view();
                   statusBar()->showMessage("Acao desfeita");
               }
           }, undo_stack_.can_undo(), QKeySequence::Undo);
    action(QString("Refazer %1").arg(QString::fromStdString(undo_stack_.redo_description())).trimmed(),
           [this] {
               if (undo_stack_.redo()) {
                   refresh_document_view();
                   statusBar()->showMessage("Acao refeita");
               }
           }, undo_stack_.can_redo(), QKeySequence::Redo);
    menu->addSeparator();
    action("Cortar", [this, selected] {
        if (selected.empty()) return;
        clipboard_entities_ = tools::clone_entities(selected);
        tool_context_.remove(selected);
        statusBar()->showMessage(QString("%1 objeto(s) recortado(s)").arg(selected.size()));
    }, has_selection, QKeySequence::Cut);
    action("Copiar", [this, selected] {
        clipboard_entities_ = tools::clone_entities(selected);
        statusBar()->showMessage(QString("%1 objeto(s) copiado(s)").arg(selected.size()));
    }, has_selection, QKeySequence::Copy);
    action("Colar", [this] {
        if (clipboard_entities_.empty()) return;
        auto copies = tools::clone_entities(clipboard_entities_);
        for (const auto& entity : copies) {
            entity->translate(10.0, -10.0);
            entity->selected = true;
        }
        document_.clear_selection();
        tool_context_.add(copies, "Colar");
        statusBar()->showMessage(QString("%1 objeto(s) colado(s)").arg(copies.size()));
    }, !clipboard_entities_.empty(), QKeySequence::Paste);
    action("Eliminar", [this, selected] {
        if (!selected.empty()) {
            tool_context_.remove(selected);
            statusBar()->showMessage(QString("%1 objeto(s) excluido(s)").arg(selected.size()));
        }
    }, has_selection, QKeySequence::Delete);
    menu->addSeparator();
    action("Desmarcar tudo", [this] {
        document_.clear_selection();
        canvas_widget_->canvas->refresh_entities();
    }, has_selection);
    action("Selecionar todos os vetores", [this] {
        document_.clear_selection();
        for (const auto& entity : document_.entities) {
            if (!document_.is_editable(*entity) || cad::is_a<cad::ConstructionLine>(entity) ||
                cad::is_a<cad::Dimension>(entity) || cad::is_a<cad::DrillPoint>(entity)) continue;
            document_.set_selected(entity, true);
        }
        canvas_widget_->canvas->refresh_entities();
    }, true, QKeySequence::SelectAll);
    action("Selecionar todos os vetores abertos", [this] {
        std::vector<cad::EntityPtr> open;
        for (const auto& entity : document_.entities) {
            if (!document_.is_editable(*entity) || cad::is_a<cad::ConstructionLine>(entity) ||
                cad::is_a<cad::Dimension>(entity) || cad::is_a<cad::DrillPoint>(entity)) continue;
            if (!entity->closed()) open.push_back(entity);
        }
        document_.clear_selection();
        for (const auto& entity : open) document_.set_selected(entity, true);
        canvas_widget_->canvas->refresh_entities();
        statusBar()->showMessage(QString("%1 vetor(es) aberto(s) selecionado(s)").arg(open.size()));
    });
    action("Selecionar todos os vetores duplicados", [this] {
        using Signature = std::tuple<std::string, bool, std::vector<std::pair<long long, long long>>>;
        std::set<Signature> seen;
        std::vector<cad::EntityPtr> duplicates;
        for (const auto& entity : document_.entities) {
            if (!document_.is_editable(*entity) || cad::is_a<cad::ConstructionLine>(entity) ||
                cad::is_a<cad::Dimension>(entity) || cad::is_a<cad::DrillPoint>(entity)) continue;
            std::vector<std::pair<long long, long long>> points;
            for (const Pt& point : entity->to_points(64))
                points.emplace_back(std::llround(point.x * 100.0), std::llround(point.y * 100.0));
            std::sort(points.begin(), points.end());
            points.erase(std::unique(points.begin(), points.end()), points.end());
            Signature signature{entity->type_name(), entity->closed(), std::move(points)};
            if (!seen.insert(signature).second) duplicates.push_back(entity);
        }
        document_.clear_selection();
        for (const auto& entity : duplicates) document_.set_selected(entity, true);
        canvas_widget_->canvas->refresh_entities();
        statusBar()->showMessage(QString("%1 vetor(es) duplicado(s) selecionado(s)").arg(duplicates.size()));
    });
    action("Selecionar vetores da camada atual", [this] {
        document_.clear_selection();
        for (const auto& entity : document_.entities)
            if (entity->layer == tool_context_.current_layer && document_.is_editable(*entity))
                document_.set_selected(entity, true);
        canvas_widget_->canvas->refresh_entities();
    });
    menu->addSeparator();
    action("Ativar camada da selecao", [this, selected] {
        if (selected.empty()) return;
        tool_context_.current_layer = selected.front()->layer;
        refresh_layers();
        statusBar()->showMessage(QString("Camada atual: %1")
                                     .arg(QString::fromStdString(tool_context_.current_layer)));
    }, has_selection);
    menu->addSeparator();
    action("Agrupar objetos", [this] { activate_tool("group"); }, selected.size() >= 2,
           QKeySequence("Ctrl+G"));
    action("Desagrupar objetos", [this] { activate_tool("ungroup"); }, has_group,
           QKeySequence("Ctrl+U"));
    action("Separar bloco de texto em linhas", [this] { activate_tool("text_lines"); },
           has_text_block);
    action("Converter texto em curvas", [this] { activate_tool("convert_text"); },
           !texts.empty());
    auto* join_menu = menu->addMenu("Juntar/fechar vetores");
    join_menu->setEnabled(has_selection);
    join_menu->addAction("Unir vetores abertos (J)", this, [this] { activate_tool("join"); });
    QAction* close_action = join_menu->addAction("Fechar polilinhas selecionadas", this,
                                                 [this] { activate_tool("close_vectors"); });
    close_action->setEnabled(has_open_polyline);
    action("Modo de edicao de nos", [this] { activate_tool("node_edit"); }, true,
           QKeySequence("N"));

    auto* guides_menu = menu->addMenu("Guias");
    QAction* visible_action = guides_menu->addAction("Mostrar guias");
    visible_action->setCheckable(true);
    visible_action->setChecked(canvas_widget_->canvas->show_guides);
    connect(visible_action, &QAction::toggled, menu, [this](bool visible) {
        canvas_widget_->canvas->show_guides = visible;
        canvas_widget_->canvas->refresh_entities();
        statusBar()->showMessage(visible ? "Guias visiveis" : "Guias ocultas");
    });
    QAction* delete_guides = guides_menu->addAction("Apagar todas as guias", this, [this, guides] {
        if (!guides.empty()) tool_context_.remove(guides);
    });
    delete_guides->setEnabled(!guides.empty());
    guides_menu->setEnabled(!guides.empty());
    menu->addSeparator();
    action("Propriedades do objeto...", [this, selected] {
        if (!selected.empty()) quick_edit_entity(QString::fromStdString(selected.front()->id));
    }, has_selection && ui::QuickEditPopup::supports(*selected.front()));

    auto* copy_layer = menu->addMenu("Copiar para camada");
    auto* move_layer = menu->addMenu("Mover para camada");
    for (const cad::Layer& layer : document_.layers) {
        QAction* copy_action = copy_layer->addAction(QString::fromStdString(layer.name));
        connect(copy_action, &QAction::triggered, menu, [this, selected, name = layer.name] {
            if (selected.empty()) return;
            auto copies = tools::clone_entities(selected);
            document_.clear_selection();
            for (const auto& entity : copies) {
                entity->layer = name;
                entity->selected = true;
            }
            tool_context_.add(copies, "Copiar para camada", true);
        });
        QAction* move_action = move_layer->addAction(QString::fromStdString(layer.name));
        connect(move_action, &QAction::triggered, menu, [this, selected, name = layer.name] {
            if (selected.empty()) return;
            tool_context_.transform(selected, [name](cad::Entity& entity) { entity.layer = name; },
                                    "Mover para camada");
        });
    }
    copy_layer->setEnabled(has_selection);
    move_layer->setEnabled(has_selection);
    menu->popup(global_pos);
}

void MainWindow::activate_tool(const QString& name) {
    if (active_tool_) active_tool_->deactivate();

    if (name == "select") active_tool_ = std::make_unique<tools::SelectTool>(tool_context_);
    else if (name == "line") active_tool_ = std::make_unique<tools::LineTool>(tool_context_);
    else if (name == "polyline") active_tool_ = std::make_unique<tools::PolylineTool>(tool_context_);
    else if (name == "rectangle") active_tool_ = std::make_unique<tools::RectangleTool>(tool_context_);
    else if (name == "circle") active_tool_ = std::make_unique<tools::CircleTool>(tool_context_);
    else if (name == "ellipse") active_tool_ = std::make_unique<tools::EllipseTool>(tool_context_);
    else if (name == "polygon") active_tool_ = std::make_unique<tools::PolygonTool>(tool_context_);
    else if (name == "star") active_tool_ = std::make_unique<tools::StarTool>(tool_context_);
    else if (name == "construction_line") active_tool_ = std::make_unique<tools::ConstructionLineTool>(tool_context_);
    else if (name == "arc") active_tool_ = std::make_unique<tools::ArcTool>(tool_context_);
    else if (name == "freehand") active_tool_ = std::make_unique<tools::FreehandTool>(tool_context_);
    else if (name == "drill_point") active_tool_ = std::make_unique<tools::DrillPointTool>(tool_context_);
    else if (name == "text") active_tool_ = std::make_unique<tools::TextTool>(tool_context_);
    else if (name == "text_on_curve") active_tool_ = std::make_unique<tools::TextOnCurveTool>(tool_context_);
    else if (name == "vector_texture") active_tool_ = std::make_unique<tools::VectorTextureTool>(tool_context_);
    else if (name == "trace_bitmap") active_tool_ = std::make_unique<tools::TraceBitmapTool>(tool_context_);
    else if (name == "node_edit") active_tool_ = std::make_unique<tools::NodeEditTool>(tool_context_);
    else if (name == "measure") active_tool_ = std::make_unique<tools::MeasureTool>(tool_context_);
    else if (name == "dimension") active_tool_ = std::make_unique<tools::DimensionTool>(tool_context_);
    else if (name == "group") active_tool_ = std::make_unique<tools::GroupTool>(tool_context_);
    else if (name == "ungroup") active_tool_ = std::make_unique<tools::UngroupTool>(tool_context_);
    else if (name == "convert_text") active_tool_ = std::make_unique<tools::ConvertTextTool>(tool_context_);
    else if (name == "close_vectors") active_tool_ = std::make_unique<tools::CloseVectorsTool>(tool_context_);
    else if (name == "weld") active_tool_ = std::make_unique<tools::WeldTool>(tool_context_);
    else if (name == "subtract") active_tool_ = std::make_unique<tools::SubtractTool>(tool_context_);
    else if (name == "intersect") active_tool_ = std::make_unique<tools::IntersectTool>(tool_context_);
    else if (name == "join") active_tool_ = std::make_unique<tools::JoinTool>(tool_context_);
    else if (name == "fillet") active_tool_ = std::make_unique<tools::FilletTool>(tool_context_);
    else if (name == "trim") active_tool_ = std::make_unique<tools::TrimTool>(tool_context_);
    else if (name == "interactive_trim") active_tool_ = std::make_unique<tools::InteractiveTrimTool>(tool_context_);
    else if (name == "extend") active_tool_ = std::make_unique<tools::ExtendTool>(tool_context_);
    else if (name == "offset") active_tool_ = std::make_unique<tools::OffsetTool>(tool_context_);
    else if (name == "boundary") active_tool_ = std::make_unique<tools::BoundaryTool>(tool_context_);
    else if (name == "fit_curves") active_tool_ = std::make_unique<tools::FitCurvesTool>(tool_context_);
    else if (name == "move") active_tool_ = std::make_unique<tools::MoveTool>(tool_context_);
    else if (name == "rotate") active_tool_ = std::make_unique<tools::RotateTool>(tool_context_);
    else if (name == "scale") active_tool_ = std::make_unique<tools::ScaleTool>(tool_context_);
    else if (name == "mirror") active_tool_ = std::make_unique<tools::MirrorTool>(tool_context_);
    else if (name == "distort") active_tool_ = std::make_unique<tools::DistortTool>(tool_context_);
    else if (name == "array_copy") active_tool_ = std::make_unique<tools::ArrayCopyTool>(tool_context_);
    else if (name == "circular_copy") active_tool_ = std::make_unique<tools::CircularCopyTool>(tool_context_);
    else if (name.startsWith("align_"))
        active_tool_ = std::make_unique<tools::AlignTool>(tool_context_, name.mid(6).toStdString());
    else active_tool_ = std::make_unique<tools::SelectTool>(tool_context_);

    if (name == "star") {
        auto* star = static_cast<tools::StarTool*>(active_tool_.get());
        ui::ResponsiveDialog dialog(this);
        dialog.setWindowTitle("Estrela · desenho paramétrico");
        auto* layout = new QVBoxLayout(&dialog);
        auto* guide = new ui::MachiningGuide("10_estrela", &dialog);
        guide->model()->setValue("points", star->points);
        guide->model()->setValue("innerRatio", star->inner_ratio);
        layout->addWidget(guide, 1);
        auto* form = new QFormLayout();
        auto* points = new QSpinBox(&dialog); points->setRange(3, 32); points->setValue(star->points);
        auto* ratio = new QDoubleSpinBox(&dialog); ratio->setRange(10, 90); ratio->setSuffix(" %"); ratio->setValue(star->inner_ratio * 100);
        form->addRow("Número de pontas", points); form->addRow("Raio interno / externo", ratio); layout->addLayout(form);
        connect(points, &QSpinBox::valueChanged, guide, [guide](int value) { guide->model()->setValue("points", value); });
        connect(ratio, &QDoubleSpinBox::valueChanged, guide, [guide](double value) { guide->model()->setValue("innerRatio", value / 100); });
        auto* hint = new QLabel("Depois de confirmar, clique no centro e arraste para definir o raio externo no desenho.", &dialog);
        hint->setWordWrap(true); layout->addWidget(hint);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(760, 650);
        if (dialog.exec() != QDialog::Accepted) { active_tool_.reset(); activate_tool("select"); return; }
        star->points = points->value(); star->inner_ratio = ratio->value() / 100;
    }

    // Operações sem interação de canvas usam os valores padrão enquanto o painel de propriedades
    // específico ainda não existe no C++. Assim o botão executa a ferramenta, em vez de apenas
    // mostrar uma dica sem produzir efeito.
    if (name == "mirror") {
        auto* mirror = static_cast<tools::MirrorTool*>(active_tool_.get());
        if (tool_context_.selection().empty()) {
            statusBar()->showMessage("Espelhar: selecione pelo menos um objeto");
        } else {
            QMessageBox dialog(QMessageBox::Question, "Espelhar objetos",
                               "Escolha o eixo do espelhamento em torno do centro da seleção.",
                               QMessageBox::NoButton, this);
            auto* horizontal = dialog.addButton("Horizontal", QMessageBox::AcceptRole);
            auto* vertical = dialog.addButton("Vertical", QMessageBox::AcceptRole);
            dialog.addButton("Cancelar", QMessageBox::RejectRole);
            auto* copy = new QCheckBox("Criar cópia e manter o original", &dialog);
            dialog.setCheckBox(copy);
            dialog.exec();
            if (dialog.clickedButton() == horizontal || dialog.clickedButton() == vertical) {
                mirror->make_copy = copy->isChecked();
                mirror->apply(dialog.clickedButton() == horizontal ? "h" : "v");
            }
        }
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "trim") {
        static_cast<tools::TrimTool*>(active_tool_.get())->apply_form();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "join") {
        static_cast<tools::JoinTool*>(active_tool_.get())->run();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "vector_texture") {
        static_cast<tools::VectorTextureTool*>(active_tool_.get())->run();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "offset") {
        static_cast<tools::OffsetTool*>(active_tool_.get())->apply_form();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "boundary") {
        static_cast<tools::BoundaryTool*>(active_tool_.get())->run();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "fit_curves") {
        static_cast<tools::FitCurvesTool*>(active_tool_.get())->run();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "array_copy") {
        static_cast<tools::ArrayCopyTool*>(active_tool_.get())->apply_form();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "circular_copy") {
        static_cast<tools::CircularCopyTool*>(active_tool_.get())->apply_form();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    if (name == "layout_optimizer") {
        const auto selected = tool_context_.selection();
        if (selected.empty()) {
            statusBar()->showMessage("Selecione os objetos para organizar");
        } else {
            const auto [moves, result] = cad::plan_layout(selected, document_.material);
            std::map<std::string, std::pair<double, double>> shifts;
            for (const auto& [entity, dx, dy] : moves) shifts[entity->id] = {dx, dy};
            if (!shifts.empty()) {
                tool_context_.transform(selected, [shifts](cad::Entity& entity) {
                    const auto it = shifts.find(entity.id);
                    if (it != shifts.end()) entity.translate(it->second.first, it->second.second);
                }, "Organizar objetos");
            }
            statusBar()->showMessage(QString("Organização concluída: %1 movido(s), %2 fora da chapa")
                                         .arg(result.movidos)
                                         .arg(result.nao_couberam));
        }
        active_tool_.reset();
        activate_tool("select");
        return;
    }

    if (active_tool_->instant()) {
        active_tool_->run();
        active_tool_.reset();
        activate_tool("select");
        return;
    }
    canvas_widget_->canvas->set_tool(active_tool_.get());
    active_tool_->activate();
    statusBar()->showMessage(QString::fromStdString(active_tool_->label()));

    for (QAbstractButton* button : findChildren<QAbstractButton*>()) {
        if (button->property("toolName").toString() == name) {
            QSignalBlocker blocker(button);
            button->setChecked(true);
        }
    }
}

void MainWindow::activate_dimension(const QString& mode) {
    activate_tool("dimension");
    if (auto* dimension = dynamic_cast<tools::DimensionTool*>(active_tool_.get()))
        dimension->set_mode(mode.toStdString());
}

void MainWindow::create_actions() {
    QMenu* file_menu = menuBar()->addMenu("Arquivo");

    QAction* new_action = file_menu->addAction("Novo");
    new_action->setShortcut(QKeySequence::New);
    connect(new_action, &QAction::triggered, this, [this] {
        document_ = cad::Document();
        document_.material.width = 1220.0;
        document_.material.height = 2440.0;
        refresh_document_view();
        statusBar()->showMessage("Novo projeto");
    });

    QAction* open_action = file_menu->addAction("Abrir...");
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Abrir projeto", {},
                                                           "Projetos CNC (*.cnc);;Todos os arquivos (*)");
        if (!path.isEmpty()) open_file(path);
    });

    QAction* save_action = file_menu->addAction("Salvar");
    save_action->setShortcut(QKeySequence::Save);
    connect(save_action, &QAction::triggered, this, [this] { save_file(); });

    QAction* save_as_action = file_menu->addAction("Salvar como...");
    save_as_action->setShortcut(QKeySequence::SaveAs);
    connect(save_as_action, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Salvar projeto", {},
                                                           "Projetos CNC (*.cnc);;Todos os arquivos (*)");
        if (!path.isEmpty()) save_file(path);
    });

    QAction* import_action = file_menu->addAction("Importar vetores...");
    import_action->setShortcut(QKeySequence("Ctrl+I"));
    connect(import_action, &QAction::triggered, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, "Importar vetores", {}, QString::fromUtf8(io::kImportFilter));
        if (!paths.isEmpty()) import_files(paths);
    });

    QAction* batch_import_action = file_menu->addAction("Importar pasta em lote...");
    connect(batch_import_action, &QAction::triggered, this, &MainWindow::import_folder);

    QAction* svg_action = file_menu->addAction("Exportar SVG...");
    svg_action->setShortcut(QKeySequence("Ctrl+E"));
    connect(svg_action, &QAction::triggered, this, &MainWindow::export_svg);

    file_menu->addSeparator();
    QAction* exit_action = file_menu->addAction("Sair");
    exit_action->setShortcut(QKeySequence::Quit);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    QMenu* view_menu = menuBar()->addMenu("Exibir");
    QAction* drawing_action = view_menu->addAction("Desenho 2D");
    drawing_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(drawing_action, &QAction::triggered, this, [this] { view_tabs_->setCurrentIndex(0); });

    QAction* view_3d_action = view_menu->addAction("Vista 3D");
    view_3d_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(view_3d_action, &QAction::triggered, this, [this] { view_tabs_->setCurrentIndex(1); });

    QAction* fit_action = view_menu->addAction("Ajustar desenho à tela");
    fit_action->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit_action, &QAction::triggered, this, [this] {
        canvas_widget_->canvas->fit_view();
        viewport_3d_->fit_scene();
    });
    view_menu->addAction("Galeria interativa de desenhos...", QKeySequence(), this,
                         &MainWindow::show_guides_gallery);
    QAction* selection_fit_action = view_menu->addAction("Zoom na seleção");
    selection_fit_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    connect(selection_fit_action, &QAction::triggered, canvas_widget_->canvas, &vp::Canvas2D::zoom_selection);
    QAction* zoom_in_action = view_menu->addAction("Zoom +");
    // On several keyboard layouts the '+' key is reported as '=' with Shift.
    zoom_in_action->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                                  QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(zoom_in_action, &QAction::triggered, canvas_widget_->canvas, [this] {
        canvas_widget_->canvas->zoom_by(1.25);
    });
    QAction* zoom_out_action = view_menu->addAction("Zoom -");
    zoom_out_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(zoom_out_action, &QAction::triggered, canvas_widget_->canvas, [this] {
        canvas_widget_->canvas->zoom_by(1.0 / 1.25);
    });

    QAction* grid_action = view_menu->addAction("Mostrar grade");
    grid_action->setCheckable(true);
    connect(grid_action, &QAction::toggled, canvas_widget_->canvas, &vp::Canvas2D::set_grid_visible);

    QMenu* edit_menu = menuBar()->addMenu("Editar");
    QAction* undo_action = edit_menu->addAction("Desfazer");
    undo_action->setShortcut(QKeySequence::Undo);
    connect(undo_action, &QAction::triggered, this, [this] {
        if (undo_stack_.undo()) {
            canvas_widget_->canvas->refresh_entities();
            statusBar()->showMessage("Desfeito");
        }
    });
    QAction* redo_action = edit_menu->addAction("Refazer");
    redo_action->setShortcut(QKeySequence::Redo);
    connect(redo_action, &QAction::triggered, this, [this] {
        if (undo_stack_.redo()) {
            canvas_widget_->canvas->refresh_entities();
            statusBar()->showMessage("Refeito");
        }
    });
    edit_menu->addSeparator();
    edit_menu->addAction("Selecionar todos os vetores", QKeySequence(QKeySequence::SelectAll), this, [this] {
        document_.select_all();
        canvas_widget_->canvas->refresh_entities();
    });
    edit_menu->addAction("Selecionar vetores da camada atual", QKeySequence(), this, [this] {
        document_.clear_selection();
        for (const auto& entity : document_.entities)
            if (entity->layer == tool_context_.current_layer && document_.is_editable(*entity))
                document_.set_selected(entity, true);
        canvas_widget_->canvas->refresh_entities();
    });
    edit_menu->addAction("Desmarcar tudo", QKeySequence(), this, [this] {
        document_.clear_selection();
        canvas_widget_->canvas->refresh_entities();
    });
    edit_menu->addSeparator();
    edit_menu->addAction("Gerenciar camadas...", QKeySequence(), this,
                         &MainWindow::manage_layers);
    edit_menu->addSeparator();
    edit_menu->addAction("Agrupar", QKeySequence("Ctrl+G"), this, [this] { activate_tool("group"); });
    edit_menu->addAction("Desagrupar", QKeySequence("Ctrl+U"), this, [this] { activate_tool("ungroup"); });
    edit_menu->addAction("Fechar vetores", QKeySequence(), this, [this] { activate_tool("close_vectors"); });
    edit_menu->addAction("Converter texto em curvas", QKeySequence(), this,
                         [this] { activate_tool("convert_text"); });
    edit_menu->addAction("Soldar vetores", QKeySequence(), this, [this] { activate_tool("weld"); });
    edit_menu->addAction("Subtrair vetores", QKeySequence(), this, [this] { activate_tool("subtract"); });
    edit_menu->addAction("Interseção de vetores", QKeySequence(), this,
                         [this] { activate_tool("intersect"); });

    QMenu* dimensions_menu = menuBar()->addMenu("Cotas");
    dimensions_menu->addAction("Medir distância", QKeySequence(), this,
                                [this] { activate_tool("measure"); });
    dimensions_menu->addSeparator();
    const std::vector<std::pair<QString, QString>> dimensions = {
        {"linear", "Cota linear"}, {"aligned", "Cota alinhada"},
        {"horizontal", "Cota horizontal"}, {"vertical", "Cota vertical"},
        {"angular", "Cota angular"}, {"diameter", "Cota de diâmetro"},
        {"radius", "Cota de raio"},
    };
    for (const auto& [mode, label] : dimensions)
        dimensions_menu->addAction(label, QKeySequence(), this, [this, mode] { activate_dimension(mode); });

    QMenu* cam_menu = menuBar()->addMenu("Percursos de ferramenta");
    const std::vector<std::pair<QString, cam::ToolpathType>> operations = {
        {"Percurso de perfil", cam::ToolpathType::Profile},
        {"Percurso de bolso", cam::ToolpathType::Pocket},
        {"Percurso de fura\xC3\xA7\xC3\xA3o", cam::ToolpathType::Drill},
        {"Entalhe V", cam::ToolpathType::VCarve},
        {"Canais (Fluting)", cam::ToolpathType::Fluting},
        {"Textura", cam::ToolpathType::Texture},
        {"Rasgo Keyhole", cam::ToolpathType::Keyhole},
        {"Inlay", cam::ToolpathType::Inlay},
        {"Faca de arraste", cam::ToolpathType::Dragknife},
    };
    for (const auto& [label, kind] : operations) {
        QAction* action = cam_menu->addAction(label);
        connect(action, &QAction::triggered, this, [this, kind] {
            toolpath_panel_kind_ = kind;
            if (toolpath_type_combo_) toolpath_type_combo_->setCurrentIndex(
                toolpath_type_combo_->findData(static_cast<int>(kind)));
            add_toolpath_from_panel();
        });
    }
    cam_menu->addSeparator();
    cam_menu->addAction("Banco de ferramentas...", QKeySequence(), this,
                        [this] { edit_tool_database(); });
    cam_menu->addSeparator();
    cam_menu->addAction("Recalcular todos os percursos", QKeySequence(Qt::Key_F9), this,
                        &MainWindow::recalculate_toolpaths);
    cam_menu->addAction("Simular percursos", QKeySequence(), this, &MainWindow::toggle_simulation);
    cam_menu->addSeparator();
    cam_menu->addAction("Central da Ordem de Serviço...", QKeySequence(), this,
                        &MainWindow::open_order_service_page);
    cam_menu->addAction("Salvar G-Code", QKeySequence("Ctrl+Shift+G"), this,
                        &MainWindow::export_gcode);

    QMenu* help_menu = menuBar()->addMenu("Ajuda");
    help_menu->addAction("Sobre o CNC-CAM", this, [this] {
        QMessageBox::about(this, "CNC-CAM", "CNC-CAM - CAD/CAM para CNC");
    });
}

void MainWindow::schedule_selection_previews() {
    selection_guide_dirty_ = true;
    material_guide_dirty_ = true;
    automatic_cut_panel_dirty_ = true;
    // Keep geometry editing synchronous. Secondary previews consume only the
    // latest state, at most once per frame, and do no work while collapsed.
    if (selection_preview_timer_ && right_panel_scroll_ && right_panel_scroll_->isVisible() &&
        !selection_preview_timer_->isActive())
        selection_preview_timer_->start();
}

void MainWindow::refresh_guide_previews() {
    const auto* tool = canvas_widget_ && canvas_widget_->canvas ? canvas_widget_->canvas->tool() : nullptr;
    const std::string mode = tool ? tool->active_mode() : std::string{};
    const bool manipulating = mode == "move" || mode == "handle-scale" ||
                              mode == "handle-rotate" || mode == "rubber";
    for (const QString name : {QString("SelectionGuide"), QString("MaterialGuideThumbnail")}) {
        auto* guide = findChild<ui::MachiningGuide*>(name);
        const bool selection = name == "SelectionGuide";
        bool& dirty = selection ? selection_guide_dirty_ : material_guide_dirty_;
        if (!guide || !guide->isVisible() || !dirty) continue;
        // Drawing a large 3D guide competes with the direct manipulation of the
        // same geometry. Keep it dirty until release/cancel schedules the final
        // state; the material thumbnail has no selection geometry and can update.
        if (selection && manipulating) continue;
        dirty = false;
        QPainterPath contour;
        contour.setFillRule(Qt::OddEvenFill);
        bool all_closed = true;
        // The material thumbnail does not use selection geometry. In particular,
        // do not tessellate thousands of vectors for an invisible selection guide.
        if (selection) {
            for (const auto& entity : document_.entities) {
                if (!entity->selected || !document_.is_editable(*entity)) continue;
                const auto points = entity->to_points(160);
                if (points.size() < 2) continue;
                contour.moveTo(points.front().x, points.front().y);
                for (size_t i = 1; i < points.size(); ++i) contour.lineTo(points[i].x, points[i].y);
                if (entity->closed()) contour.closeSubpath(); else all_closed = false;
            }
        }
        auto* model = guide->model();
        bool changed = false;
        // Batch document updates: rebuild the preview once, not once per field.
        {
            QSignalBlocker block(model);
            const auto set_value = [model, &changed](const QString& key, const QVariant& value) {
                if (model->value(key) == value) return;
                model->setValue(key, value);
                changed = true;
            };
            set_value("width", document_.material.width);
            set_value("height", document_.material.height);
            set_value("thickness", document_.material.thickness);
            set_value("clearance", document_.material.clearance_z);
            set_value("originX", QStringList{"left", "center", "right"}.indexOf(QString::fromStdString(document_.material.zero_x)));
            set_value("originY", QStringList{"bottom", "center", "top"}.indexOf(QString::fromStdString(document_.material.zero_y)));
            set_value("zeroZ", document_.material.zero_z == "bottom" ? 1 : 0);
            if (selection) {
                if (model->contour() != contour || model->closed() != all_closed) {
                    model->setContour(contour, all_closed);
                    changed = true;
                }
                set_value("side", 2);
                set_value("openSide", 2);
            }
        }
        const QString kind = selection && !contour.isEmpty() ? "01_usinar_vetores" : "08_material";
        // Changing topic already rebuilds with the new model. Avoid doing the
        // same geometry calculation again through GuideModel::changed.
        if (guide->kind() != kind) guide->set_kind(kind);
        else if (changed) emit model->changed();
    }
}

void MainWindow::refresh_document_view() {
    selection_guide_dirty_ = true;
    material_guide_dirty_ = true;
    automatic_cut_panel_dirty_ = true;
    order_service_geometry_snapshot_valid_ = false;
    if (simulation_timer_ && simulation_timer_->isActive()) simulation_timer_->stop();
    simulation_.reset();
    refresh_simulation_status();
    canvas_widget_->set_document(&document_);
    refresh_layers();
    // QOpenGLWidget ainda nao possui contexto durante a construcao da janela.
    // A limpeza dos buffers so pode ocorrer depois que a viewport foi mostrada.
    if (viewport_3d_->isValid()) {
        viewport_3d_->clear_entities();
        viewport_3d_->clear_simulation();
    }
    const double z_top = document_.material.surface_z();
    viewport_3d_->set_workpiece(document_.material.width, document_.material.height,
                                 z_top - document_.material.thickness, z_top);
    viewport_3d_->fit_scene();
    refresh_toolpaths();
    refresh_guide_previews();
    setWindowTitle(QString("CNC-CAM — %1").arg(QString::fromStdString(document_.title())));
}

bool MainWindow::open_file(const QString& path) {
    try {
        document_.load(std::filesystem::path(path.toStdWString()));
        refresh_document_view();
        canvas_widget_->canvas->fit_view();
        statusBar()->showMessage(QString("Projeto aberto: %1").arg(path));
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Erro ao abrir projeto", QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::save_file(const QString& path) {
    QString target = path;
    if (target.isEmpty()) {
        if (document_.file_path) {
            target = QString::fromStdWString(document_.file_path->wstring());
        } else {
            target = QFileDialog::getSaveFileName(this, "Salvar projeto", {},
                                                  "Projetos CNC (*.cnc);;Todos os arquivos (*)");
        }
    }
    if (target.isEmpty()) return false;

    try {
        document_.save(std::filesystem::path(target.toStdWString()));
        refresh_document_view();
        statusBar()->showMessage(QString("Projeto salvo: %1").arg(target));
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Erro ao salvar projeto", QString::fromUtf8(error.what()));
        return false;
    }
}

}  // namespace cnc::app
