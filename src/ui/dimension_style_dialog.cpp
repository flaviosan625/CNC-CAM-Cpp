#include "ui/dimension_style_dialog.hpp"

#include <algorithm>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace cnc::ui {

ColorButton::ColorButton(const std::optional<std::string>& initial_color, QWidget* parent)
    : QPushButton(parent), color_(initial_color ? *initial_color : "#FF007F") {
    setMinimumHeight(28);
    refresh();
    connect(this, &QPushButton::clicked, this, &ColorButton::choose_color);
}

void ColorButton::set_color(const std::string& c) {
    color_ = c.empty() ? "#FF007F" : c;
    refresh();
}

void ColorButton::choose_color() {
    const QColor initial(QString::fromStdString(color_));
    // The button paints itself with the selected color. Do not use it as the
    // dialog parent or its inline stylesheet will bleed into QColorDialog.
    const QColor chosen = QColorDialog::getColor(initial, window(), "Cor da cota");
    if (chosen.isValid()) {
        color_ = chosen.name(QColor::HexRgb).toStdString();
        refresh();
    }
}

void ColorButton::refresh() {
    const QString hex = QString::fromStdString(color_).toUpper();
    setText(hex);
    const QColor qc(hex);
    const QString text_col = (qc.isValid() && qc.lightness() > 130) ? "#181818" : "#ffffff";
    setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #5c718a; "
                          "padding: 5px 10px; font-weight: 600; border-radius: 4px;")
                      .arg(hex, text_col));
}

DimensionStyleDialog::DimensionStyleDialog(const cad::DimensionStyle& style, int selected_count, QWidget* parent)
    : DimensionStyleDialog(style, selected_count, std::nullopt, parent) {}

DimensionStyleDialog::DimensionStyleDialog(const cad::DimensionStyle& style, int selected_count,
                                           std::optional<double> initial_offset, QWidget* parent)
    : ResponsiveDialog(parent) {
    setWindowTitle(selected_count == 1 ? "Editar cota" : "Modificar estilo de cotas");
    setMinimumSize(600, 440);
    resize(700, 500);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("DimensionStyleTabs");
    root->addWidget(tabs_, 1);

    build_lines_tab(style, initial_offset);
    build_arrows_tab(style);
    build_text_tab(style);
    build_fit_tab(style);
    build_primary_units_tab(style);
    build_alternate_units_tab(style);
    build_tolerances_tab(style);

    apply_selected_ = new QCheckBox(
        selected_count == 1
            ? "Aplicar à cota selecionada"
            : QString("Aplicar também às %1 cotas selecionadas").arg(selected_count), this);
    apply_selected_->setEnabled(selected_count > 0);
    apply_selected_->setChecked(selected_count > 0);
    root->addWidget(apply_selected_);

    auto* button_row = new QHBoxLayout();
    button_row->addStretch(1);
    button_row->setSpacing(10);

    auto* cancel_btn = new QPushButton("Cancelar", this);
    cancel_btn->setMinimumWidth(90);
    cancel_btn->setMinimumHeight(32);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);

    auto* ok_btn = new QPushButton(selected_count == 1 ? "Salvar cota" : "Salvar estilo", this);
    ok_btn->setProperty("primary", true);
    ok_btn->setMinimumWidth(120);
    ok_btn->setMinimumHeight(32);
    connect(ok_btn, &QPushButton::clicked, this, &QDialog::accept);

    button_row->addWidget(cancel_btn);
    button_row->addWidget(ok_btn);
    root->addLayout(button_row);
}

QFormLayout* DimensionStyleDialog::add_tab(const QString& title) {
    auto* page = new QWidget(tabs_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(10);
    layout->addLayout(form);
    layout->addStretch(1);
    tabs_->addTab(page, title);
    return form;
}

void DimensionStyleDialog::build_lines_tab(const cad::DimensionStyle& current,
                                          std::optional<double> initial_offset) {
    auto* page = new QWidget(tabs_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    line_color_btn_ = new ColorButton(current.line_color, page);
    extension_color_btn_ = new ColorButton(current.extension_color, page);
    line_by_layer_ = new QCheckBox("Usar cor da camada", page);
    extension_by_layer_ = new QCheckBox("Usar cor da camada", page);
    line_by_layer_->setChecked(!current.line_color.has_value());
    extension_by_layer_->setChecked(!current.extension_color.has_value());
    bind_layer_color(line_color_btn_, line_by_layer_);
    bind_layer_color(extension_color_btn_, extension_by_layer_);

    line_width_ = make_length_control(current.line_width, 0.05, 3.0, 0.05, 2, " mm");
    extension_gap_ = make_length_control(current.extension_gap, 0.0, 30.0, 0.5, 1, " mm");
    extension_overshoot_ = make_length_control(current.extension_overshoot, 0.0, 30.0, 0.5, 1, " mm");

    auto* dimension_box = new QGroupBox("Linhas de cota", page);
    auto* dim_form = new QFormLayout(dimension_box);
    dim_form->setSpacing(8);
    dim_form->addRow("Cor", make_color_control(line_color_btn_, line_by_layer_));
    dim_form->addRow("Espessura", line_width_);
    if (initial_offset.has_value()) {
        offset_control_ = make_length_control(*initial_offset, -1000.0, 1000.0, 1.0, 1, " mm");
        dim_form->addRow("Afastamento da linha (Offset)", offset_control_);
    }
    layout->addWidget(dimension_box);

    auto* extension_box = new QGroupBox("Linhas de extensão", page);
    auto* ext_form = new QFormLayout(extension_box);
    ext_form->setSpacing(8);
    ext_form->addRow("Cor", make_color_control(extension_color_btn_, extension_by_layer_));
    ext_form->addRow("Afastamento da origem", extension_gap_);
    ext_form->addRow("Prolongar além da cota", extension_overshoot_);
    layout->addWidget(extension_box);

    layout->addStretch(1);
    tabs_->addTab(page, "Linhas");
}

void DimensionStyleDialog::build_arrows_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Símbolos e setas");
    arrow_size_ = make_length_control(current.arrow_size, 0.5, 30.0, 0.5, 1, " mm");
    arrow_style_ = new QComboBox();
    arrow_style_->addItem("Fechada", "closed");
    arrow_style_->addItem("Aberta", "open");
    arrow_style_->addItem("Traço arquitetônico", "tick");
    const int idx = arrow_style_->findData(QString::fromStdString(current.arrow_style));
    arrow_style_->setCurrentIndex(std::max(0, idx));
    form->addRow("Tipo de seta", arrow_style_);
    form->addRow("Tamanho", arrow_size_);
}

void DimensionStyleDialog::build_text_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Texto");
    text_color_btn_ = new ColorButton(current.text_color);
    text_by_layer_ = new QCheckBox("Usar cor da camada");
    text_by_layer_->setChecked(!current.text_color.has_value());
    bind_layer_color(text_color_btn_, text_by_layer_);

    text_height_ = make_length_control(current.text_height, 1.0, 30.0, 0.5, 1, " mm");
    text_gap_ = make_length_control(current.text_gap, 0.0, 30.0, 0.5, 1, " mm");

    form->addRow("Cor do texto", make_color_control(text_color_btn_, text_by_layer_));
    form->addRow("Altura do texto", text_height_);
    form->addRow("Afastamento da linha de cota", text_gap_);
}

void DimensionStyleDialog::build_fit_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Ajustar");
    arrow_fit_ = new QComboBox();
    arrow_fit_->addItem("Automático", "auto");
    arrow_fit_->addItem("Setas dentro", "inside");
    arrow_fit_->addItem("Setas fora", "outside");
    const int idx = arrow_fit_->findData(QString::fromStdString(current.arrow_fit));
    arrow_fit_->setCurrentIndex(std::max(0, idx));
    form->addRow("Quando o espaço for curto", arrow_fit_);
    auto* hint = new QLabel("No modo automático, as setas vão para fora quando não couberem junto ao texto.");
    hint->setObjectName("MutedText");
    hint->setWordWrap(true);
    form->addRow("", hint);
}

void DimensionStyleDialog::build_primary_units_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Unidades principais");
    precision_ = new QSpinBox();
    precision_->setRange(0, 6);
    precision_->setValue(current.precision);
    show_unit_ = new QCheckBox("Mostrar unidade no texto da cota");
    show_unit_->setChecked(current.show_unit);
    prefix_ = new QLineEdit(QString::fromStdString(current.prefix));
    suffix_ = new QLineEdit(QString::fromStdString(current.suffix));
    form->addRow("Casas decimais", precision_);
    form->addRow("", show_unit_);
    form->addRow("Prefixo", prefix_);
    form->addRow("Sufixo", suffix_);
}

void DimensionStyleDialog::build_alternate_units_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Unidades alternativas");
    alternate_units_ = new QCheckBox("Exibir conversão para a outra unidade");
    alternate_units_->setChecked(current.alternate_units);
    alternate_precision_ = new QSpinBox();
    alternate_precision_->setRange(0, 6);
    alternate_precision_->setValue(current.alternate_precision);
    form->addRow("", alternate_units_);
    form->addRow("Casas decimais", alternate_precision_);
}

void DimensionStyleDialog::build_tolerances_tab(const cad::DimensionStyle& current) {
    auto* form = add_tab("Tolerâncias");
    tolerance_mode_ = new QComboBox();
    tolerance_mode_->addItem("Nenhuma", "none");
    tolerance_mode_->addItem("Simétrica (±)", "symmetric");
    tolerance_mode_->addItem("Desvios superior/inferior", "deviation");
    const int idx = tolerance_mode_->findData(QString::fromStdString(current.tolerance_mode));
    tolerance_mode_->setCurrentIndex(std::max(0, idx));
    tolerance_upper_ = make_length_control(current.tolerance_upper, -999.0, 999.0, 0.1, 2, " mm");
    tolerance_lower_ = make_length_control(current.tolerance_lower, -999.0, 999.0, 0.1, 2, " mm");
    form->addRow("Método", tolerance_mode_);
    form->addRow("Superior / ±", tolerance_upper_);
    form->addRow("Inferior", tolerance_lower_);
}

void DimensionStyleDialog::bind_layer_color(ColorButton* button, QCheckBox* by_layer) {
    button->setEnabled(!by_layer->isChecked());
    QObject::connect(by_layer, &QCheckBox::toggled, [button](bool use_layer) {
        button->setEnabled(!use_layer);
    });
}

QWidget* DimensionStyleDialog::make_color_control(ColorButton* button, QCheckBox* by_layer) {
    auto* widget = new QWidget();
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(button, 1);
    row->addWidget(by_layer);
    return widget;
}

QDoubleSpinBox* DimensionStyleDialog::make_length_control(
    double value, double min, double max, double step, int decimals, const QString& suffix) {
    auto* control = new QDoubleSpinBox();
    control->setRange(min, max);
    control->setDecimals(decimals);
    control->setSingleStep(step);
    control->setSuffix(suffix);
    control->setValue(value);
    return control;
}

cad::DimensionStyle DimensionStyleDialog::result_style() const {
    cad::DimensionStyle s;
    s.line_color = line_by_layer_->isChecked() ? std::nullopt : std::optional<std::string>(line_color_btn_->color());
    s.extension_color = extension_by_layer_->isChecked() ? std::nullopt : std::optional<std::string>(extension_color_btn_->color());
    s.text_color = text_by_layer_->isChecked() ? std::nullopt : std::optional<std::string>(text_color_btn_->color());
    s.line_width = line_width_->value();
    s.extension_gap = extension_gap_->value();
    s.extension_overshoot = extension_overshoot_->value();
    s.arrow_size = arrow_size_->value();
    s.arrow_style = arrow_style_->currentData().toString().toStdString();
    s.arrow_fit = arrow_fit_->currentData().toString().toStdString();
    s.text_height = text_height_->value();
    s.text_gap = text_gap_->value();
    s.precision = precision_->value();
    s.show_unit = show_unit_->isChecked();
    s.prefix = prefix_ ? prefix_->text().toStdString() : "";
    s.suffix = suffix_ ? suffix_->text().toStdString() : "";
    s.alternate_units = alternate_units_->isChecked();
    s.alternate_precision = alternate_precision_->value();
    s.tolerance_mode = tolerance_mode_->currentData().toString().toStdString();
    s.tolerance_upper = tolerance_upper_->value();
    s.tolerance_lower = tolerance_lower_->value();
    return s;
}

std::optional<double> DimensionStyleDialog::result_offset() const {
    if (offset_control_) return offset_control_->value();
    return std::nullopt;
}

bool DimensionStyleDialog::apply_to_selected() const {
    return apply_selected_ && apply_selected_->isChecked();
}

} // namespace cnc::ui

