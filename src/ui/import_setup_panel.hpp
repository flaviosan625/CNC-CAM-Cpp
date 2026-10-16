#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "cad/material.hpp"
#include "cam/tools.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;

namespace cnc::ui {

struct ImportSetupValues {
    cad::MaterialSetup material;
    cam::Tool tool;
    bool has_recess = false;
    double recess_depth = 3.0;
};

// Preparacao do desenho importado, embutida no painel fixo da direita.
// O widget apresenta os dados; a janela principal aplica as alteracoes ao documento.
class ImportSetupPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ImportSetupPanel(QWidget* parent = nullptr);

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

signals:
    void applyRequested();
    void assignRecessRequested();
    void createRecessRequested();
    void manageLayersRequested();
    void openToolpathsRequested();

private:
    void refresh_recess_controls();
    void refresh_tool_summary();

    cad::MaterialSetup material_;
    std::vector<cam::Tool> tools_;
    QTabWidget* tabs_ = nullptr;
    QComboBox* material_combo_ = nullptr;
    QComboBox* sheet_preset_combo_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QComboBox* zero_xy_ = nullptr;
    QComboBox* zero_z_ = nullptr;
    QComboBox* tools_combo_ = nullptr;
    QComboBox* recess_answer_ = nullptr;
    QDoubleSpinBox* recess_depth_ = nullptr;
    QWidget* recess_fields_ = nullptr;
    QLabel* recess_intro_ = nullptr;
    QLabel* recess_count_ = nullptr;
    QLabel* tool_summary_ = nullptr;
    QLabel* recess_tool_summary_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* assign_recess_ = nullptr;
    QPushButton* create_recess_ = nullptr;
    int recess_selection_count_ = 0;
    int marked_recess_count_ = 0;
};

}  // namespace cnc::ui
