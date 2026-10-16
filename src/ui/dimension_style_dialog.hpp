#pragma once

#include "cad/entities.hpp"
#include "ui/responsive.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>

namespace cnc::ui {

class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(const std::optional<std::string>& initial_color, QWidget* parent = nullptr);
    std::string color() const { return color_; }
    void set_color(const std::string& c);

private:
    void choose_color();
    void refresh();
    std::string color_;
};

class DimensionStyleDialog : public ResponsiveDialog {
    Q_OBJECT
public:
    explicit DimensionStyleDialog(const cad::DimensionStyle& style, int selected_count = 0, QWidget* parent = nullptr);
    ~DimensionStyleDialog() override = default;

    cad::DimensionStyle result_style() const;
    bool apply_to_selected() const;

private:
    QFormLayout* add_tab(const QString& title);
    void build_lines_tab(const cad::DimensionStyle& current);
    void build_arrows_tab(const cad::DimensionStyle& current);
    void build_text_tab(const cad::DimensionStyle& current);
    void build_fit_tab(const cad::DimensionStyle& current);
    void build_primary_units_tab(const cad::DimensionStyle& current);
    void build_alternate_units_tab(const cad::DimensionStyle& current);
    void build_tolerances_tab(const cad::DimensionStyle& current);

    static void bind_layer_color(ColorButton* button, QCheckBox* by_layer);
    static QWidget* make_color_control(ColorButton* button, QCheckBox* by_layer);
    static QDoubleSpinBox* make_length_control(double value, double min, double max, double step, int decimals, const QString& suffix);

    QTabWidget* tabs_ = nullptr;
    ColorButton* line_color_btn_ = nullptr;
    ColorButton* extension_color_btn_ = nullptr;
    ColorButton* text_color_btn_ = nullptr;
    QCheckBox* line_by_layer_ = nullptr;
    QCheckBox* extension_by_layer_ = nullptr;
    QCheckBox* text_by_layer_ = nullptr;

    QDoubleSpinBox* line_width_ = nullptr;
    QDoubleSpinBox* extension_gap_ = nullptr;
    QDoubleSpinBox* extension_overshoot_ = nullptr;

    QComboBox* arrow_style_ = nullptr;
    QDoubleSpinBox* arrow_size_ = nullptr;

    QDoubleSpinBox* text_height_ = nullptr;
    QDoubleSpinBox* text_gap_ = nullptr;

    QComboBox* arrow_fit_ = nullptr;

    QSpinBox* precision_ = nullptr;
    QCheckBox* show_unit_ = nullptr;

    QCheckBox* alternate_units_ = nullptr;
    QSpinBox* alternate_precision_ = nullptr;

    QComboBox* tolerance_mode_ = nullptr;
    QDoubleSpinBox* tolerance_upper_ = nullptr;
    QDoubleSpinBox* tolerance_lower_ = nullptr;

    QCheckBox* apply_selected_ = nullptr;
};

} // namespace cnc::ui

