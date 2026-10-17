#pragma once

#include <memory>
#include <vector>

#include <QWidget>
#include <QString>

#include "cad/model3d.hpp"
#include "cad/relief.hpp"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace cnc::vp { class Viewport3D; }

namespace cnc::ui {

class Modeling3DWorkspace final : public QWidget {
    Q_OBJECT
public:
    explicit Modeling3DWorkspace(QWidget* parent = nullptr);

private:
    void setup_ui();
    void setup_top_bar();
    QWidget* create_left_panel();
    QWidget* create_cad_page();
    QWidget* create_relief_page();
    QWidget* create_center_panel();
    QWidget* create_right_panel();
    QWidget* create_bottom_sculpt_bar();

    // CAD Extrusion handlers
    void add_extrusion();
    void apply_extrusion();
    void sync_cad_form(const cad::model3d::Extrusion& f);
    cad::model3d::Extrusion read_cad_form() const;

    // Relief handlers
    void open_relief_image();
    void add_relief_component();
    void apply_relief_component();
    void set_combine_mode(cad::relief::CombineMode mode);
    void sync_relief_form(const cad::relief::ReliefComponent& c);
    cad::relief::ReliefComponent read_relief_form() const;

    // Tree actions
    void select_tree_item(QTreeWidgetItem* item);
    void toggle_selected_visibility();
    void toggle_selected_lock();
    void move_selected_up();
    void move_selected_down();
    void delete_selected();

    // Project persistence
    void new_part();
    void open_part();
    void save_part();
    void save_part_as();

    // Rebuild routines
    void rebuild_tree();
    void rebuild_view();

    // Data model
    cad::model3d::Part part_;
    QString file_path_;
    int editing_cad_idx_ = -1;
    int editing_relief_idx_ = -1;
    double preview_resolution_mm_ = 0.5;
    double light_angle_deg_ = 45.0;
    double max_total_height_ = 200.0;

    // Undo buffer for sculpting / relief
    std::vector<cad::relief::ReliefComponent> relief_undo_backup_;

    // UI widgets
    QTreeWidget* tree_ = nullptr;
    vp::Viewport3D* viewport_ = nullptr;
    QTabWidget* left_tabs_ = nullptr;

    // CAD widgets
    QComboBox* profile_ = nullptr;
    QComboBox* end_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QDoubleSpinBox* depth_ = nullptr;
    QDoubleSpinBox* start_z_ = nullptr;
    QDoubleSpinBox* target_z_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QDoubleSpinBox* draft_ = nullptr;
    QDoubleSpinBox* depth2_ = nullptr;
    QCheckBox* reverse_ = nullptr;
    QCheckBox* two_directions_ = nullptr;
    QCheckBox* suppressed_ = nullptr;

    // Relief widgets
    QLabel* img_preview_label_ = nullptr;
    QLabel* img_status_label_ = nullptr;
    QSlider* contrast_slider_ = nullptr;
    QDoubleSpinBox* contrast_spin_ = nullptr;
    QCheckBox* invert_check_ = nullptr;
    QRadioButton* nature_raised_ = nullptr;
    QRadioButton* nature_carved_ = nullptr;
    QDoubleSpinBox* shape_height_ = nullptr;
    QDoubleSpinBox* base_height_ = nullptr;
    QSlider* smoothness_slider_ = nullptr;
    QLabel* smoothness_label_ = nullptr;
    QDoubleSpinBox* relief_w_ = nullptr;
    QDoubleSpinBox* relief_h_ = nullptr;
    QDoubleSpinBox* relief_x_ = nullptr;
    QDoubleSpinBox* relief_y_ = nullptr;
    std::vector<QPushButton*> combine_mode_buttons_;
    cad::relief::CombineMode current_combine_mode_ = cad::relief::CombineMode::Add;

    // Sculpting widgets
    QPushButton* sculpt_smooth_btn_ = nullptr;
    QPushButton* sculpt_smudge_btn_ = nullptr;
    QPushButton* sculpt_elevate_btn_ = nullptr;
    QPushButton* sculpt_remove_btn_ = nullptr;
    QPushButton* sculpt_undo_btn_ = nullptr;
    QDoubleSpinBox* brush_size_spin_ = nullptr;
    QSlider* brush_strength_slider_ = nullptr;
    QDoubleSpinBox* max_height_spin_ = nullptr;
    cad::relief::SculptTool active_sculpt_tool_ = cad::relief::SculptTool::Smooth;

    // Viewport HUD controls
    QComboBox* resolution_combo_ = nullptr;
    QSlider* light_slider_ = nullptr;
};

} // namespace cnc::ui
