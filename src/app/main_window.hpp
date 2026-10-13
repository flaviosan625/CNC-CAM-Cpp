// Janela principal (porte de src/app.py). Em construcao: cresce junto com os paineis.
#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>
#include <QStringList>

#include "cam/job.hpp"
#include "cad/document.hpp"
#include "sim/playback.hpp"
#include "tools/base.hpp"
#include "utils/commands.hpp"

class QLabel;
class QTabBar;
class QStackedWidget;
class QAbstractButton;
class QComboBox;
class QCheckBox;
class QListWidget;
class QPushButton;
class QTimer;

namespace cnc::vp {
class Canvas2DWidget;
class Viewport3D;
}  // namespace cnc::vp

namespace cnc::app {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool open_file(const QString& path);

private:
    void create_actions();
    void create_tool_panel();
    void create_legacy_tool_panel();
    void create_toolpath_panel();
    void create_legacy_toolpath_panel();
    void activate_tool(const QString& name);
    void activate_dimension(const QString& mode);
    void refresh_document_view();
    void edit_material_setup();
    void refresh_layers();
    void add_layer_from_panel();
    void remove_current_layer();
    void refresh_toolpaths();
    void add_toolpath_from_panel();
    bool configure_toolpath(cam::ToolpathJob& job);
    void edit_current_toolpath();
    void duplicate_current_toolpath();
    void delete_current_toolpath();
    void toggle_current_toolpath();
    void move_current_toolpath(int delta);
    void recalculate_toolpaths();
    void toggle_simulation();
    void update_simulation();
    void export_gcode();
    void export_setup_sheet();
    void export_svg();
    void import_files(const QStringList& paths);
    void import_file(const QString& path);
    void import_folder();
    bool save_file(const QString& path = {});

    cad::Document document_;
    UndoStack undo_stack_;
    tools::ToolContext tool_context_;
    std::unique_ptr<tools::BaseTool> active_tool_;
    QTabBar* view_tabs_ = nullptr;
    QStackedWidget* view_stack_ = nullptr;
    cnc::vp::Canvas2DWidget* canvas_widget_ = nullptr;
    cnc::vp::Viewport3D* viewport_3d_ = nullptr;
    QLabel* coordinate_label_ = nullptr;
    QComboBox* layer_combo_ = nullptr;
    QAbstractButton* layer_visible_ = nullptr;
    QAbstractButton* layer_locked_ = nullptr;
    QComboBox* toolpath_type_combo_ = nullptr;
    QListWidget* toolpath_list_ = nullptr;
    QStackedWidget* toolpath_list_stack_ = nullptr;
    QLabel* toolpath_size_label_ = nullptr;
    QLabel* toolpath_origin_label_ = nullptr;
    QLabel* toolpath_empty_label_ = nullptr;
    QPushButton* simulate_button_ = nullptr;
    QTimer* simulation_timer_ = nullptr;
    std::unique_ptr<sim::ToolpathSimulation> simulation_;
};

}  // namespace cnc::app
