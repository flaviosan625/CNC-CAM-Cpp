#include "ui/import_setup_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "cad/material.hpp"
#include "cam/presets.hpp"
#include "cam/tools.hpp"
#include "ui/safe_inputs.hpp"

namespace cnc::ui {

namespace {

QLabel* explanation(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setObjectName("MutedText");
    return label;
}

QDoubleSpinBox* millimeters(QWidget* parent, double minimum = 0.001) {
    auto* box = new SafeDoubleSpinBox(parent);
    box->setDecimals(3);
    box->setRange(minimum, 100000.0);
    box->setSuffix(" mm");
    box->setMinimumWidth(0);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return box;
}

QFormLayout* stacked_form(QWidget* parent) {
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

void prepare_combo(QComboBox* combo) {
    combo->setMinimumWidth(0);
    combo->setMinimumContentsLength(6);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

QString normalize(const std::string& text) {
    const QString source = QString::fromStdString(text).normalized(QString::NormalizationForm_KD);
    QString result;
    for (const QChar ch : source) {
        if (ch.category() != QChar::Mark_NonSpacing) result += ch;
    }
    return result.trimmed().toLower();
}

void populate_sheet_presets(QComboBox* combo, const std::string& material_name) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem("Personalizado / atual", -1);
    const QString selected_material = normalize(material_name);
    const auto& presets = cad::standard_sheet_presets();
    for (size_t i = 0; i < presets.size(); ++i) {
        const auto& preset = presets[i];
        if (normalize(preset.name) != selected_material) continue;
        combo->addItem(QString::fromStdString(preset.label), static_cast<int>(i));
    }
}

}  // namespace

ImportSetupPanel::ImportSetupPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("ImportSetupPanel");
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* heading = new QLabel("PREPARAR DESENHO IMPORTADO", this);
    heading->setWordWrap(true);
    heading->setObjectName("ImportSetupHeading");
    heading->setStyleSheet(
        "background: #1f1f1f; color: #ffffff; border-bottom: 2px solid #0078d4; "
        "padding: 8px 10px; font-weight: 600; border-radius: 4px 4px 0 0;");
    root->addWidget(heading);

    // 1. Banner de Geometria Real
    geometry_banner_ = new QLabel(this);
    geometry_banner_->setObjectName("ImportGeometryBanner");
    geometry_banner_->setWordWrap(true);
    geometry_banner_->setStyleSheet(
        "background: #252526; color: #4fc3f7; padding: 7px 10px; border-radius: 4px; "
        "font-weight: 600; border: 1px solid #3c3c3c;");
    geometry_banner_->setVisible(false);
    root->addWidget(geometry_banner_);

    inch_convert_button_ = new QPushButton("⚠ Peça pequena: Converter Polegadas → mm (×25.4)", this);
    inch_convert_button_->setObjectName("ImportConvertInches");
    inch_convert_button_->setStyleSheet("background: #5a3d00; color: #ffeb3b; border: 1px solid #ffb300; padding: 4px;");
    inch_convert_button_->setVisible(false);
    connect(inch_convert_button_, &QPushButton::clicked, this, &ImportSetupPanel::convertInchesRequested);
    root->addWidget(inch_convert_button_);

    // 1.1 Botões de alinhamento rápido no Zero da chapa
    auto* align_row = new QWidget(this);
    auto* align_layout = new QHBoxLayout(align_row);
    align_layout->setContentsMargins(0, 0, 0, 0);
    align_layout->setSpacing(6);

    align_zero_button_ = new QPushButton("📍 Zero da Peça (X10, Y10)", align_row);
    align_zero_button_->setObjectName("ImportAlignZero");
    align_zero_button_->setToolTip("Posiciona a peça no ponto zero da chapa com 10 mm de margem");
    align_zero_button_->setStyleSheet("padding: 4px 8px; font-size: 11px;");
    connect(align_zero_button_, &QPushButton::clicked, this, &ImportSetupPanel::alignToZeroRequested);

    center_material_button_ = new QPushButton("🎯 Centralizar", align_row);
    center_material_button_->setObjectName("ImportCenterMaterial");
    center_material_button_->setToolTip("Centraliza a peça no meio da chapa configurada");
    center_material_button_->setStyleSheet("padding: 4px 8px; font-size: 11px;");
    connect(center_material_button_, &QPushButton::clicked, this, &ImportSetupPanel::centerOnMaterialRequested);

    align_layout->addWidget(align_zero_button_);
    align_layout->addWidget(center_material_button_);
    root->addWidget(align_row);

    // 2. Seção de Presets Rápidos da Oficina (1 Clique)
    auto* preset_header = new QLabel("⚡ PRESET RÁPIDO DO MATERIAL (1 CLIQUE)", this);
    preset_header->setStyleSheet("font-weight: 600; color: #e0e0e0; font-size: 8.5pt; margin-top: 4px;");
    root->addWidget(preset_header);

    auto* chips_grid = new QWidget(this);
    auto* chips_layout = new QGridLayout(chips_grid);
    chips_layout->setContentsMargins(0, 0, 0, 0);
    chips_layout->setSpacing(5);

    struct QuickChip { QString label; QString mat; double thick; };
    const std::vector<QuickChip> quick_chips = {
        {"MDF 15 mm", "MDF", 15.0},
        {"MDF 6 mm", "MDF", 6.0},
        {"Acrílico 4 mm", "Acrílico", 4.0},
        {"ACM 4 mm", "ACM", 4.0},
        {"PVC 10 mm", "PVC", 10.0},
        {"PS 2 mm", "PS", 2.0}
    };

    preset_chips_.clear();
    const auto& house_presets = cam::house_presets();
    for (size_t c = 0; c < quick_chips.size(); ++c) {
        const auto& chip_info = quick_chips[c];
        auto* chip = new QPushButton(chip_info.label, chips_grid);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setMinimumHeight(28);
        chip->setStyleSheet(
            "QPushButton { background: #252526; color: #cccccc; border: 1px solid #3c3c3c; border-radius: 4px; font-weight: 600; font-size: 11px; } "
            "QPushButton:hover { background: #2a2d2e; border-color: #0078d4; color: #ffffff; } "
            "QPushButton[active=\"true\"] { background: #0078d4; border-color: #1a85ff; color: #ffffff; }");

        int target_index = -1;
        for (size_t i = 0; i < house_presets.size(); ++i) {
            if (QString::fromStdString(house_presets[i].material).compare(chip_info.mat, Qt::CaseInsensitive) == 0 &&
                std::fabs(house_presets[i].thickness - chip_info.thick) < 0.05) {
                target_index = static_cast<int>(i);
                break;
            }
        }
        chip->setProperty("presetIndex", target_index);
        connect(chip, &QPushButton::clicked, this, [this, target_index] {
            if (target_index >= 0) {
                select_house_preset(target_index);
                apply_house_preset(target_index);
            }
        });
        chips_layout->addWidget(chip, static_cast<int>(c / 3), static_cast<int>(c % 3));
        preset_chips_.push_back(chip);
    }
    root->addWidget(chips_grid);

    house_preset_combo_ = new SafeComboBox(this);
    house_preset_combo_->setObjectName("ImportPresetCombo");
    prepare_combo(house_preset_combo_);
    house_preset_combo_->addItem("— Selecionar Preset da Casa —", -1);
    for (int i = 0; i < static_cast<int>(house_presets.size()); ++i) {
        const auto& p = house_presets[static_cast<size_t>(i)];
        house_preset_combo_->addItem(
            QString("%1 %2 mm · Ø%3mm (%4 pass)")
                .arg(QString::fromStdString(p.material))
                .arg(p.thickness, 0, 'g', 6)
                .arg(p.diameter, 0, 'g', 6)
                .arg(p.passes()),
            i);
    }
    house_preset_combo_->addItem("Personalizado / Outro", -2);
    root->addWidget(house_preset_combo_);

    // 3. Seção de Tamanho da Chapa
    auto* stock_header = new QLabel("TAMANHO DA CHAPA", this);
    stock_header->setStyleSheet("font-weight: 600; color: #e0e0e0; font-size: 8.5pt; margin-top: 4px;");
    root->addWidget(stock_header);

    stock_mode_combo_ = new SafeComboBox(this);
    stock_mode_combo_->setObjectName("ImportStockMode");
    prepare_combo(stock_mode_combo_);
    stock_mode_combo_->addItem("Ajustar ao desenho (+20 mm folga)", 0);
    stock_mode_combo_->addItem("Chapa comercial inteira", 1);
    stock_mode_combo_->addItem("Medida personalizada (X × Y)", 2);
    root->addWidget(stock_mode_combo_);

    auto* stock_fields = new QWidget(this);
    auto* stock_form = stacked_form(stock_fields);

    thickness_ = millimeters(stock_fields);
    thickness_->setObjectName("ImportThickness");
    thickness_->setDecimals(2);
    thickness_->setMinimumWidth(80);
    stock_form->addRow("Espessura da chapa", thickness_);

    auto* size_row = new QWidget(stock_fields);
    auto* size_layout = new QHBoxLayout(size_row);
    size_layout->setContentsMargins(0, 0, 0, 0);
    size_layout->setSpacing(6);
    width_ = new SafeDoubleSpinBox(size_row);
    width_->setObjectName("ImportStockWidth");
    width_->setRange(1.0, 100000.0);
    width_->setDecimals(1);
    width_->setMinimumWidth(85);
    width_->setPrefix("X: ");
    width_->setSuffix(" mm");
    width_->setToolTip("Largura da chapa, eixo X");

    height_ = new SafeDoubleSpinBox(size_row);
    height_->setObjectName("ImportStockHeight");
    height_->setRange(1.0, 100000.0);
    height_->setDecimals(1);
    height_->setMinimumWidth(85);
    height_->setPrefix("Y: ");
    height_->setSuffix(" mm");
    height_->setToolTip("Comprimento da chapa, eixo Y");
    size_layout->addWidget(width_, 1);
    size_layout->addWidget(height_, 1);
    stock_form->addRow("Dimensões da chapa", size_row);
    root->addWidget(stock_fields);

    // 4. Seção de Rebaixo (Não-bloqueante)
    has_recess_check_ = new QCheckBox("Esta peça possui rebaixo / usinagem rasa", this);
    has_recess_check_->setObjectName("ImportHasRecessCheck");
    has_recess_check_->setStyleSheet("font-weight: 500; color: #cccccc; margin-top: 4px;");
    root->addWidget(has_recess_check_);

    // Dropdown oculto/sincronizado para manter contrato com testes e legados
    recess_answer_ = new SafeComboBox(this);
    recess_answer_->setObjectName("ImportHasRecess");
    prepare_combo(recess_answer_);
    recess_answer_->addItem("Escolher: tem rebaixo?", -1);
    recess_answer_->addItem("Não", 0);
    recess_answer_->addItem("Sim", 1);
    recess_answer_->setVisible(false);
    root->addWidget(recess_answer_);

    recess_fields_ = new QWidget(this);
    auto* recess_fields_layout = new QVBoxLayout(recess_fields_);
    recess_fields_layout->setContentsMargins(0, 0, 0, 0);
    recess_fields_layout->setSpacing(6);

    recess_intro_ = explanation(
        "Selecione os contornos fechados no desenho. Use Shift para selecionar várias áreas. Depois marque a seleção como rebaixo.",
        recess_fields_);
    recess_fields_layout->addWidget(recess_intro_);

    auto* recess_form_widget = new QWidget(recess_fields_);
    auto* recess_form = stacked_form(recess_form_widget);
    recess_depth_ = millimeters(recess_fields_);
    recess_depth_->setObjectName("ImportRecessDepth");
    recess_depth_->setValue(3.0);
    recess_form->addRow("Profundidade do rebaixo", recess_depth_);
    recess_fields_layout->addWidget(recess_form_widget);

    recess_tool_summary_ = explanation({}, recess_fields_);
    recess_fields_layout->addWidget(recess_tool_summary_);
    recess_count_ = explanation({}, recess_fields_);
    recess_count_->setObjectName("ImportRecessCount");
    recess_fields_layout->addWidget(recess_count_);

    assign_recess_ = new QPushButton("Marcar seleção como rebaixo", recess_fields_);
    assign_recess_->setObjectName("ImportAssignRecess");
    assign_recess_->setMinimumHeight(34);
    recess_fields_layout->addWidget(assign_recess_);

    create_recess_ = new QPushButton("Gerar desbaste", recess_fields_);
    create_recess_->setObjectName("ImportCreateRecess");
    create_recess_->setProperty("primary", true);
    create_recess_->setMinimumHeight(34);
    recess_fields_layout->addWidget(create_recess_);
    recess_fields_layout->addWidget(explanation(
        "Sequência do trabalho: rebaixo → corte interno → corte externo.", recess_fields_));
    recess_fields_->setVisible(false);
    root->addWidget(recess_fields_);

    // 5. Configurações Avançadas (Retrátil)
    advanced_toggle_ = new QToolButton(this);
    advanced_toggle_->setObjectName("ImportAdvancedToggle");
    advanced_toggle_->setText("Configurações Avançadas (Fresa e Origens)");
    advanced_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced_toggle_->setArrowType(Qt::RightArrow);
    advanced_toggle_->setCheckable(true);
    advanced_toggle_->setStyleSheet("color: #9cdcfe; margin-top: 4px;");
    root->addWidget(advanced_toggle_);

    advanced_fields_ = new QWidget(this);
    auto* adv_form = stacked_form(advanced_fields_);

    material_combo_ = new SafeComboBox(advanced_fields_);
    material_combo_->setObjectName("ImportMaterial");
    prepare_combo(material_combo_);
    for (const QString& material : {QString("PVC"), QString("MDF"), QString("Acrílico"),
                                    QString("ACM"), QString("PS"), QString("Policarbonato"),
                                    QString("Compensado"), QString("Alumínio")})
        material_combo_->addItem(material, material);
    adv_form->addRow("Material base", material_combo_);

    sheet_preset_combo_ = new SafeComboBox(advanced_fields_);
    sheet_preset_combo_->setObjectName("ImportSheetPreset");
    prepare_combo(sheet_preset_combo_);
    populate_sheet_presets(sheet_preset_combo_, material_combo_->currentData().toString().toStdString());
    adv_form->addRow("Tamanho comercial", sheet_preset_combo_);

    tools_combo_ = new SafeComboBox(advanced_fields_);
    tools_combo_->setObjectName("ImportTool");
    prepare_combo(tools_combo_);
    adv_form->addRow("Fresa de corte", tools_combo_);

    tool_summary_ = explanation({}, advanced_fields_);
    tool_summary_->setObjectName("ImportToolSummary");
    adv_form->addRow(tool_summary_);

    zero_xy_ = new SafeComboBox(advanced_fields_);
    zero_xy_->setObjectName("ImportZeroXY");
    prepare_combo(zero_xy_);
    const std::array<std::pair<QString, QString>, 9> origins = {{
        {"Inferior esquerdo", "left/bottom"}, {"Inferior central", "center/bottom"},
        {"Inferior direito", "right/bottom"}, {"Meio esquerdo", "left/center"},
        {"Centro da chapa", "center/center"}, {"Meio direito", "right/center"},
        {"Superior esquerdo", "left/top"}, {"Superior central", "center/top"},
        {"Superior direito", "right/top"},
    }};
    for (const auto& [label, code] : origins) zero_xy_->addItem(label, code);
    adv_form->addRow("Zero XY", zero_xy_);

    zero_z_ = new SafeComboBox(advanced_fields_);
    zero_z_->setObjectName("ImportZeroZ");
    prepare_combo(zero_z_);
    zero_z_->addItem("Topo da chapa", "top");
    zero_z_->addItem("Base da chapa", "bottom");
    adv_form->addRow("Zero Z", zero_z_);

    advanced_fields_->setVisible(false);
    root->addWidget(advanced_fields_);

    connect(advanced_toggle_, &QToolButton::toggled, this, [this](bool expanded) {
        advanced_fields_->setVisible(expanded);
        advanced_toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });

    // 6. Botões Principais de Ação
    generate_cut_ = new QPushButton("⚡ GERAR CORTE AUTOMÁTICO", this);
    generate_cut_->setObjectName("ImportGenerateCut");
    generate_cut_->setProperty("primary", true);
    generate_cut_->setMinimumHeight(42);
    generate_cut_->setStyleSheet("font-weight: 700; font-size: 10pt;");
    generate_cut_->setToolTip("Aplica a chapa e gera os percursos de corte automaticamente");
    root->addWidget(generate_cut_);

    apply_ = new QPushButton("Aplicar chapa e ferramenta", this);
    apply_->setObjectName("ImportApply");
    apply_->setMinimumHeight(34);
    root->addWidget(apply_);

    auto* aux_row = new QWidget(this);
    auto* aux_layout = new QHBoxLayout(aux_row);
    aux_layout->setContentsMargins(0, 0, 0, 0);
    aux_layout->setSpacing(6);
    layers_button_ = new QPushButton("Camadas", aux_row);
    layers_button_->setObjectName("ImportManageLayers");
    toolpaths_button_ = new QPushButton("Ver percursos", aux_row);
    toolpaths_button_->setObjectName("ImportOpenToolpaths");
    aux_layout->addWidget(layers_button_);
    aux_layout->addWidget(toolpaths_button_);
    root->addWidget(aux_row);

    status_ = explanation("Escolha o material e gere o corte.", this);
    status_->setObjectName("ImportSetupStatus");
    root->addWidget(status_);

    // Conexões de eventos
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (!recess_answered()) set_recess_answer(false);
        emit applyRequested();
    });
    connect(generate_cut_, &QPushButton::clicked, this, [this] {
        if (!recess_answered()) set_recess_answer(false);
        emit generateAutomaticCutRequested();
    });
    connect(assign_recess_, &QPushButton::clicked, this, &ImportSetupPanel::assignRecessRequested);
    connect(create_recess_, &QPushButton::clicked, this, &ImportSetupPanel::createRecessRequested);
    connect(layers_button_, &QPushButton::clicked, this, &ImportSetupPanel::manageLayersRequested);
    connect(toolpaths_button_, &QPushButton::clicked, this, &ImportSetupPanel::openToolpathsRequested);

    connect(has_recess_check_, &QCheckBox::toggled, this, [this](bool checked) {
        const QSignalBlocker blocker(recess_answer_);
        const int target = checked ? 1 : 0;
        const int idx = recess_answer_->findData(target);
        if (idx >= 0) recess_answer_->setCurrentIndex(idx);
        refresh_recess_controls();
    });

    connect(recess_answer_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0) return;
        const int val = recess_answer_->currentData().toInt();
        const QSignalBlocker blocker(has_recess_check_);
        has_recess_check_->setChecked(val == 1);
        refresh_recess_controls();
    });

    connect(house_preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0) return;
        const int preset_idx = house_preset_combo_->currentData().toInt();
        update_preset_chips_selection(preset_idx);
        if (preset_idx >= 0) {
            apply_house_preset(preset_idx);
        }
    });

    connect(stock_mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        update_stock_dimensions();
    });

    connect(tools_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        refresh_tool_summary();
        refresh_recess_controls();
    });

    connect(material_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const std::string mat_name = material_combo_->currentData().toString().toStdString();
        populate_sheet_presets(sheet_preset_combo_, mat_name);
        const auto* preset = cad::find_sheet_preset(mat_name);
        if (preset) {
            const QSignalBlocker bt(thickness_);
            thickness_->setValue(preset->default_thickness);
            if (stock_mode_combo_ && stock_mode_combo_->currentIndex() == 1) {
                const QSignalBlocker bw(width_), bh(height_);
                width_->setValue(preset->width);
                height_->setValue(preset->height);
            }
            if (sheet_preset_combo_ && sheet_preset_combo_->count() > 1) sheet_preset_combo_->setCurrentIndex(1);
            for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
                if (std::fabs(tools_[static_cast<size_t>(i)].diameter - preset->tool_diameter) < 0.1) {
                    tools_combo_->setCurrentIndex(i);
                    break;
                }
            }
            refresh_tool_summary();
            refresh_recess_controls();
        }
    });

    connect(sheet_preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0) return;
        const int preset_index = sheet_preset_combo_->currentData().toInt();
        const auto& presets = cad::standard_sheet_presets();
        if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) return;
        const auto& preset = presets[static_cast<size_t>(preset_index)];
        const QSignalBlocker bw(width_), bh(height_), bt(thickness_);
        width_->setValue(preset.width);
        height_->setValue(preset.height);
        thickness_->setValue(preset.default_thickness);
    });

    const auto set_custom_stock = [this] {
        if (stock_mode_combo_ && stock_mode_combo_->currentIndex() != 2) {
            const QSignalBlocker blocker(stock_mode_combo_);
            stock_mode_combo_->setCurrentIndex(2);
        }
        if (sheet_preset_combo_ && sheet_preset_combo_->currentIndex() != 0) {
            const QSignalBlocker blocker(sheet_preset_combo_);
            sheet_preset_combo_->setCurrentIndex(0);
        }
    };
    connect(width_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, set_custom_stock);
    connect(height_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, set_custom_stock);

    set_material(material_);
    set_tools({});
    set_recess_selection_count(0);
}

void ImportSetupPanel::set_geometry_info(double width, double height, int closed_contours, int total_entities) {
    piece_width_ = width;
    piece_height_ = height;
    closed_contours_ = closed_contours;
    total_entities_ = total_entities;

    if (geometry_banner_) {
        if (width > 0.0 && height > 0.0) {
            geometry_banner_->setText(QString("📐 Peça: %1 × %2 mm · %3 contornos")
                                          .arg(width, 0, 'f', 1)
                                          .arg(height, 0, 'f', 1)
                                          .arg(closed_contours));
            geometry_banner_->setVisible(true);
        } else {
            geometry_banner_->setVisible(false);
        }
    }

    if (inch_convert_button_) {
        const double max_dim = std::max(width, height);
        inch_convert_button_->setVisible(max_dim > 0.01 && max_dim < 25.0);
    }

    update_stock_dimensions();
}

void ImportSetupPanel::update_stock_dimensions() {
    const int mode = stock_mode_combo_ ? stock_mode_combo_->currentIndex() : 0;
    if (mode == 0) {
        // Auto-fit com 20 mm de borda
        if (piece_width_ > 0.0 && piece_height_ > 0.0) {
            const double margin = 20.0;
            const double target_w = std::ceil(piece_width_ + margin * 2.0);
            const double target_h = std::ceil(piece_height_ + margin * 2.0);
            const QSignalBlocker bw(width_), bh(height_);
            width_->setValue(std::max(10.0, target_w));
            height_->setValue(std::max(10.0, target_h));
        }
    } else if (mode == 1) {
        // Comercial
        const std::string mat_name = material_combo_->currentData().toString().toStdString();
        const auto* sheet_preset = cad::find_sheet_preset(mat_name);
        if (sheet_preset) {
            const QSignalBlocker bw(width_), bh(height_);
            width_->setValue(sheet_preset->width);
            height_->setValue(sheet_preset->height);
        }
    }
}

void ImportSetupPanel::set_material(const cad::MaterialSetup& material) {
    material_ = material;
    int material_index = -1;
    const QString material_name = normalize(material.material_profile);
    for (int i = 0; i < material_combo_->count(); ++i) {
        if (normalize(material_combo_->itemData(i).toString().toStdString()) == material_name) {
            material_index = i;
            break;
        }
    }
    if (material_index < 0 && !material.material_profile.empty()) {
        const QString name = QString::fromStdString(material.material_profile);
        material_combo_->addItem(name, name);
        material_index = material_combo_->count() - 1;
    }
    {
        const QSignalBlocker blocker(material_combo_);
        material_combo_->setCurrentIndex(material_index < 0 ? 0 : material_index);
    }
    populate_sheet_presets(sheet_preset_combo_, material_combo_->currentData().toString().toStdString());
    {
        const QSignalBlocker bw(width_), bh(height_), bt(thickness_);
        thickness_->setValue(material.thickness);
        width_->setValue(material.width);
        height_->setValue(material.height);
    }
    for (int i = 1; i < sheet_preset_combo_->count(); ++i) {
        const int preset_index = sheet_preset_combo_->itemData(i).toInt();
        const auto& presets = cad::standard_sheet_presets();
        if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) continue;
        const auto& preset = presets[static_cast<size_t>(preset_index)];
        if (std::fabs(preset.width - material.width) < 0.01 &&
            std::fabs(preset.height - material.height) < 0.01) {
            const QSignalBlocker blocker(sheet_preset_combo_);
            sheet_preset_combo_->setCurrentIndex(i);
            break;
        }
    }
    const QString origin_code = QString("%1/%2")
        .arg(QString::fromStdString(material.zero_x))
        .arg(QString::fromStdString(material.zero_y));
    const int origin_index = zero_xy_->findData(origin_code);
    if (origin_index >= 0) zero_xy_->setCurrentIndex(origin_index);
    const int z_index = zero_z_->findData(QString::fromStdString(material.zero_z));
    if (z_index >= 0) zero_z_->setCurrentIndex(z_index);
}

void ImportSetupPanel::set_tools(const std::vector<cam::Tool>& tools,
                                 std::optional<cam::Tool> preferred) {
    tools_ = tools;
    const QSignalBlocker blocker(tools_combo_);
    tools_combo_->clear();
    int preferred_index = -1;
    for (size_t i = 0; i < tools_.size(); ++i) {
        const auto& tool = tools_[i];
        tools_combo_->addItem(
            QString("T%1 · %2 · Ø %3 mm")
                .arg(tool.tool_number)
                .arg(QString::fromStdString(tool.name))
                .arg(tool.diameter, 0, 'g', 6),
            static_cast<int>(i));
        if (preferred && preferred_index < 0 &&
            ((tool.tool_number == preferred->tool_number && tool.name == preferred->name) ||
             (std::fabs(tool.diameter - preferred->diameter) < 0.01 &&
              tool.name == preferred->name))) {
            preferred_index = static_cast<int>(i);
        }
    }
    if (preferred_index < 0 && preferred) {
        for (size_t i = 0; i < tools_.size(); ++i) {
            if (std::fabs(tools_[i].diameter - preferred->diameter) < 0.01) {
                preferred_index = static_cast<int>(i);
                break;
            }
        }
    }
    if (preferred_index >= 0) tools_combo_->setCurrentIndex(preferred_index);
    else if (!tools_.empty()) tools_combo_->setCurrentIndex(0);
    refresh_tool_summary();
    refresh_recess_controls();
}

void ImportSetupPanel::begin_import(const cad::MaterialSetup& material,
                                   const std::vector<cam::Tool>& tools,
                                   std::optional<cam::Tool> preferred) {
    set_material(material);
    set_tools(tools, preferred);
    refresh_tool_summary();
    refresh_recess_controls();
}

void ImportSetupPanel::reset_recess_answer() {
    {
        const QSignalBlocker blocker(recess_answer_);
        recess_answer_->setCurrentIndex(0);
    }
    {
        const QSignalBlocker blocker(has_recess_check_);
        has_recess_check_->setChecked(false);
    }
    refresh_recess_controls();
}

void ImportSetupPanel::set_recess_answer(std::optional<bool> answer) {
    if (!answer.has_value()) {
        reset_recess_answer();
        return;
    }
    const int target = *answer ? 1 : 0;
    const int idx = recess_answer_->findData(target);
    if (idx >= 0) {
        const QSignalBlocker blocker(recess_answer_);
        recess_answer_->setCurrentIndex(idx);
    }
    {
        const QSignalBlocker blocker(has_recess_check_);
        has_recess_check_->setChecked(*answer);
    }
    refresh_recess_controls();
}

void ImportSetupPanel::set_recess_depth(double depth) {
    const QSignalBlocker blocker(recess_depth_);
    recess_depth_->setValue(depth);
}

int ImportSetupPanel::selected_preset_index() const {
    if (!house_preset_combo_) return -1;
    return house_preset_combo_->currentData().toInt();
}

void ImportSetupPanel::update_preset_chips_selection(int selected_index) {
    for (auto* chip : preset_chips_) {
        if (!chip) continue;
        const int chip_idx = chip->property("presetIndex").toInt();
        const bool active = (chip_idx >= 0 && chip_idx == selected_index);
        chip->setProperty("active", active ? "true" : "false");
        if (chip->style()) {
            chip->style()->unpolish(chip);
            chip->style()->polish(chip);
        }
        chip->update();
    }
}

void ImportSetupPanel::select_house_preset(int preset_index) {
    if (!house_preset_combo_) return;
    const int combo_idx = house_preset_combo_->findData(preset_index);
    if (combo_idx >= 0) {
        house_preset_combo_->setCurrentIndex(combo_idx);
    }
    update_preset_chips_selection(preset_index);
}

void ImportSetupPanel::apply_house_preset(int preset_index) {
    update_preset_chips_selection(preset_index);
    const auto& presets = cam::house_presets();
    if (preset_index < 0 || preset_index >= static_cast<int>(presets.size())) return;
    const auto& preset = presets[static_cast<size_t>(preset_index)];

    // Espessura
    {
        const QSignalBlocker bt(thickness_);
        thickness_->setValue(preset.thickness);
    }

    // Material
    for (int i = 0; i < material_combo_->count(); ++i) {
        if (normalize(material_combo_->itemData(i).toString().toStdString()) == normalize(preset.material)) {
            const QSignalBlocker bm(material_combo_);
            material_combo_->setCurrentIndex(i);
            break;
        }
    }

    // Fresa correspondente
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (std::fabs(tools_[static_cast<size_t>(i)].diameter - preset.diameter) < 0.1) {
            const QSignalBlocker btc(tools_combo_);
            tools_combo_->setCurrentIndex(i);
            break;
        }
    }

    update_stock_dimensions();

    if (!recess_answered()) {
        set_recess_answer(false);
    }

    refresh_tool_summary();
    refresh_recess_controls();

    set_status(QString("Preset %1 selecionado: Fresa Ø%2 mm, %3 passada(s).")
                   .arg(QString::fromStdString(preset.name()))
                   .arg(preset.diameter, 0, 'g', 6)
                   .arg(preset.passes()));
}

ImportSetupValues ImportSetupPanel::values() const {
    ImportSetupValues result;
    result.material = material_;
    result.material.material_profile = material_combo_->currentData().toString().toStdString();
    result.material.width = width_->value();
    result.material.height = height_->value();
    result.material.thickness = thickness_->value();
    const QStringList origin = zero_xy_->currentData().toString().split('/');
    if (origin.size() == 2) {
        result.material.zero_x = origin[0].toStdString();
        result.material.zero_y = origin[1].toStdString();
    }
    result.material.zero_z = zero_z_->currentData().toString().toStdString();
    result.has_recess = recess_answered() ? (recess_answer_->currentData().toInt() == 1) : false;
    result.recess_depth = recess_depth_->value();
    const int tool_index = tools_combo_->currentIndex();
    if (tool_index >= 0 && tool_index < static_cast<int>(tools_.size()))
        result.tool = tools_[static_cast<size_t>(tool_index)];
    return result;
}

bool ImportSetupPanel::recess_answered() const {
    return recess_answer_->currentData().toInt() >= 0;
}

void ImportSetupPanel::set_status(const QString& text) {
    status_->setText(text);
}

void ImportSetupPanel::set_recess_selection_count(int count) {
    recess_selection_count_ = std::max(0, count);
    refresh_recess_controls();
}

void ImportSetupPanel::set_marked_recess_count(int count) {
    marked_recess_count_ = std::max(0, count);
    refresh_recess_controls();
}

void ImportSetupPanel::refresh_recess_controls() {
    const bool answered = recess_answered();
    const bool has_recess = answered && recess_answer_->currentData().toInt() == 1;
    recess_fields_->setVisible(has_recess);
    if (!answered)
        recess_intro_->setText("Escolha um preset ou marque se o trabalho tem rebaixo para continuar.");
    else if (!has_recess)
        recess_intro_->setText("Trabalho sem rebaixo. Clique em Gerar Corte ou posicione o desenho.");
    else
        recess_intro_->setText("Indique as áreas e a profundidade. A ferramenta escolhida será usada no rebaixo.");
    const bool has_tool = !tools_.empty();
    recess_count_->setText(QString("Selecionados: %1 contornos · marcados: %2")
                              .arg(recess_selection_count_).arg(marked_recess_count_));
    apply_->setEnabled(answered && has_tool);
    if (generate_cut_) generate_cut_->setEnabled(has_tool);
    assign_recess_->setEnabled(has_recess && has_tool && recess_selection_count_ > 0);
    create_recess_->setEnabled(has_recess && has_tool && marked_recess_count_ > 0);
}

void ImportSetupPanel::refresh_tool_summary() {
    const int index = tools_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(tools_.size())) {
        tool_summary_->setText("Cadastre uma ferramenta para aplicar os dados do trabalho.");
        recess_tool_summary_->setText("Nenhuma ferramenta selecionada.");
        return;
    }
    const auto& tool = tools_[static_cast<size_t>(index)];
    tool_summary_->setText(QString("%1 rpm · XY %2 mm/min · Z %3 mm/min\nPassada Z %4 mm · lateral %5 mm")
                              .arg(tool.spindle_speed).arg(tool.feed_rate, 0, 'g', 6)
                              .arg(tool.plunge_rate, 0, 'g', 6).arg(tool.pass_depth, 0, 'g', 6)
                              .arg(tool.stepover, 0, 'g', 6));
    recess_tool_summary_->setText(QString("Fresa: T%1 · %2 · Ø %3 mm")
                                     .arg(tool.tool_number).arg(QString::fromStdString(tool.name))
                                     .arg(tool.diameter, 0, 'g', 6));
}

}  // namespace cnc::ui
