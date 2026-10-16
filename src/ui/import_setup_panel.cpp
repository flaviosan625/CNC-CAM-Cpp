#include "ui/import_setup_panel.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/safe_inputs.hpp"

namespace cnc::ui {
namespace {

QLabel* explanation(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
    root->setSpacing(7);
    auto* heading = new QLabel("PREPARAR DESENHO IMPORTADO", this);
    heading->setWordWrap(true);
    heading->setObjectName("ImportSetupHeading");
    heading->setStyleSheet("background: #1f1f1f; color: #ffffff; border-bottom: 2px solid #0078d4; padding: 8px 10px; font-weight: 600; border-radius: 4px 4px 0 0;");
    root->addWidget(heading);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("ImportSetupTabs");
    tabs_->setMinimumWidth(0);
    tabs_->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #2b2b2b; background: #1f1f1f; }"
        "QTabBar::tab { padding: 6px 12px; min-width: 0; background: #181818; border: 1px solid #2b2b2b; color: #cccccc; }"
        "QTabBar::tab:selected { background: #252526; color: #ffffff; border-top: 2px solid #0078d4; border-bottom: 1px solid #252526; }");
    root->addWidget(tabs_);

    auto* work_page = new QWidget(tabs_);
    auto* work_layout = new QVBoxLayout(work_page);
    work_layout->setContentsMargins(8, 8, 8, 8);
    work_layout->setSpacing(7);
    auto* work_form_widget = new QWidget(work_page);
    auto* work_form = stacked_form(work_form_widget);

    material_combo_ = new SafeComboBox(work_page);
    material_combo_->setObjectName("ImportMaterial");
    prepare_combo(material_combo_);
    for (const QString& material : {QString("PVC"), QString("MDF"), QString("Acrílico"),
                                    QString("ACM"), QString("PS"), QString("Policarbonato"),
                                    QString("Compensado"), QString("Alumínio")})
        material_combo_->addItem(material == "PVC" ? QStringLiteral("PVC Expandido") : material, material);
    work_form->addRow("O que vai cortar?", material_combo_);

    thickness_ = millimeters(work_page);
    thickness_->setObjectName("ImportThickness");
    thickness_->setDecimals(2);
    thickness_->setMinimumWidth(80);
    work_form->addRow("Espessura da chapa", thickness_);

    sheet_preset_combo_ = new SafeComboBox(work_page);
    sheet_preset_combo_->setObjectName("ImportSheetPreset");
    prepare_combo(sheet_preset_combo_);
    populate_sheet_presets(sheet_preset_combo_, material_combo_->currentData().toString().toStdString());
    work_form->addRow("Tamanho comercial", sheet_preset_combo_);

    auto* size_row = new QWidget(work_page);
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
    work_form->addRow("Chapa X × Y (mm)", size_row);

    tools_combo_ = new SafeComboBox(work_page);
    tools_combo_->setObjectName("ImportTool");
    prepare_combo(tools_combo_);
    work_form->addRow("Fresa", tools_combo_);
    work_layout->addWidget(work_form_widget);

    tool_summary_ = explanation({}, work_page);
    tool_summary_->setObjectName("ImportToolSummary");
    work_layout->addWidget(tool_summary_);

    auto* answer_form_widget = new QWidget(work_page);
    auto* answer_form = stacked_form(answer_form_widget);
    recess_answer_ = new SafeComboBox(work_page);
    recess_answer_->setObjectName("ImportHasRecess");
    prepare_combo(recess_answer_);
    recess_answer_->addItem("Escolher: tem rebaixo?", -1);
    recess_answer_->addItem("Não", 0);
    recess_answer_->addItem("Sim", 1);
    answer_form->addRow("Tem rebaixo?", recess_answer_);
    work_layout->addWidget(answer_form_widget);

    auto* origin_toggle = new QToolButton(work_page);
    origin_toggle->setObjectName("ImportOriginToggle");
    origin_toggle->setText("Conferir origem XY e Z");
    origin_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    origin_toggle->setArrowType(Qt::RightArrow);
    origin_toggle->setCheckable(true);
    work_layout->addWidget(origin_toggle);
    auto* origin_fields = new QWidget(work_page);
    auto* origin_form = stacked_form(origin_fields);
    zero_xy_ = new SafeComboBox(origin_fields);
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
    origin_form->addRow("Zero XY", zero_xy_);
    zero_z_ = new SafeComboBox(origin_fields);
    zero_z_->setObjectName("ImportZeroZ");
    prepare_combo(zero_z_);
    zero_z_->addItem("Topo da chapa", "top");
    zero_z_->addItem("Base da chapa", "bottom");
    origin_form->addRow("Zero Z", zero_z_);
    origin_fields->setVisible(false);
    work_layout->addWidget(origin_fields);
    connect(origin_toggle, &QToolButton::toggled, this, [origin_toggle, origin_fields](bool expanded) {
        origin_fields->setVisible(expanded);
        origin_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });

    apply_ = new QPushButton("Aplicar chapa e ferramenta", work_page);
    apply_->setObjectName("ImportApply");
    apply_->setProperty("primary", true);
    apply_->setMinimumHeight(36);
    work_layout->addWidget(apply_);
    work_layout->addWidget(explanation(
        "O material orienta os parâmetros sugeridos. Todos os recursos e percursos continuam disponíveis.", work_page));
    tabs_->addTab(work_page, "Trabalho");

    auto* recess_page = new QWidget(tabs_);
    auto* recess_layout = new QVBoxLayout(recess_page);
    recess_layout->setContentsMargins(8, 8, 8, 8);
    recess_layout->setSpacing(8);
    recess_intro_ = explanation({}, recess_page);
    recess_layout->addWidget(recess_intro_);
    recess_fields_ = new QWidget(recess_page);
    auto* recess_fields_layout = new QVBoxLayout(recess_fields_);
    recess_fields_layout->setContentsMargins(0, 0, 0, 0);
    recess_fields_layout->setSpacing(8);
    recess_fields_layout->addWidget(explanation(
        "Selecione os contornos fechados no desenho. Use Shift para selecionar várias áreas. Depois marque a seleção como rebaixo.", recess_fields_));
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
    assign_recess_->setMinimumHeight(36);
    recess_fields_layout->addWidget(assign_recess_);
    create_recess_ = new QPushButton("Gerar rebaixo", recess_fields_);
    create_recess_->setObjectName("ImportCreateRecess");
    create_recess_->setProperty("primary", true);
    create_recess_->setMinimumHeight(36);
    recess_fields_layout->addWidget(create_recess_);
    recess_fields_layout->addWidget(explanation(
        "Sequência do trabalho: rebaixo → corte interno → corte externo.", recess_fields_));
    recess_layout->addWidget(recess_fields_);
    recess_layout->addStretch();
    tabs_->addTab(recess_page, "Rebaixos");

    status_ = explanation("Importe o desenho e confira os dados do trabalho.", this);
    status_->setObjectName("ImportSetupStatus");
    root->addWidget(status_);
    auto* layers = new QPushButton("Camadas / renomear", this);
    layers->setObjectName("ImportManageLayers");
    root->addWidget(layers);
    auto* toolpaths = new QPushButton("Escolher percurso", this);
    toolpaths->setObjectName("ImportOpenToolpaths");
    root->addWidget(toolpaths);

    connect(apply_, &QPushButton::clicked, this, &ImportSetupPanel::applyRequested);
    connect(assign_recess_, &QPushButton::clicked, this, &ImportSetupPanel::assignRecessRequested);
    connect(create_recess_, &QPushButton::clicked, this, &ImportSetupPanel::createRecessRequested);
    connect(layers, &QPushButton::clicked, this, &ImportSetupPanel::manageLayersRequested);
    connect(toolpaths, &QPushButton::clicked, this, &ImportSetupPanel::openToolpathsRequested);
    connect(recess_answer_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        refresh_recess_controls();
        if (recess_answer_->currentData().toInt() == 1) tabs_->setCurrentIndex(1);
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
            const QSignalBlocker bw(width_), bh(height_), bt(thickness_);
            width_->setValue(preset->width);
            height_->setValue(preset->height);
            thickness_->setValue(preset->default_thickness);
            if (sheet_preset_combo_ && sheet_preset_combo_->count() > 1) sheet_preset_combo_->setCurrentIndex(1);
            for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
                if (std::fabs(tools_[static_cast<size_t>(i)].diameter - preset->tool_diameter) < 0.1) {
                    tools_combo_->setCurrentIndex(i);
                    break;
                }
            }
            refresh_tool_summary();
            refresh_recess_controls();
            set_status(QString("Material %1: medidas padrão %2 × %3 mm, espessura %4 mm reconhecidas.")
                .arg(QString::fromStdString(preset->name))
                .arg(preset->width, 0, 'f', 0)
                .arg(preset->height, 0, 'f', 0)
                .arg(preset->default_thickness, 0, 'f', 1));
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

    const auto set_custom_preset = [this] {
        if (sheet_preset_combo_ && sheet_preset_combo_->currentIndex() != 0) {
            const QSignalBlocker blocker(sheet_preset_combo_);
            sheet_preset_combo_->setCurrentIndex(0);
        }
    };
    connect(width_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, set_custom_preset);
    connect(height_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, set_custom_preset);

    set_material(material_);
    set_tools({});
    set_recess_selection_count(0);
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
    const QString origin = QString::fromStdString(material.zero_x + "/" + material.zero_y);
    const int origin_index = zero_xy_->findData(origin);
    if (origin_index >= 0) zero_xy_->setCurrentIndex(origin_index);
    const int z_index = zero_z_->findData(QString::fromStdString(material.zero_z));
    if (z_index >= 0) zero_z_->setCurrentIndex(z_index);
}

void ImportSetupPanel::set_tools(const std::vector<cam::Tool>& tools,
                                 std::optional<cam::Tool> preferred) {
    if (!preferred && tools_combo_->currentIndex() >= 0 &&
        tools_combo_->currentIndex() < static_cast<int>(tools_.size()))
        preferred = tools_[static_cast<size_t>(tools_combo_->currentIndex())];
    const QSignalBlocker blocker(tools_combo_);
    tools_ = tools;
    if (preferred && std::find(tools_.begin(), tools_.end(), *preferred) == tools_.end())
        tools_.push_back(*preferred);
    tools_combo_->clear();
    int preferred_index = -1;
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        const auto& tool = tools_[static_cast<size_t>(i)];
        const QString label = QString("T%1 · %2 · Ø %3 mm")
                                  .arg(tool.tool_number).arg(QString::fromStdString(tool.name))
                                  .arg(tool.diameter, 0, 'g', 6);
        tools_combo_->addItem(label);
        tools_combo_->setItemData(i, label, Qt::ToolTipRole);
        if (preferred && tool == *preferred) preferred_index = i;
    }
    if (preferred_index >= 0) tools_combo_->setCurrentIndex(preferred_index);
    if (tools_.empty()) tools_combo_->addItem("Nenhuma fresa cadastrada");
    tools_combo_->setEnabled(!tools_.empty());
    refresh_tool_summary();
    refresh_recess_controls();
}

void ImportSetupPanel::begin_import(const cad::MaterialSetup& material,
                                    const std::vector<cam::Tool>& tools,
                                    std::optional<cam::Tool> preferred) {
    set_material(material);
    set_tools(tools, std::move(preferred));
    reset_recess_answer();
    set_recess_selection_count(0);
    set_marked_recess_count(0);
    tabs_->setCurrentIndex(0);
}

void ImportSetupPanel::reset_recess_answer() {
    set_recess_answer(std::nullopt);
}

void ImportSetupPanel::set_recess_answer(std::optional<bool> answer) {
    const int answer_code = answer ? (*answer ? 1 : 0) : -1;
    recess_answer_->setCurrentIndex(recess_answer_->findData(answer_code));
    refresh_recess_controls();
}

void ImportSetupPanel::set_recess_depth(double depth) {
    recess_depth_->setValue(depth);
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
    result.has_recess = recess_answer_->currentData().toInt() == 1;
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
        recess_intro_->setText("Na aba Trabalho, responda se o desenho tem rebaixo para continuar.");
    else if (!has_recess)
        recess_intro_->setText("Trabalho sem rebaixo. Posicione o desenho e escolha o percurso desejado.");
    else
        recess_intro_->setText("Indique as áreas e a profundidade. A ferramenta escolhida na aba Trabalho será usada no rebaixo.");
    const bool has_tool = !tools_.empty();
    recess_count_->setText(QString("Selecionados: %1 contornos · marcados: %2")
                              .arg(recess_selection_count_).arg(marked_recess_count_));
    apply_->setEnabled(answered && has_tool);
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
    recess_tool_summary_->setText(QString("Fresa: T%1 · %2 · Ø %3 mm\nPara trocar a fresa, abra a aba Trabalho.")
                                     .arg(tool.tool_number).arg(QString::fromStdString(tool.name))
                                     .arg(tool.diameter, 0, 'g', 6));
}

}  // namespace cnc::ui
