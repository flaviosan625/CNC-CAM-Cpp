#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ui/responsive.hpp"

#include "cad/material.hpp"

class QCheckBox;
class QListWidget;
class QLineEdit;
class QPushButton;
class QString;

namespace cnc::ui {

// Gerenciador de camadas equivalente a src/ui/layer_manager_dialog.py.
class LayerManagerDialog final : public ResponsiveDialog {
public:
    LayerManagerDialog(const std::vector<cad::Layer>& layers,
                       const std::string& current, QWidget* parent = nullptr);

    std::vector<cad::Layer> result_layers();
    std::vector<std::pair<std::string, std::string>> renamed_layers() const;

private:
    cad::Layer* current_layer();
    void load_current();
    void save_current();
    void choose_color();
    void add_layer();
    void remove_layer();
    bool rename_current();
    bool valid_name(const QString& name, int excluded_row = -1);

    std::vector<cad::Layer> layers_;
    QListWidget* layer_list_ = nullptr;
    QLineEdit* current_layer_name_ = nullptr;
    int current_name_row_ = -1;
    QPushButton* rename_button_ = nullptr;
    QPushButton* remove_button_ = nullptr;
    QLineEdit* new_layer_name_ = nullptr;
    QPushButton* color_button_ = nullptr;
    QCheckBox* locked_ = nullptr;
};

}  // namespace cnc::ui
