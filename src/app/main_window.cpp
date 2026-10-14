#include "app/main_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <tuple>

#include <QAction>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
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
#include "cad/layout.hpp"
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

using InteractiveGuideWidget = ui::MachiningGuide;

QImage make_job_layout_preview(const std::vector<cam::SetupSheetOperation>& operations) {
    QImage image(960, 340, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("white"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    std::optional<BBox> bounds;
    for (const auto& operation : operations) {
        if (!operation.path) continue;
        for (const cam::Move& move : operation.path->moves) {
            if (!move.cutting()) continue;
            if (!bounds) bounds = BBox{move.x, move.y, move.x, move.y};
            else {
                bounds->x0 = std::min(bounds->x0, move.x);
                bounds->y0 = std::min(bounds->y0, move.y);
                bounds->x1 = std::max(bounds->x1, move.x);
                bounds->y1 = std::max(bounds->y1, move.y);
            }
        }
    }
    if (!bounds) {
        painter.setPen(QColor("#536579"));
        painter.drawText(image.rect(), Qt::AlignCenter, "Sem movimentos de corte calculados");
        painter.end();
        return image;
    }

    const double width_mm = std::max(0.01, bounds->x1 - bounds->x0);
    const double height_mm = std::max(0.01, bounds->y1 - bounds->y0);
    const QRectF area(82, 40, 780, 230);
    const double scale = std::min(area.width() / width_mm, area.height() / height_mm);
    const double drawn_width = width_mm * scale;
    const double drawn_height = height_mm * scale;
    const double left = area.center().x() - drawn_width / 2.0;
    const double top = area.center().y() - drawn_height / 2.0;
    const QRectF extent(left, top, drawn_width, drawn_height);

    painter.setPen(QPen(QColor("#8294a6"), 1.2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(extent);
    const std::array<QColor, 7> colors = {QColor("#1769aa"), QColor("#d45532"), QColor("#27825a"),
                                          QColor("#8a54a2"), QColor("#bc7b16"), QColor("#16888a"),
                                          QColor("#ba3d6c")};
    for (size_t op_index = 0; op_index < operations.size(); ++op_index) {
        const auto* path = operations[op_index].path;
        if (!path) continue;
        QPainterPath cut_path;
        bool connected = false;
        for (const cam::Move& move : path->moves) {
            if (!move.cutting()) {
                connected = false;
                continue;
            }
            const QPointF point(left + (move.x - bounds->x0) * scale,
                                top + (bounds->y1 - move.y) * scale);
            if (connected) cut_path.lineTo(point);
            else cut_path.moveTo(point);
            connected = true;
        }
        painter.setPen(QPen(colors[op_index % colors.size()], 1.8, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(cut_path);
    }

    painter.setPen(QPen(QColor("#40566c"), 1.0));
    const qreal dim_y = extent.bottom() + 24.0;
    painter.drawLine(QPointF(extent.left(), extent.bottom() + 3), QPointF(extent.left(), dim_y + 3));
    painter.drawLine(QPointF(extent.right(), extent.bottom() + 3), QPointF(extent.right(), dim_y + 3));
    painter.drawLine(QPointF(extent.left(), dim_y), QPointF(extent.right(), dim_y));
    painter.drawLine(QPointF(extent.left(), dim_y), QPointF(extent.left() + 7, dim_y - 4));
    painter.drawLine(QPointF(extent.left(), dim_y), QPointF(extent.left() + 7, dim_y + 4));
    painter.drawLine(QPointF(extent.right(), dim_y), QPointF(extent.right() - 7, dim_y - 4));
    painter.drawLine(QPointF(extent.right(), dim_y), QPointF(extent.right() - 7, dim_y + 4));
    painter.fillRect(QRectF(extent.center().x() - 44, dim_y - 10, 88, 20), Qt::white);
    painter.drawText(QRectF(extent.center().x() - 55, dim_y - 9, 110, 18), Qt::AlignCenter,
                     QString("X  %1 mm").arg(width_mm, 0, 'f', 2));

    painter.save();
    painter.translate(extent.right() + 30, extent.center().y());
    painter.rotate(-90);
    painter.fillRect(QRectF(-55, -10, 110, 20), Qt::white);
    painter.drawText(QRectF(-55, -9, 110, 18), Qt::AlignCenter,
                     QString("Y  %1 mm").arg(height_mm, 0, 'f', 2));
    painter.restore();
    painter.setPen(QColor("#536579"));
    painter.drawText(QRectF(12, 310, image.width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     "Envelope do percurso calculado; medidas em milimetros.");
    painter.end();
    return image;
}

}  // namespace

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
    layout->addWidget(view_header);

    canvas_widget_ = new vp::Canvas2DWidget(document_.material.width, document_.material.height,
                                             central);
    canvas_widget_->canvas->set_document(&document_);
    view_stack_ = new QStackedWidget(central);
    view_stack_->addWidget(canvas_widget_);

    viewport_3d_ = new vp::Viewport3D(central);
    view_stack_->addWidget(viewport_3d_);
    layout->addWidget(view_stack_, 1);
    setCentralWidget(central);

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
        resizeDocks({left_dock, right_dock}, {286, 326}, Qt::Horizontal);
    activate_tool("select");

    connect(canvas_widget_, &vp::Canvas2DWidget::mouse_moved, this,
            [this](double x, double y) {
                coordinate_label_->setText(QString("X: %1   Y: %2 mm")
                                               .arg(x, 0, 'f', 2)
                                               .arg(y, 0, 'f', 2));
            });
    connect(view_tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (view_stack_) view_stack_->setCurrentIndex(index);
        if (view_3d_controls_) view_3d_controls_->setVisible(index == 1);
        if (index == 1 && viewport_3d_) {
            QTimer::singleShot(120, this, [this] { refresh_3d_view(); });
        }
    });
    connect(canvas_widget_, &vp::Canvas2DWidget::files_dropped, this,
            [this](const QStringList& paths, double, double) { import_files(paths); });
    connect(canvas_widget_->canvas, &vp::Canvas2D::selection_changed, this,
            &MainWindow::refresh_guide_previews);
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

void MainWindow::show_3d_view() {
    if (view_tabs_) view_tabs_->setCurrentIndex(1);
}

void MainWindow::create_toolpath_panel() {
    auto* dock = new QDockWidget("Percursos", this);
    dock->setObjectName("ToolpathPanel");
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(286);

    auto* shell = new QWidget(dock);
    auto* shell_layout = new QHBoxLayout(shell);
    shell_layout->setContentsMargins(0, 0, 0, 0);
    shell_layout->setSpacing(0);

    auto* rail = new QWidget(shell);
    rail->setObjectName("RightToolRail");
    rail->setFixedWidth(50);
    auto* rail_layout = new QVBoxLayout(rail);
    rail_layout->setContentsMargins(4, 6, 4, 8);
    rail_layout->setSpacing(4);
    auto* collapse = new QToolButton(rail);
    collapse->setObjectName("RightRailToggle");
    collapse->setFixedSize(40, 30);
    collapse->setText("‹");
    collapse->setToolTip("Mostrar ou ocultar o painel direito");
    rail_layout->addWidget(collapse);
    auto* rail_sep = new QFrame(rail);
    rail_sep->setObjectName("RightToolRailSep");
    rail_sep->setFrameShape(QFrame::NoFrame);
    rail_sep->setFixedHeight(1);
    rail_layout->addWidget(rail_sep);
    auto* selection_button = new QToolButton(rail);
    selection_button->setObjectName("RightRailButton");
    selection_button->setCheckable(true);
    selection_button->setChecked(true);
    selection_button->setFixedSize(40, 34);
    selection_button->setIcon(ui::get_icon("select"));
    selection_button->setIconSize(QSize(22, 22));
    selection_button->setToolTip("Selecao");
    rail_layout->addWidget(selection_button);
    selection_nav_button_ = selection_button;
    auto* toolpath_button = new QToolButton(rail);
    toolpath_button->setObjectName("RightRailButton");
    toolpath_button->setCheckable(true);
    toolpath_button->setFixedSize(40, 34);
    toolpath_button->setIcon(ui::get_icon("tp_recalc"));
    toolpath_button->setIconSize(QSize(22, 22));
    toolpath_button->setToolTip("Percursos CAM");
    rail_layout->addWidget(toolpath_button);
    toolpath_nav_button_ = toolpath_button;
    auto* simulation_button = new QToolButton(rail);
    simulation_button->setObjectName("RightRailButton");
    simulation_button->setCheckable(true);
    simulation_button->setFixedSize(40, 34);
    simulation_button->setIcon(ui::get_icon("tp_preview"));
    simulation_button->setIconSize(QSize(22, 22));
    simulation_button->setToolTip(QString::fromUtf8("Simula\xC3\xA7\xC3\xA3o do percurso"));
    rail_layout->addWidget(simulation_button);
    simulation_nav_button_ = simulation_button;
    auto* automatic_button = new QToolButton(rail);
    automatic_button->setObjectName("RightRailButton");
    automatic_button->setCheckable(true);
    automatic_button->setFixedSize(40, 34);
    automatic_button->setIcon(ui::get_icon("tp_template"));
    automatic_button->setIconSize(QSize(22, 22));
    automatic_button->setToolTip(QString::fromUtf8("Corte autom\xC3\xA1tico por material"));
    rail_layout->addWidget(automatic_button);
    automatic_nav_button_ = automatic_button;
    rail_layout->addStretch();
    shell_layout->addWidget(rail);

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
        {"tp_sheet", "Folha de trabalho", [this] { export_setup_sheet(); }},
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
            [this](int) { refresh_automatic_cut_panel(); });

    auto* automatic_note = new QLabel(
        "Segurança: a espessura escolhida deve ser a mesma da chapa do trabalho. "
        "Materiais sem cadastro não recebem parâmetros inventados.", automatic_page);
    automatic_note->setObjectName("MutedText");
    automatic_note->setWordWrap(true);
    automatic_layout->addWidget(automatic_note);
    automatic_layout->addStretch();
    stack->addWidget(automatic_page);

    auto* panel_scroll = new QScrollArea(shell);
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
    dock->setWidget(shell);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(selection_button, &QToolButton::clicked, this, [this] { show_right_page(0); });
    connect(toolpath_button, &QToolButton::clicked, this, [this] { show_right_page(1); });
    connect(simulation_button, &QToolButton::clicked, this, [this] { show_right_page(2); });
    connect(automatic_button, &QToolButton::clicked, this, [this] {
        show_right_page(3);
        refresh_automatic_cut_panel();
    });
    connect(collapse, &QToolButton::clicked, dock, [dock] { dock->setVisible(false); });
    show_right_page(0);
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
    refresh_bridge_markers();
    statusBar()->showMessage("Marcacao manual de pontes encerrada.", 3500);
}

void MainWindow::refresh_3d_view() {
    if (!viewport_3d_) return;
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
    cam::ToolDatabase database = cam::ToolDatabase::load();

    ui::ResponsiveDialog dialog(this);
    dialog.setWindowTitle("Banco de ferramentas");
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
                        return;
                    }
                }
            }
        }
    };

    refresh_tree();
    if (!database.tools.empty()) load_editor(0);

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
    connect(dialog_buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
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
    dialog.resize(std::min(880, screen_size.width() - 48),
                  std::min(760, screen_size.height() - 96));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 0, 0);
    auto* selection = new QLabel(
        QString("%1 vetor(es) selecionado(s)").arg(job.entity_ids.size()), &dialog);
    selection->setStyleSheet("color: #96a9c1; padding: 12px 16px 10px;");
    root->addWidget(selection);

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
    QPainterPath selected_contour;
    selected_contour.setFillRule(Qt::OddEvenFill);
    bool all_closed = true;
    for (const auto& id : job.entity_ids) {
        const auto entity = document_.find_entity(id);
        if (!entity) continue;
        const auto points = entity->to_points(160);
        if (points.size() < 2) continue;
        selected_contour.moveTo(points.front().x, points.front().y);
        for (size_t i = 1; i < points.size(); ++i)
            selected_contour.lineTo(points[i].x, points[i].y);
        if (entity->closed()) selected_contour.closeSubpath();
        else all_closed = false;
    }
    if (!selected_contour.isEmpty()) guide_model->setContour(selected_contour, all_closed);

    const QMap<QString, QString> guide_keys = {
        {"Cota inicial (D)", "start"}, {"Cota inicial", "start"}, {"Profundidade (C)", "depth"}, {"Profundidade de corte", "depth"},
        {"Posição da fresa", "side"}, {"Vetor aberto", "openSide"}, {"Direção", "direction"},
        {"Pontes de fixação", "tabs"}, {"Comprimento da ponte", "tabLength"},
        {"Espessura da ponte", "tabHeight"}, {"Quantidade de pontes", "tabCount"},
        {"Pontes 3D com subida em rampa", "tabs3d"}, {"Tipo de entrada", "lead"},
        {"Comprimento", "leadLength"}, {"Raio do arco", "leadRadius"}, {"Ângulo", "leadAngle"},
        {"Criar saída", "leadOut"}, {"Tipo de rampa", "ramp"}, {"Rampa", "ramp"},
        {"Distância", "rampDistance"}, {"Distância da rampa", "rampDistance"},
        {"Preenchimento", "fill"}, {"Ângulo do raster", "rasterAngle"}, {"Passo lateral", "stepover"},
        {"Sobremetal", "allowance"}
    };
    const auto bind_guide_field = [guide_model, guide_keys, &dialog](const QString& label, QWidget* field) {
        QString key = guide_keys.value(label);
        if (label == "Ângulo" && field->parentWidget()) {
            const auto* section = qobject_cast<QGroupBox*>(field->parentWidget()->parentWidget());
            if (section && section->title() == "Rampas") key = "rampAngle";
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
        auto* field = new QDoubleSpinBox(body);
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
        auto* field = new QSpinBox(body);
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
        auto* field = new QComboBox(body);
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
    auto add_group = [&](const QString& title, GuideTopics guides = {}) {
        auto* box = new QGroupBox(title, body);
        auto* section = new QHBoxLayout(box);
        section->setContentsMargins(10, 8, 10, 8);
        section->setSpacing(14);
        new ui::AdaptiveSection(box, section, 790);
        auto* fields = new QWidget(box);
        auto* form = new QFormLayout(fields);
        form->setContentsMargins(4, 8, 4, 8);
        form->setSpacing(7);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        section->addWidget(fields, 1);
        if (!guides.empty()) {
            auto* card = new QWidget(box);
            card->setObjectName("CamGuideCard");
            auto* guide_layout = new QVBoxLayout(card);
            guide_layout->setContentsMargins(0, 0, 0, 0);
            auto* choice = new QComboBox(card);
            choice->setObjectName("CamGuideTopic");
            choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            choice->setMinimumContentsLength(10);
            for (const auto& [label, key] : guides) choice->addItem(label, key);
            auto* image = new InteractiveGuideWidget(guides.front().second, card, guide_model);
            image->setMinimumHeight(230);
            image->setMaximumHeight(390);
            interactive_guides.push_back(image);
            guide_layout->addWidget(choice);
            guide_layout->addWidget(image, 1);
            guide_layout->addWidget(ui::guidePlayback(image, card));
            auto* enlarge = new QPushButton("Ampliar desenho e animação", card);
            enlarge->setObjectName("EnlargeGuide");
            guide_layout->addWidget(enlarge);
            connect(choice, &QComboBox::currentIndexChanged, image,
                [choice, image] { image->set_kind(choice->currentData().toString()); });
            connect(enlarge, &QPushButton::clicked, box, [image, guide_model] {
                ui::ResponsiveDialog preview(image);
                preview.setObjectName("EnlargedGuide");
                preview.setWindowTitle(ui::MachiningGuide::title(image->kind()));
                auto* layout = new QVBoxLayout(&preview);
                auto* enlarged = new InteractiveGuideWidget(image->kind(), &preview, guide_model);
                enlarged->setProgress(image->progress());
                layout->addWidget(enlarged, 1);
                layout->addWidget(ui::guidePlayback(enlarged, &preview));
                auto* hint = new QLabel("Esta vista acompanha os parâmetros do percurso. Feche para continuar editando.", &preview);
                hint->setWordWrap(true); layout->addWidget(hint);
                auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &preview);
                connect(close, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
                layout->addWidget(close);
                preview.resize(900, 660);
                preview.exec();
            });
            section->addWidget(card, 1);
        }
        body_layout->addWidget(box);
        return form;
    };

    auto* identity = add_group("Percurso");
    auto* name = new QLineEdit(QString::fromStdString(job.name), body);
    identity->addRow("Nome", name);
    apply.push_back([&job, name] { job.name = name->text().toStdString(); });

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
    auto* tool_group_box = qobject_cast<QGroupBox*>(tool_form->parentWidget()->parentWidget());
    body_layout->removeWidget(tool_group_box);
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
            for (const auto& tool : database.tools) {
                auto* item = new QListWidgetItem(QString::fromStdString(tool->name), tool_list);
                item->setIcon(ui::get_icon(tool->tool_type == cam::ToolType::Drill
                                               ? "tp_drill" : "tp_tools"));
                item->setToolTip(QString::fromStdString(tool->describe()));
            }
            if (!database.tools.empty()) tool_list->setCurrentRow(0);
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
            edit_tool_database();
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
    connect(select_tool_button, &QPushButton::clicked, &dialog, [&] {
        if (choose_tool_from_database()) {
            selected_tool_name->setText(QString::fromStdString(job.tool.name));
            guide_model->setValue("diameter", job.tool.diameter);
            guide_model->setValue("pass", job.tool.pass_depth);
            guide_model->setValue("toolStepover", job.tool.stepover);
            guide_model->setValue("toolType", static_cast<int>(job.tool.tool_type));
            if (refresh_pass_summary) refresh_pass_summary();
        }
    });
    connect(edit_tools_button, &QPushButton::clicked, &dialog,
            [this] { edit_tool_database(); });

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
        auto* label = new QLabel(box);
        label->setObjectName("PropHint");
        label->setWordWrap(true);
        layout->addWidget(label, 1);
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
            auto* separate_last = add_bool(form, "Fazer último passe em separado",
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
            form = add_group("Pontes", {{"Pontes de fixação", "03_pontes"}});
            auto* bridge_enabled = add_bool(form, "Pontes de fixação", settings.tabs,
                                            [&settings](bool v) { settings.tabs = v; });
            auto* bridge_length = add_double(form, "Comprimento da ponte", settings.tab_length, 0, 1000, " mm",
                                             [&settings](double v) { settings.tab_length = v; });
            auto* bridge_thickness = add_double(form, "Espessura da ponte", settings.tab_thickness, 0, 1000, " mm",
                                                [&settings](double v) { settings.tab_thickness = v; });
            auto* bridge_count = add_int(form, "Quantidade de pontes", settings.tab_count, 1, 200,
                                         [&settings](int v) { settings.tab_count = v; });
            auto* bridge_spacing = add_double(form, "Espacamento entre pontes", settings.tab_spacing,
                                              0.5, 5000, " mm",
                                              [&settings](double v) { settings.tab_spacing = v; });
            auto* bridge_distribution = new QComboBox(body);
            bridge_distribution->addItem("Quantidade fixa por contorno", 0);
            bridge_distribution->addItem("Uma ponte a cada espacamento", 1);
            bridge_distribution->setCurrentIndex(settings.tab_spacing > 0.0 ? 1 : 0);
            form->addRow("Distribuicao", bridge_distribution);
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
            auto* bridges_3d = add_bool(form, "Pontes 3D com subida em rampa", settings.tabs_3d,
                                        [&settings](bool v) { settings.tabs_3d = v; });
            auto* manual_only = add_bool(form, "Usar somente pontes posicionadas manualmente",
                                         settings.tab_manual_only,
                                         [&settings](bool v) { settings.tab_manual_only = v; });
            auto* bridge_help = new QLabel(
                "Use Marcar no desenho para adicionar pontes. Arraste os marcadores para ajustar; "
                "pressione Esc para encerrar a marcacao.", body);
            bridge_help->setWordWrap(true);
            bridge_help->setObjectName("MutedText");
            form->addRow(bridge_help);
            auto* bridge_actions = new QWidget(body);
            auto* bridge_actions_layout = new QGridLayout(bridge_actions);
            bridge_actions_layout->setContentsMargins(0, 0, 0, 0);
            auto* mark_bridges = new QPushButton("Marcar no desenho", bridge_actions);
            auto* undo_manual_bridge = new QPushButton("Desfazer última", bridge_actions);
            auto* clear_manual_bridges = new QPushButton("Limpar manuais", bridge_actions);
            auto* restore_auto_bridges = new QPushButton("Redistribuir", bridge_actions);
            bridge_actions_layout->addWidget(mark_bridges, 0, 0);
            bridge_actions_layout->addWidget(undo_manual_bridge, 0, 1);
            bridge_actions_layout->addWidget(clear_manual_bridges, 1, 0);
            bridge_actions_layout->addWidget(restore_auto_bridges, 1, 1);
            form->addRow(bridge_actions);
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
                mark_bridges->setEnabled(enabled);
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

            form = add_group("Entradas e saídas", {{"Entradas e saídas", "04_entradas_saidas"}});
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

            form = add_group("Rampas", {{"Rampas", "05_rampas"}});
            auto* ramp_type = add_enum(form, "Tipo de rampa", static_cast<int>(settings.ramp), ramps,
                     [&settings](int v) { settings.ramp = static_cast<cam::RampType>(v); });
            connect(ramp_type, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                    [update_guide_mode](int value) { update_guide_mode("05_rampas", value); });
            if (auto* guide = guide_for("05_rampas")) {
                guide->mode_clicked = [ramp_type](int value) { ramp_type->setCurrentIndex(value); };
            }
            auto* ramp_distance = add_double(form, "Distância", settings.ramp_distance, 0, 10000, " mm",
                                             [&settings](double v) { settings.ramp_distance = v; });
            auto* ramp_angle = add_double(form, "Ângulo", settings.ramp_angle, 0, 90, " °",
                                          [&settings](double v) { settings.ramp_angle = v; });
            auto* ramp_on_lead = add_bool(form, "Aplicar rampa na entrada", settings.ramp_on_lead,
                                          [&settings](bool v) { settings.ramp_on_lead = v; });
            register_advanced_field(form, ramp_distance);
            register_advanced_field(form, ramp_angle);
            register_advanced_field(form, ramp_on_lead);

            form = add_group("Ordem", {{"Ordem de usinagem", "01_usinar_vetores"}});
            add_enum(form, "Ordenar por", static_cast<int>(settings.order), orders,
                     [&settings](int v) { settings.order = static_cast<cam::MachineOrder>(v); });
            add_bool(form, "Separar interiores", settings.separate_interior,
                     [&settings](bool v) { settings.separate_interior = v; });
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
    root->addWidget(buttons);
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
            refresh_bridge_markers();
            statusBar()->showMessage(
                "Clique nos contornos para adicionar pontes; arraste marcadores existentes. Esc encerra.");
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
    if (errors.empty()) statusBar()->showMessage(QString("Percurso criado: %1").arg(QString::fromStdString(job->name)));
    else statusBar()->showMessage(QString::fromStdString(errors.front()));
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
    if (draft_errors.empty()) statusBar()->showMessage("Percurso atualizado");
    else statusBar()->showMessage(QString::fromStdString(draft_errors.front()));
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

void MainWindow::show_right_page(int index) {
    if (!right_panel_stack_ || index < 0 || index >= right_panel_stack_->count()) return;
    right_panel_stack_->setCurrentIndex(index);
    const std::array<QToolButton*, 4> buttons = {
        selection_nav_button_, toolpath_nav_button_, simulation_nav_button_, automatic_nav_button_};
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        if (buttons[static_cast<size_t>(i)]) buttons[static_cast<size_t>(i)]->setChecked(i == index);
    }
}

void MainWindow::refresh_automatic_cut_panel() {
    if (!automatic_cut_combo_ || !automatic_cut_info_label_) return;
    const auto& presets = cam::house_presets();
    const double thickness = document_.material.thickness;
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
    if (automatic_cut_combo_->currentData().toInt() < 0 && matching_count == 1)
        automatic_cut_combo_->setCurrentIndex(matching_combo_index);

    const int preset_index = automatic_cut_combo_->currentData().toInt();
    if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) {
        automatic_cut_info_label_->setText(
            QString("O trabalho está com chapa de %1 mm. Escolha um material cadastrado.")
                .arg(thickness, 0, 'g', 10));
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
    automatic_cut_info_label_->setText(description);
    if (automatic_create_button_) automatic_create_button_->setEnabled(true);
}

void MainWindow::create_automatic_cut() {
    if (!automatic_cut_combo_) return;
    const int preset_index = automatic_cut_combo_->currentData().toInt();
    const auto& presets = cam::house_presets();
    if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) return;

    const auto selected = document_.selected_entities();
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
    for (const auto profile : {cam::PostProcessor::Grbl, cam::PostProcessor::Mach3,
                                cam::PostProcessor::LinuxCnc})
        post->addItem(QString::fromUtf8(cam::post_label(profile)), static_cast<int>(profile));
    const int saved_post = preferences.value("gcode/post", static_cast<int>(cam::PostProcessor::Grbl)).toInt();
    post->setCurrentIndex(std::max(0, post->findData(saved_post)));
    auto* tool_change = new QCheckBox("Emitir Tn M6 na troca de ferramenta", &settings_dialog);
    auto* line_numbers = new QCheckBox("Numerar linhas", &settings_dialog);
    auto* comments = new QCheckBox("Incluir comentários", &settings_dialog);
    auto* return_home = new QCheckBox("Retornar ao ponto inicial no fim", &settings_dialog);
    connect(post, qOverload<int>(&QComboBox::currentIndexChanged), &settings_dialog,
            [tool_change, post] {
        const auto selected = static_cast<cam::PostProcessor>(post->currentData().toInt());
        tool_change->setEnabled(selected != cam::PostProcessor::Grbl);
        tool_change->setChecked(selected != cam::PostProcessor::Grbl);
    });
    settings_form->addRow("Controladora", post);
    settings_form->addRow("Troca de ferramenta", tool_change);
    settings_form->addRow(line_numbers);
    settings_form->addRow(comments);
    settings_form->addRow(return_home);
    settings_layout->addLayout(settings_form);
    auto* compatibility = new QLabel(
        "Ajuste o pós ao modelo e firmware reais. GRBL não recebe M6 neste perfil; em Mach3 e LinuxCNC, "
        "confirme a macro de troca, origem G54, spindle e limites em simulação antes de cortar.",
        &settings_dialog);
    compatibility->setWordWrap(true);
    compatibility->setObjectName("MutedText");
    settings_layout->addWidget(compatibility);
    const auto initial_post = static_cast<cam::PostProcessor>(post->currentData().toInt());
    const QString tool_change_key = QString("gcode/toolChange/%1").arg(static_cast<int>(initial_post));
    const bool saved_tool_change = preferences.value(tool_change_key,
                                                      initial_post != cam::PostProcessor::Grbl).toBool();
    tool_change->setChecked(initial_post != cam::PostProcessor::Grbl && saved_tool_change);
    tool_change->setEnabled(initial_post != cam::PostProcessor::Grbl);
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

    cam::GCodeSettings settings;
    settings.post = static_cast<cam::PostProcessor>(post->currentData().toInt());
    settings.tool_change = tool_change->isChecked();
    settings.line_numbers = line_numbers->isChecked();
    settings.comments = comments->isChecked();
    settings.home_at_end = return_home->isChecked();
    preferences.setValue("gcode/post", static_cast<int>(settings.post));
    preferences.setValue(QString("gcode/toolChange/%1").arg(static_cast<int>(settings.post)), settings.tool_change);
    preferences.setValue("gcode/lineNumbers", settings.line_numbers);
    preferences.setValue("gcode/comments", settings.comments);
    preferences.setValue("gcode/returnHome", settings.home_at_end);

    const QString path = QFileDialog::getSaveFileName(this, "Salvar G-Code", {},
                                                       "G-Code (*.nc *.tap *.gcode);;Todos os arquivos (*)");
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Erro ao salvar G-Code", output.errorString());
        return;
    }
    const std::string code = cam::generate_gcode(paths, document_.material, settings, document_.title());
    output.write(QByteArray::fromStdString(code));
    output.close();
    statusBar()->showMessage(QString("G-Code salvo: %1").arg(path));
}

void MainWindow::export_setup_sheet() {
    std::vector<cam::SetupSheetOperation> operations;
    for (const auto& job : document_.toolpaths) {
        if (!job || !job->visible) continue;
        for (const cam::Toolpath& path : job->results) {
            if (path.visible) operations.push_back({&path, job.get()});
        }
    }
    if (operations.empty()) {
        statusBar()->showMessage("Recalcule um percurso antes de criar a ordem de servico");
        return;
    }

    QSettings preferences;
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
    dialog.setWindowTitle("Dados da ordem de servico");
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* customer = new QLineEdit(&dialog);
    auto* contact = new QLineEdit(&dialog);
    auto* order_number = new QLineEdit(&dialog);
    auto* part_name = new QLineEdit(QString::fromStdString(document_.title()), &dialog);
    auto* revision = new QLineEdit(&dialog);
    auto* operator_name = new QLineEdit(&dialog);
    auto* notes = new QTextEdit(&dialog);
    notes->setMaximumHeight(90);
    form->addRow("Cliente", customer);
    form->addRow("Contato", contact);
    form->addRow("Numero da OS/pedido", order_number);
    form->addRow("Peca", part_name);
    form->addRow("Revisao", revision);
    form->addRow("Operador", operator_name);
    form->addRow("Observacoes", notes);

    auto* logo_row = new QWidget(&dialog);
    auto* logo_layout = new QHBoxLayout(logo_row);
    logo_layout->setContentsMargins(0, 0, 0, 0);
    auto* logo_status = new QLabel(logo_path.isEmpty() ? "Logo UNY CV padrao" : QFileInfo(logo_path).fileName(), logo_row);
    logo_status->setObjectName("MutedText");
    auto* choose_logo = new QPushButton("Escolher PNG/JPG...", logo_row);
    auto* remove_logo = new QPushButton("Remover", logo_row);
    logo_layout->addWidget(logo_status, 1);
    logo_layout->addWidget(choose_logo);
    logo_layout->addWidget(remove_logo);
    form->addRow("Logo da empresa", logo_row);
    layout->addLayout(form);

    connect(choose_logo, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog, "Selecionar logo", logo_path,
            "Imagens PNG ou JPG (*.png *.jpg *.jpeg)");
        if (selected.isEmpty()) return;
        QImageReader reader(selected);
        QImage loaded = reader.read();
        if (loaded.isNull()) {
            QMessageBox::warning(&dialog, "Logo invalido", "Nao foi possivel ler esta imagem.");
            return;
        }
        logo_path = selected;
        logo_image = std::move(loaded);
        preferences.setValue("jobSheet/logoPath", logo_path);
        logo_status->setText(QFileInfo(logo_path).fileName());
    });
    connect(remove_logo, &QPushButton::clicked, &dialog, [&] {
        logo_path.clear();
        logo_image = {};
        preferences.remove("jobSheet/logoPath");
        logo_status->setText("Logo UNY CV padrao");
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Gerar PDF");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    cam::SetupSheetInfo info;
    info.customer = customer->text().toStdString();
    info.contact = contact->text().toStdString();
    info.order_number = order_number->text().toStdString();
    info.part_name = part_name->text().toStdString();
    info.revision = revision->text().toStdString();
    info.operator_name = operator_name->text().toStdString();
    info.notes = notes->toPlainText().toStdString();
    if (!logo_image.isNull()) info.logo_source = "job-logo";

    QString path = QFileDialog::getSaveFileName(
        this, "Salvar ordem de servico em PDF",
        QString::fromStdString(document_.title()) + "_OS.pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".pdf";

    QString pdf_error;
    if (!cam::write_job_setup_sheet_pdf(path, operations, document_.material, document_.title(), info,
                                        make_job_layout_preview(operations), logo_image, &pdf_error)) {
        QMessageBox::critical(this, "Erro ao gerar PDF", pdf_error);
        return;
    }
    statusBar()->showMessage(QString("Ordem de servico PDF criada: %1").arg(path));
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
    for (const QString& path : paths) import_file(path);
}

void MainWindow::import_file(const QString& path) {
    if (path.isEmpty()) return;
    try {
        const auto entities = io::import_file(std::filesystem::path(path.toStdWString()));
        if (entities.empty()) {
            QMessageBox::information(this, "Importar", "Nenhum vetor encontrado no arquivo.");
            return;
        }

        const QString suffix = QFileInfo(path).suffix().toUpper();
        tool_context_.add(entities, QString("Importar %1").arg(suffix).toStdString());
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
    dock->setMinimumWidth(250);

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

    add_section("ARQUIVO E VISTA 2D", {
        {"import", "Importar"}, {"export", "Exportar"}, {"guide_gallery", "Guias"},
        {"preview_3d", "Previa 3D"},
        {"measure", "Medir"}, {"grid", "Grade"}, {"workspace_setup", "Area de trabalho"},
        {"select", "Selecao"}}, 6);
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
        {"move", ""}, {"distort", "Distorcer"}, {"mirror", "Espelhar"}, {"scale", ""},
        {"array_copy", "Copia array"}, {"circular_copy", ""}, {"rotate", ""}}, 7);
    add_separator();
    add_section("EDITAR OBJETOS", {
        {"node_edit", "Nos"}, {"fillet", "Filete"}, {"trim", "Aparar"},
        {"interactive_trim", "Aparar interativo"}, {"extend", ""}, {"fit_curves", "Ajustar curvas"},
        {"weld", "Soldar"}, {"subtract", ""}, {"intersect", ""},
        {"join", "Unir"}, {"group", "Agrupar"}, {"ungroup", ""}, {"offset", "Offset"}, {"boundary", ""},
        {"convert_text", "Texto"}}, 7);
    add_separator();
    add_section("ALINHAR E POSICIONAR", {
        {"align_left", ""}, {"align_center_h", ""}, {"align_right", ""},
        {"align_top", ""}, {"align_center_v", ""}, {"align_bottom", ""},
        {"layout_optimizer", "Organizar"}}, 7);
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
    zoom_in_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
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
        {"Percurso de furação", cam::ToolpathType::Drill},
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
                        &MainWindow::edit_tool_database);
    cam_menu->addSeparator();
    cam_menu->addAction("Recalcular todos os percursos", QKeySequence(Qt::Key_F9), this,
                        &MainWindow::recalculate_toolpaths);
    cam_menu->addAction("Simular percursos", QKeySequence(), this, &MainWindow::toggle_simulation);
    cam_menu->addSeparator();
    cam_menu->addAction("Criar folha de trabalho", QKeySequence(), this, &MainWindow::export_setup_sheet);
    cam_menu->addAction("Salvar G-Code", QKeySequence("Ctrl+Shift+G"), this,
                        &MainWindow::export_gcode);

    QMenu* help_menu = menuBar()->addMenu("Ajuda");
    help_menu->addAction("Sobre o CNC-CAM", this, [this] {
        QMessageBox::about(this, "CNC-CAM", "CNC-CAM - CAD/CAM para CNC");
    });
}

void MainWindow::refresh_guide_previews() {
    QPainterPath contour;
    contour.setFillRule(Qt::OddEvenFill);
    bool all_closed = true;
    for (const auto& entity : document_.selected_entities()) {
        const auto points = entity->to_points(160);
        if (points.size() < 2) continue;
        contour.moveTo(points.front().x, points.front().y);
        for (size_t i = 1; i < points.size(); ++i) contour.lineTo(points[i].x, points[i].y);
        if (entity->closed()) contour.closeSubpath(); else all_closed = false;
    }
    for (const QString name : {QString("SelectionGuide"), QString("MaterialGuideThumbnail")}) {
        auto* guide = findChild<ui::MachiningGuide*>(name);
        if (!guide) continue;
        auto* model = guide->model();
        // Batch document updates: rebuild the preview once, not once per field.
        {
            QSignalBlocker block(model);
            model->setValue("width", document_.material.width);
            model->setValue("height", document_.material.height);
            model->setValue("thickness", document_.material.thickness);
            model->setValue("clearance", document_.material.clearance_z);
            model->setValue("originX", QStringList{"left", "center", "right"}.indexOf(QString::fromStdString(document_.material.zero_x)));
            model->setValue("originY", QStringList{"bottom", "center", "top"}.indexOf(QString::fromStdString(document_.material.zero_y)));
            model->setValue("zeroZ", document_.material.zero_z == "bottom" ? 1 : 0);
            if (name == "SelectionGuide") {
                model->setContour(contour, all_closed);
                model->setValue("side", 2);
                model->setValue("openSide", 2);
            }
        }
        if (name == "SelectionGuide") guide->set_kind(contour.isEmpty() ? "08_material" : "01_usinar_vetores");
        emit model->changed();
    }
}

void MainWindow::refresh_document_view() {
    if (simulation_timer_ && simulation_timer_->isActive()) simulation_timer_->stop();
    simulation_.reset();
    refresh_simulation_status();
    canvas_widget_->set_material(document_.material.width, document_.material.height);
    canvas_widget_->canvas->set_document(&document_);
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
