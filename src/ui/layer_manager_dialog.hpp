#pragma once

#include <string>
#include <vector>

#include "ui/responsive.hpp"

#include "cad/material.hpp"

class QCheckBox;
class QComboBox;
class QPushButton;

namespace cnc::ui {

// Gerenciador de camadas equivalente a src/ui/layer_manager_dialog.py.
class LayerManagerDialog final : public ResponsiveDialog {
public:
    LayerManagerDialog(const std::vector<cad::Layer>& layers,
                       const std::string& current, QWidget* parent = nullptr);

    std::vector<cad::Layer> result_layers();

private:
    cad::Layer* current_layer();
    void load_current();
    void save_current();
    void choose_color();
    void add_layer();
    void remove_layer();

    std::vector<cad::Layer> layers_;
    QComboBox* layer_combo_ = nullptr;
    QPushButton* color_button_ = nullptr;
    QCheckBox* visible_ = nullptr;
    QCheckBox* locked_ = nullptr;
};

}  // namespace cnc::ui
