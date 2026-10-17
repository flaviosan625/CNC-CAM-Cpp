#pragma once

#include <QWidget>
#include <QString>

#include "cad/model3d.hpp"

class QComboBox;
class QDoubleSpinBox;
class QTreeWidget;
class QCheckBox;
namespace cnc::vp { class Viewport3D; }

namespace cnc::ui {
class Modeling3DWorkspace final : public QWidget {
    Q_OBJECT
public:
    explicit Modeling3DWorkspace(QWidget* parent = nullptr);
private:
    void add_feature();
    void select_feature(int row);
    void apply_feature();
    void rebuild_tree();
    void rebuild_view();
    void new_part();
    void open_part();
    void save_part_as();
    void save_part();
    void sync_form(const cad::model3d::Extrusion& feature);
    cad::model3d::Extrusion read_form() const;
    cad::model3d::Part part_;
    QString file_path_;
    QTreeWidget* tree_ = nullptr;
    vp::Viewport3D* viewport_ = nullptr;
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
    int editing_row_ = -1;
};
} // namespace cnc::ui
