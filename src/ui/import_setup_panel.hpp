#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "cad/material.hpp"
#include "cam/tools.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QToolButton;

namespace cnc::ui {

struct ImportSetupValues {
    cad::MaterialSetup material;
    cam::Tool tool;
    bool has_recess = false;
    double recess_depth = 3.0;
};

// Preparacao do desenho importado, embutida no painel fixo da direita.
// Apresenta dados simplificados em fluxo 1-clique com presets da oficina e auto-ajuste de chapa.
class ImportSetupPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ImportSetupPanel(QWidget* parent = nullptr);

    void set_geometry_info(double width, double height, int closed_contours = 0, int total_entities = 0);
    void set_material(const cad::MaterialSetup& material);
    void set_tools(const std::vector<cam::Tool>& tools,
                   std::optional<cam::Tool> preferred = std::nullopt);
    void begin_import(const cad::MaterialSetup& material,
                      const std::vector<cam::Tool>& tools,
                      std::optional<cam::Tool> preferred = std::nullopt);
    void reset_recess_answer();
    void set_recess_answer(std::optional<bool> answer);
    void set_recess_depth(double depth);
    ImportSetupValues values() const;
    bool recess_answered() const;
    void set_status(const QString& text);
    void set_recess_selection_count(int count);
    void set_marked_recess_count(int count);
    int selected_preset_index() const;
    void select_house_preset(int preset_index);

signals:
    void applyRequested();
    void generateAutomaticCutRequested();
    void assignRecessRequested();
    void createRecessRequested();
    void manageLayersRequested();
    void openToolpathsRequested();
    void convertInchesRequested();

private:
    void refresh_recess_controls();
    void refresh_tool_summary();
    void apply_house_preset(int preset_index);
    void update_stock_dimensions();

    cad::MaterialSetup material_;
    std::vector<cam::Tool> tools_;
    double piece_width_ = 0.0;
    double piece_height_ = 0.0;
    int closed_contours_ = 0;
    int total_entities_ = 0;

    // Banner de dimensões reais da peça
    QLabel* geometry_banner_ = nullptr;
    QPushButton* inch_convert_button_ = nullptr;

    // Presets rápidos da oficina
    QComboBox* house_preset_combo_ = nullptr;

    // Modo da chapa
    QComboBox* stock_mode_combo_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QComboBox* sheet_preset_combo_ = nullptr;

    // Rebaixo (não bloqueante)
    QCheckBox* has_recess_check_ = nullptr;
    QComboBox* recess_answer_ = nullptr;
    QDoubleSpinBox* recess_depth_ = nullptr;
    QWidget* recess_fields_ = nullptr;
    QLabel* recess_intro_ = nullptr;
    QLabel* recess_count_ = nullptr;
    QLabel* recess_tool_summary_ = nullptr;
    QPushButton* assign_recess_ = nullptr;
    QPushButton* create_recess_ = nullptr;

    // Avançado (retrátil)
    QToolButton* advanced_toggle_ = nullptr;
    QWidget* advanced_fields_ = nullptr;
    QComboBox* material_combo_ = nullptr;
    QComboBox* tools_combo_ = nullptr;
    QLabel* tool_summary_ = nullptr;
    QComboBox* zero_xy_ = nullptr;
    QComboBox* zero_z_ = nullptr;

    // Ações principais
    QPushButton* generate_cut_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* layers_button_ = nullptr;
    QPushButton* toolpaths_button_ = nullptr;
    QLabel* status_ = nullptr;

    int recess_selection_count_ = 0;
    int marked_recess_count_ = 0;
};

}  // namespace cnc::ui
