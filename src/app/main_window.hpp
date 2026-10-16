// Janela principal (porte de src/app.py). Em construcao: cresce junto com os paineis.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QMainWindow>
#include <QPointer>
#include <QPoint>
#include <QStringList>

#include "cam/job.hpp"
#include "cam/job_sheet.hpp"
#include "cam/setup_sheet.hpp"
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
class QProgressBar;
class QPushButton;
class QDockWidget;
class QDialog;
class QScrollArea;
class QSlider;
class QTimer;
class QToolButton;
class QTableWidget;
class QWidget;
class QLineEdit;
class QPlainTextEdit;

namespace cnc::vp {
class Canvas2DWidget;
class Viewport3D;
}  // namespace cnc::vp

namespace cnc::ui { class ImportSetupPanel; }

namespace cnc::app {

// Layout of the job-sheet drawing, in logical units.  The image comes back
// `supersample` times larger so the dimensions and callouts stay sharp once the
// PDF stretches the drawing across a landscape page.
struct JobPreviewOptions {
    int width = 980;           // logical canvas width
    // The height follows the projected stock so the drawing fills the frame
    // instead of being padded with empty space, within these bounds.
    int min_height = 300;
    int max_height = 540;
    double supersample = 3.0;  // device pixels per logical unit
    int only_operation = -1;   // -1 draws every toolpath
};

QImage make_job_layout_preview(const std::vector<cam::SetupSheetOperation>& operations,
                               const cad::MaterialSetup* material = nullptr,
                               const JobPreviewOptions& options = {});

// One drawing per toolpath, each isolated on a taller canvas, for the sheets
// that follow the summary page of the job sheet.
std::vector<QImage> make_operation_previews(const std::vector<cam::SetupSheetOperation>& operations,
                                            const cad::MaterialSetup* material = nullptr);

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool open_file(const QString& path);
    void show_3d_view();
    void activate_tool(const QString& name);
    cad::Document& document() noexcept { return document_; }
    const cad::Document& document() const noexcept { return document_; }

private:
    void create_actions();
    void create_tool_panel();
    void create_legacy_tool_panel();
    void create_toolpath_panel();
    void create_order_service_page();
    void open_order_service_page();
    void refresh_order_service_page();
    bool recalculate_order_service_if_geometry_changed();
    void generate_order_service_pdf();
    void create_legacy_toolpath_panel();
    void activate_dimension(const QString& mode);
    void edit_dimension_style();
    void refresh_document_view();
    void schedule_selection_previews();
    void refresh_guide_previews();
    void refresh_3d_view();
    void edit_material_setup();
    void refresh_layers();
    void add_layer_from_panel();
    void remove_current_layer();
    void manage_layers();
    void quick_edit_entity(const QString& entity_id);
    void show_canvas_context_menu(const QPoint& global_pos, double wx, double wy);
    void refresh_toolpaths();
    void refresh_bridge_markers();
    void start_manual_bridge_preparation();
    void add_manual_bridge(double x, double y);
    void move_bridge_marker(int index, double x, double y);
    void delete_bridge_marker(int index);
    void stop_manual_bridge_mode();
    bool apply_bridge_preparation(cam::ToolpathJob& job);
    void edit_tool_database();
    bool edit_tool_database(const std::string& preselected_tool_name, cam::Tool* out_tool = nullptr);
    void add_toolpath_from_panel();
    bool configure_toolpath(cam::ToolpathJob& job);
    void edit_current_toolpath();
    void duplicate_current_toolpath();
    void delete_current_toolpath();
    void toggle_current_toolpath();
    void move_current_toolpath(int delta);
    void recalculate_toolpaths();
    void toggle_simulation();
    void begin_simulation(const QString& scope);
    void play_simulation();
    void pause_simulation();
    void step_simulation();
    void finish_simulation();
    void restart_simulation();
    void update_simulation();
    void refresh_simulation_view();
    void refresh_simulation_status();
    void show_right_page(int index);
    void set_right_panel_collapsed(bool collapsed);
    void refresh_automatic_cut_panel();
    void create_automatic_cut();
    void classify_imported_entities(const std::vector<cad::EntityPtr>& entities);
    void begin_import_preparation();
    void refresh_import_preparation();
    bool apply_import_preparation();
    void assign_selected_recess();
    bool calculate_prepared_recesses(const std::vector<std::string>& scope = {});
    void generate_prepared_recesses();
    void show_guides_gallery();
    void export_gcode();
    void export_setup_sheet();
    void export_svg();
    void import_files(const QStringList& paths);
    void import_file(const QString& path);
    void import_folder();
    bool save_file(const QString& path = {});
    bool eventFilter(QObject* watched, QEvent* event) override;

    struct RegisteredJobRecord {
        std::string id;
        std::string name;
        std::string part_name;
        std::string material_profile;
        double width = 0.0;
        double height = 0.0;
        double thickness = 0.0;
        std::string area_m2;
        std::string gcode;
        double total_seconds = 0.0;
        double total_cut_m = 0.0;
        QDateTime timestamp;
        std::vector<cam::SetupSheetOperation> operations;
        cad::MaterialSetup material;
        cam::SetupSheetInfo info;
    };
    std::vector<RegisteredJobRecord> job_history_;
    std::vector<std::pair<std::string, uint64_t>> order_service_geometry_versions_;
    bool order_service_geometry_snapshot_valid_ = false;
    bool order_service_preview_dirty_ = true;

    cad::Document document_;
    std::vector<cad::EntityPtr> clipboard_entities_;
    UndoStack undo_stack_;
    tools::ToolContext tool_context_;
    std::unique_ptr<tools::BaseTool> active_tool_;
    QTabBar* view_tabs_ = nullptr;
    QWidget* view_3d_controls_ = nullptr;
    QWidget* order_service_page_ = nullptr;
    QToolButton* projection_3d_button_ = nullptr;
    std::optional<std::array<double, 4>> view_3d_scene_bounds_;
    QStackedWidget* view_stack_ = nullptr;
    QStackedWidget* right_panel_stack_ = nullptr;
    QStackedWidget* order_service_steps_ = nullptr;
    QTabBar* order_service_step_tabs_ = nullptr;
    QLabel* order_service_preview_label_ = nullptr;
    QLabel* order_service_state_label_ = nullptr;
    QLabel* order_service_dimensions_label_ = nullptr;
    QLabel* order_service_origin_label_ = nullptr;
    QLabel* order_service_time_label_ = nullptr;
    QLabel* order_service_cut_label_ = nullptr;
    QLabel* order_service_depth_label_ = nullptr;
    QLabel* order_service_material_label_ = nullptr;
    QLabel* order_service_material_dims_label_ = nullptr;
    QLabel* order_service_material_area_label_ = nullptr;
    QLabel* order_service_margin_label_ = nullptr;
    QLabel* order_service_zero_xy_label_ = nullptr;
    QLabel* order_service_zero_z_label_ = nullptr;
    QLabel* order_service_clearance_label_ = nullptr;
    QLabel* order_service_plunge_gap_label_ = nullptr;
    QLabel* order_service_offset_label_ = nullptr;
    QTableWidget* order_service_operations_table_ = nullptr;
    QLineEdit* order_service_customer_ = nullptr;
    QLineEdit* order_service_contact_ = nullptr;
    QLineEdit* order_service_number_ = nullptr;
    QLineEdit* order_service_part_ = nullptr;
    QLineEdit* order_service_revision_ = nullptr;
    QLineEdit* order_service_operator_ = nullptr;
    QPlainTextEdit* order_service_notes_ = nullptr;
    cnc::vp::Canvas2DWidget* canvas_widget_ = nullptr;
    cnc::vp::Viewport3D* viewport_3d_ = nullptr;
    QLabel* coordinate_label_ = nullptr;
    QComboBox* layer_combo_ = nullptr;
    QAbstractButton* layer_visible_ = nullptr;
    QAbstractButton* layer_locked_ = nullptr;
    QPointer<QWidget> quick_edit_popup_;
    QComboBox* toolpath_type_combo_ = nullptr;
    cam::ToolpathType toolpath_panel_kind_ = cam::ToolpathType::Profile;
    QListWidget* toolpath_list_ = nullptr;
    QStackedWidget* toolpath_list_stack_ = nullptr;
    QLabel* toolpath_size_label_ = nullptr;
    QLabel* toolpath_origin_label_ = nullptr;
    QLabel* toolpath_empty_label_ = nullptr;
    QLabel* simulation_operation_label_ = nullptr;
    QLabel* simulation_info_label_ = nullptr;
    QLabel* simulation_speed_label_ = nullptr;
    QProgressBar* simulation_progress_ = nullptr;
    QSlider* simulation_speed_slider_ = nullptr;
    QPushButton* simulation_play_button_ = nullptr;
    QPushButton* simulation_pause_button_ = nullptr;
    QPushButton* simulation_step_button_ = nullptr;
    QPushButton* simulation_finish_button_ = nullptr;
    QPushButton* simulation_restart_button_ = nullptr;
    QDockWidget* toolpath_dock_ = nullptr;
    int last_expanded_dock_width_ = 340;
    QScrollArea* right_panel_scroll_ = nullptr;
    QToolButton* selection_nav_button_ = nullptr;
    QToolButton* toolpath_nav_button_ = nullptr;
    QToolButton* simulation_nav_button_ = nullptr;
    QToolButton* order_service_nav_button_ = nullptr;
    QToolButton* automatic_nav_button_ = nullptr;
    QComboBox* automatic_cut_combo_ = nullptr;
    ui::ImportSetupPanel* import_setup_panel_ = nullptr;
    bool import_preparation_pending_ = false;
    QLabel* automatic_cut_info_label_ = nullptr;
    QPushButton* automatic_create_button_ = nullptr;
    QTimer* selection_preview_timer_ = nullptr;
    bool selection_guide_dirty_ = true;
    bool material_guide_dirty_ = true;
    bool automatic_cut_panel_dirty_ = true;
    QTimer* simulation_timer_ = nullptr;
    std::unique_ptr<sim::ToolpathSimulation> simulation_;
    std::string bridge_target_job_id_;
    std::optional<cam::ToolpathJob> bridge_preparation_job_;
    QPointer<QDialog> active_toolpath_dialog_;
    std::function<void()> sync_toolpath_bridge_draft_;
    QWidget* bridge_mode_banner_ = nullptr;
    std::chrono::steady_clock::time_point simulation_last_tick_{};
};

}  // namespace cnc::app
