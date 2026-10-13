#include "app/main_window.hpp"

#include <algorithm>
#include <filesystem>

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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabBar>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/theme.hpp"
#include "ui/icons.hpp"
#include "cam/gcode.hpp"
#include "cam/setup_sheet.hpp"
#include "io/importers.hpp"
#include "tools/draw_tools.hpp"
#include "tools/edit_tools.hpp"
#include "tools/transform_tools.hpp"
#include "viewport/canvas_2d.hpp"
#include "viewport/gl_widget.hpp"

namespace cnc::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), tool_context_(document_, undo_stack_) {
    setWindowTitle("CNC-CAM");
    // The stylesheet is installed on QApplication in main.cpp.  A local
    // stylesheet here used to leave top-level dialogs and helper widgets on
    // the native Windows style.
    setMinimumSize(820, 520);
    resize(1500, 920);
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
        resizeDocks({left_dock, right_dock}, {340, 290}, Qt::Horizontal);
    activate_tool("select");

    connect(canvas_widget_, &vp::Canvas2DWidget::mouse_moved, this,
            [this](double x, double y) {
                coordinate_label_->setText(QString("X: %1   Y: %2 mm")
                                               .arg(x, 0, 'f', 2)
                                               .arg(y, 0, 'f', 2));
            });
    connect(view_tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (view_stack_) view_stack_->setCurrentIndex(index);
        if (index == 1 && viewport_3d_) viewport_3d_->fit_scene();
    });
    connect(canvas_widget_, &vp::Canvas2DWidget::files_dropped, this,
            [this](const QStringList& paths, double, double) { import_files(paths); });

    create_actions();
    refresh_document_view();
    statusBar()->showMessage(QFile::exists(":/assets/check.svg")
                                 ? "0 objeto(s) selecionado(s)"
                                 : "recursos FALTANDO");
}

void MainWindow::create_toolpath_panel() {
    auto* dock = new QDockWidget("Selecao", this);
    dock->setObjectName("ToolpathPanel");
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(280);

    auto* shell = new QWidget(dock);
    auto* shell_layout = new QHBoxLayout(shell);
    shell_layout->setContentsMargins(0, 0, 0, 0);
    shell_layout->setSpacing(0);

    auto* rail = new QWidget(shell);
    rail->setObjectName("RightToolRail");
    rail->setFixedWidth(48);
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
    auto* toolpath_button = new QToolButton(rail);
    toolpath_button->setObjectName("RightRailButton");
    toolpath_button->setCheckable(true);
    toolpath_button->setFixedSize(40, 34);
    toolpath_button->setIcon(ui::get_icon("rotate"));
    toolpath_button->setIconSize(QSize(22, 22));
    toolpath_button->setToolTip("Percursos CAM");
    rail_layout->addWidget(toolpath_button);
    rail_layout->addStretch();
    shell_layout->addWidget(rail);

    auto* stack = new QStackedWidget(shell);
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
    auto* preview = new QFrame(selection_page);
    preview->setFixedSize(150, 90);
    preview->setStyleSheet("QFrame { background: #ffffff; border: 1px solid #d5d9df; border-radius: 8px; }");
    auto* preview_layout = new QVBoxLayout(preview);
    preview_layout->setContentsMargins(40, 28, 40, 28);
    auto* preview_shape = new QFrame(preview);
    preview_shape->setStyleSheet("QFrame { background: #ffffff; border: 2px solid #5b5b5b; border-radius: 0; }");
    preview_layout->addWidget(preview_shape);
    selection_layout->addWidget(preview, 0, Qt::AlignHCenter);
    selection_layout->addStretch();
    stack->addWidget(selection_page);

    auto* toolpath_page = new QWidget(stack);
    auto* layout = new QVBoxLayout(toolpath_page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);
    auto* title = new QLabel("PERCURSOS", toolpath_page);
    title->setObjectName("SectionHeader");
    layout->addWidget(title);
    auto* material_button = new QPushButton("Definicoes do material...", toolpath_page);
    layout->addWidget(material_button);
    connect(material_button, &QPushButton::clicked, this, &MainWindow::edit_material_setup);
    toolpath_list_ = new QListWidget(toolpath_page);
    toolpath_list_->setObjectName("ToolpathList");
    toolpath_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(toolpath_list_, 1);
    auto* type_label = new QLabel("Novo percurso", toolpath_page);
    type_label->setObjectName("PropLabel");
    layout->addWidget(type_label);
    toolpath_type_combo_ = new QComboBox(toolpath_page);
    const std::vector<std::pair<QString, cam::ToolpathType>> kinds = {
        {"Perfil", cam::ToolpathType::Profile}, {"Bolso", cam::ToolpathType::Pocket},
        {"Furacao", cam::ToolpathType::Drill}, {"V-carve", cam::ToolpathType::VCarve},
        {"Fluting", cam::ToolpathType::Fluting}, {"Textura", cam::ToolpathType::Texture},
        {"Keyhole", cam::ToolpathType::Keyhole}, {"Inlay", cam::ToolpathType::Inlay},
        {"Drag knife", cam::ToolpathType::Dragknife},
    };
    for (const auto& [label, kind] : kinds) toolpath_type_combo_->addItem(label, static_cast<int>(kind));
    layout->addWidget(toolpath_type_combo_);
    auto* add_button = new QPushButton("Adicionar percurso", toolpath_page);
    add_button->setProperty("primary", true);
    layout->addWidget(add_button);
    connect(add_button, &QPushButton::clicked, this, &MainWindow::add_toolpath_from_panel);
    auto* recalc_button = new QPushButton("Recalcular todos", toolpath_page);
    layout->addWidget(recalc_button);
    connect(recalc_button, &QPushButton::clicked, this, &MainWindow::recalculate_toolpaths);
    auto* row = new QHBoxLayout();
    auto* edit_button = new QPushButton("Editar", toolpath_page);
    auto* toggle_button = new QPushButton("Mostrar/ocultar", toolpath_page);
    auto* delete_button = new QPushButton("Excluir", toolpath_page);
    row->addWidget(edit_button);
    row->addWidget(toggle_button);
    row->addWidget(delete_button);
    layout->addLayout(row);
    connect(edit_button, &QPushButton::clicked, this, &MainWindow::edit_current_toolpath);
    connect(toggle_button, &QPushButton::clicked, this, &MainWindow::toggle_current_toolpath);
    connect(delete_button, &QPushButton::clicked, this, &MainWindow::delete_current_toolpath);
    simulate_button_ = new QPushButton("Simular percursos", toolpath_page);
    simulate_button_->setObjectName("SimulatePathBtn");
    layout->addWidget(simulate_button_);
    connect(simulate_button_, &QPushButton::clicked, this, &MainWindow::toggle_simulation);
    simulation_timer_ = new QTimer(this);
    simulation_timer_->setInterval(30);
    connect(simulation_timer_, &QTimer::timeout, this, &MainWindow::update_simulation);
    connect(toolpath_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            statusBar()->showMessage(QString::fromStdString(document_.toolpaths[static_cast<size_t>(row)]->summary()));
    });
    stack->addWidget(toolpath_page);
    shell_layout->addWidget(stack, 1);
    dock->setWidget(shell);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(selection_button, &QToolButton::clicked, this, [stack, selection_button, toolpath_button] {
        stack->setCurrentIndex(0);
        selection_button->setChecked(true);
        toolpath_button->setChecked(false);
    });
    connect(toolpath_button, &QToolButton::clicked, this, [stack, selection_button, toolpath_button] {
        stack->setCurrentIndex(1);
        selection_button->setChecked(false);
        toolpath_button->setChecked(true);
    });
    connect(collapse, &QToolButton::clicked, dock, [dock] { dock->setVisible(false); });
    refresh_toolpaths();
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

    simulate_button_ = new QPushButton("Simular percursos", panel);
    simulate_button_->setObjectName("SimulatePathBtn");
    layout->addWidget(simulate_button_);
    connect(simulate_button_, &QPushButton::clicked, this, &MainWindow::toggle_simulation);

    simulation_timer_ = new QTimer(this);
    simulation_timer_->setInterval(30);
    connect(simulation_timer_, &QTimer::timeout, this, &MainWindow::update_simulation);
    connect(toolpath_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(document_.toolpaths.size()))
            statusBar()->showMessage(QString::fromStdString(document_.toolpaths[static_cast<size_t>(row)]->summary()));
    });

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    refresh_toolpaths();
}

void MainWindow::refresh_toolpaths() {
    if (!toolpath_list_) return;
    if (simulation_timer_ && simulation_timer_->isActive()) simulation_timer_->stop();
    simulation_.reset();
    if (simulate_button_) simulate_button_->setText("Simular percursos");
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

    const auto visible = cam::calculated_toolpaths(document_.toolpaths);
    canvas_widget_->canvas->refresh_toolpaths(visible);
    if (!viewport_3d_->isValid()) return;

    viewport_3d_->clear_entities();
    viewport_3d_->clear_simulation();
    for (const cam::Toolpath* path : visible) {
        std::vector<float> vertices;
        if (path->moves.size() < 2) continue;
        for (size_t i = 1; i < path->moves.size(); ++i) {
            const cam::Move& a = path->moves[i - 1];
            const cam::Move& b = path->moves[i];
            const bool rapid = b.kind == cam::MoveKind::Rapid;
            const float r = rapid ? 0.42f : 0.28f;
            const float g = rapid ? 0.52f : 0.88f;
            const float bl = rapid ? 0.65f : 0.52f;
            vertices.insert(vertices.end(), {static_cast<float>(a.x), static_cast<float>(a.y),
                                              static_cast<float>(a.z), r, g, bl,
                                              static_cast<float>(b.x), static_cast<float>(b.y),
                                              static_cast<float>(b.z), r, g, bl});
        }
        if (!vertices.empty()) viewport_3d_->add_entity_lines(vertices);
    }
    viewport_3d_->fit_scene();
}

void MainWindow::edit_material_setup() {
    QDialog dialog(this);
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

void MainWindow::add_toolpath_from_panel() {
    if (!toolpath_type_combo_) return;
    const auto selected = document_.selected_entities();
    std::vector<std::string> ids;
    const auto& source = selected.empty() ? document_.entities : selected;
    for (const auto& entity : source) ids.push_back(entity->id);
    if (ids.empty()) {
        statusBar()->showMessage("Desenhe ou selecione vetores antes de criar o percurso");
        return;
    }

    const auto kind = static_cast<cam::ToolpathType>(toolpath_type_combo_->currentData().toInt());
    auto job = std::make_shared<cam::ToolpathJob>();
    job->kind = kind;
    job->settings = cam::default_settings(kind);
    job->name = document_.unique_toolpath_name(QString::fromUtf8(cam::toolpath_label(kind)).toStdString());
    job->entity_ids = std::move(ids);
    const auto tools = cam::default_tools();
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

    QDialog dialog(this);
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
    if (simulation_timer_->isActive()) {
        simulation_timer_->stop();
        simulate_button_->setText("Continuar simulação");
        return;
    }
    if (simulation_ && simulation_->finished()) simulation_->reset();
    if (!simulation_) {
        std::vector<sim::SimulationOperation> operations;
        for (const auto& job : document_.toolpaths) {
            if (!job->visible) continue;
            for (const auto& path : job->results) operations.push_back({path, job->tool});
        }
        if (operations.empty()) {
            statusBar()->showMessage("Recalcule pelo menos um percurso antes de simular");
            return;
        }
        const auto& material = document_.material;
        auto config = sim::HeightmapConfig::for_stock(material.width, material.height,
                                                       material.thickness, material.surface_z());
        simulation_ = std::make_unique<sim::ToolpathSimulation>(std::move(operations), config,
                                                                material.surface_z() - material.thickness);
    }
    simulation_timer_->start();
    simulate_button_->setText("Pausar simulação");
    view_tabs_->setCurrentIndex(1);
}

void MainWindow::update_simulation() {
    if (!simulation_ || !viewport_3d_->isValid()) return;
    simulation_->advance(0.03);
    const auto mesh = simulation_->stock_mesh({0.72f, 0.55f, 0.32f});
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
    if (simulation_->finished()) {
        simulation_timer_->stop();
        simulate_button_->setText("Reiniciar simulação");
        statusBar()->showMessage("Simulação concluída");
    }
}

void MainWindow::export_gcode() {
    const auto paths = cam::calculated_toolpaths(document_.toolpaths);
    if (paths.empty()) {
        statusBar()->showMessage("Recalcule um percurso antes de gerar G-Code");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Guardar G-Code", {},
                                                       "G-Code (*.nc *.tap *.gcode);;Todos os arquivos (*)");
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Erro ao guardar G-Code", output.errorString());
        return;
    }
    const std::string code = cam::generate_gcode(paths, document_.material, {}, document_.title());
    output.write(QByteArray::fromStdString(code));
    output.close();
    statusBar()->showMessage(QString("G-Code guardado: %1").arg(path));
}

void MainWindow::export_setup_sheet() {
    const auto paths = cam::calculated_toolpaths(document_.toolpaths);
    if (paths.empty()) {
        statusBar()->showMessage("Recalcule um percurso antes de criar a folha de trabalho");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Criar folha de trabalho", {},
                                                       "HTML (*.html);;Todos os arquivos (*)");
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Erro ao guardar folha", output.errorString());
        return;
    }
    const std::string html = cam::setup_sheet_html(paths, document_.material, document_.title());
    output.write(QByteArray::fromStdString(html));
    output.close();
    statusBar()->showMessage(QString("Folha de trabalho criada: %1").arg(path));
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
    dock->setMinimumWidth(340);

    auto* panel = new QWidget(dock);
    panel->setMinimumWidth(340);
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
        auto* grid = new QGridLayout();
        grid->setContentsMargins(0, 2, 0, 4);
        grid->setHorizontalSpacing(2);
        grid->setVerticalSpacing(3);
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& [name, label] = entries[i];
            auto* tile = new QWidget(body);
            tile->setObjectName("ToolTile");
            tile->setFixedWidth(38);
            auto* tile_layout = new QVBoxLayout(tile);
            tile_layout->setContentsMargins(0, 0, 0, 0);
            tile_layout->setSpacing(2);
            auto* button = new QToolButton(tile);
            button->setObjectName("ToolBtn");
            button->setFixedSize(34, 34);
            button->setIcon(ui::get_icon(name));
            button->setIconSize(QSize(28, 28));
            button->setToolTip(label);
            button->setCursor(Qt::PointingHandCursor);
            button->setCheckable(true);
            button->setProperty("toolName", name);
            group->addButton(button);
            tile_layout->addWidget(button, 0, Qt::AlignHCenter);
            auto* caption = new QLabel(label, tile);
            caption->setObjectName("ToolCaption");
            caption->setStyleSheet("font-size: 9px;");
            caption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            caption->setWordWrap(true);
            caption->setVisible(!label.isEmpty());
            tile_layout->addWidget(caption);
            grid->addWidget(tile, static_cast<int>(i / columns), static_cast<int>(i % columns),
                            Qt::AlignHCenter | Qt::AlignTop);
            connect(button, &QToolButton::clicked, this, [this, name] {
                if (name == "zoom_fit") canvas_widget_->canvas->fit_view();
                else if (name == "zoom_in") canvas_widget_->canvas->zoom_by(1.25);
                else if (name == "zoom_out") canvas_widget_->canvas->zoom_by(0.8);
                else if (name != "pan") activate_tool(name);
            });
        }
        layout->addLayout(grid);
    };

    add_section("ARQUIVO E VISTA 2D", {
        {"import", "Importar"}, {"export", "Exportar"}, {"preview_3d", "Previa 3D"},
        {"measure", "Medir"}, {"grid", "Grade"}, {"workspace_setup", "Area de trabalho"},
        {"select", "Selecao"}}, 7);
    add_separator();

    auto* layer_row = new QHBoxLayout();
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
    auto* layer_glyph = new QLabel(layer_bar);
    layer_glyph->setObjectName("LayerGlyph");
    layer_glyph->setPixmap(ui::get_icon("layers").pixmap(18, 18));
    layer_layout->addWidget(layer_glyph);
    layer_combo_ = new QComboBox(layer_bar);
    layer_combo_->setObjectName("LayerCombo");
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
    layer_row->addWidget(layer_bar, 1);
    layout->addLayout(layer_row);
    add_separator();

    add_section("CRIAR VETORES", {
        {"circle", ""}, {"ellipse", ""}, {"rectangle", ""}, {"polygon", ""},
        {"star", ""}, {"polyline", "Polilinha"}, {"text", "Texto"}, {"line", ""},
        {"arc", ""}, {"construction_line", ""}, {"freehand", "Mao livre"},
        {"drill_point", "Furacao"}, {"vector_texture", "Textura"}, {"text_on_curve", ""},
        {"dimension", "Cotas"}, {"trace_bitmap", "Vetorizar"}}, 8);
    add_separator();
    add_section("TRANSFORMAR OBJETOS", {
        {"move", ""}, {"distort", "Distorcer"}, {"mirror", "Espelhar"}, {"scale", ""},
        {"array_copy", "Copia array"}, {"circular_copy", ""}, {"rotate", ""}}, 7);
    add_separator();
    add_section("EDITAR OBJETOS", {
        {"node_edit", "Nos"}, {"fillet", "Filete"},
        {"interactive_trim", "Aparar interativo"}, {"extend", ""}, {"fit_curves", "Ajustar curvas"},
        {"weld", "Soldar"}, {"subtract", ""}, {"intersect", ""},
        {"group", "Agrupar"}, {"ungroup", ""}, {"offset", "Offset"}, {"boundary", ""},
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
        canvas_widget_->canvas->refresh_entities();
    });
    connect(layer_visible_, &QAbstractButton::toggled, this, [this](bool visible) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->visible = visible;
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
        }
    });
    connect(layer_locked_, &QAbstractButton::toggled, this, [this](bool locked) {
        if (auto* layer = document_.get_layer(tool_context_.current_layer)) {
            layer->locked = locked;
            document_.modified = true;
            canvas_widget_->canvas->refresh_entities();
        }
    });
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

void MainWindow::activate_tool(const QString& name) {
    if (active_tool_) active_tool_->deactivate();

    if (name == "select") active_tool_ = std::make_unique<tools::SelectTool>(tool_context_);
    else if (name == "line") active_tool_ = std::make_unique<tools::LineTool>(tool_context_);
    else if (name == "polyline") active_tool_ = std::make_unique<tools::PolylineTool>(tool_context_);
    else if (name == "rectangle") active_tool_ = std::make_unique<tools::RectangleTool>(tool_context_);
    else if (name == "circle") active_tool_ = std::make_unique<tools::CircleTool>(tool_context_);
    else if (name == "ellipse") active_tool_ = std::make_unique<tools::EllipseTool>(tool_context_);
    else if (name == "arc") active_tool_ = std::make_unique<tools::ArcTool>(tool_context_);
    else if (name == "drill_point") active_tool_ = std::make_unique<tools::DrillPointTool>(tool_context_);
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
    else if (name == "move") active_tool_ = std::make_unique<tools::MoveTool>(tool_context_);
    else if (name == "rotate") active_tool_ = std::make_unique<tools::RotateTool>(tool_context_);
    else if (name == "scale") active_tool_ = std::make_unique<tools::ScaleTool>(tool_context_);
    else if (name == "mirror") active_tool_ = std::make_unique<tools::MirrorTool>(tool_context_);
    else active_tool_ = std::make_unique<tools::SelectTool>(tool_context_);

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
            if (toolpath_type_combo_) toolpath_type_combo_->setCurrentIndex(
                toolpath_type_combo_->findData(static_cast<int>(kind)));
            add_toolpath_from_panel();
        });
    }
    cam_menu->addSeparator();
    cam_menu->addAction("Recalcular todos os percursos", QKeySequence(Qt::Key_F9), this,
                        &MainWindow::recalculate_toolpaths);
    cam_menu->addAction("Simular percursos", QKeySequence(), this, &MainWindow::toggle_simulation);
    cam_menu->addSeparator();
    cam_menu->addAction("Criar folha de trabalho", QKeySequence(), this, &MainWindow::export_setup_sheet);
    cam_menu->addAction("Guardar G-Code", QKeySequence("Ctrl+Shift+G"), this,
                        &MainWindow::export_gcode);

    QMenu* help_menu = menuBar()->addMenu("Ajuda");
    help_menu->addAction("Sobre o CNC-CAM", this, [this] {
        QMessageBox::about(this, "CNC-CAM", "CNC-CAM - CAD/CAM para CNC");
    });
}

void MainWindow::refresh_document_view() {
    if (simulation_timer_ && simulation_timer_->isActive()) simulation_timer_->stop();
    simulation_.reset();
    if (simulate_button_) simulate_button_->setText("Simular percursos");
    canvas_widget_->set_material(document_.material.width, document_.material.height);
    canvas_widget_->canvas->set_document(&document_);
    refresh_layers();
    // QOpenGLWidget ainda nao possui contexto durante a construcao da janela.
    // A limpeza dos buffers so pode ocorrer depois que a viewport foi mostrada.
    if (viewport_3d_->isValid()) {
        viewport_3d_->clear_entities();
        viewport_3d_->clear_simulation();
    }
    viewport_3d_->set_workpiece(document_.material.width, document_.material.height,
                                 -document_.material.thickness, 0.0);
    viewport_3d_->fit_scene();
    refresh_toolpaths();
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
