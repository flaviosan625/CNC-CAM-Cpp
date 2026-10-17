#include "ui/modeling_3d.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "viewport/gl_widget.hpp"

namespace cnc::ui {
using namespace cad::model3d;
using namespace cad::relief;

Modeling3DWorkspace::Modeling3DWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("Modelagem3DWorkspace");
    setup_ui();
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::setup_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    setup_top_bar();
    root->addLayout(findChild<QHBoxLayout*>("TopBarLayout"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("Modelagem3DSplitter");

    auto* left = create_left_panel();
    auto* center = create_center_panel();
    auto* right = create_right_panel();

    splitter->addWidget(left);
    splitter->addWidget(center);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0); // Painel esquerdo fixo
    splitter->setStretchFactor(1, 1); // Viewport 3D expande
    splitter->setStretchFactor(2, 0); // Árvore direita fixa
    splitter->setSizes({340, 720, 280});

    root->addWidget(splitter, 1);
    root->addWidget(create_bottom_sculpt_bar());
}

void Modeling3DWorkspace::setup_top_bar() {
    auto* top = new QHBoxLayout;
    top->setObjectName("TopBarLayout");

    auto* title = new QLabel("MODELAGEM 3D · PEÇA PARAMÉTRICA & RELEVO", this);
    title->setObjectName("SectionHeader");
    top->addWidget(title, 1);

    for (const auto& [label, callback] : std::vector<std::pair<QString, std::function<void()>>>{
             {"Nova peça", [this]{ new_part(); }},
             {"Abrir peça", [this]{ open_part(); }},
             {"Salvar", [this]{ save_part(); }},
             {"Salvar como", [this]{ save_part_as(); }}}) {
        auto* b = new QPushButton(label, this);
        top->addWidget(b);
        connect(b, &QPushButton::clicked, this, callback);
    }
}

QWidget* Modeling3DWorkspace::create_left_panel() {
    auto* container = new QWidget(this);
    container->setMinimumWidth(320);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(6);

    left_tabs_ = new QTabWidget(container);
    left_tabs_->addTab(create_relief_page(), "Imagem e Relevo");
    left_tabs_->addTab(create_cad_page(), "Esboço CAD");
    layout->addWidget(left_tabs_);

    return container;
}

QWidget* Modeling3DWorkspace::create_cad_page() {
    auto* page = new QWidget(left_tabs_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(6);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    profile_ = new QComboBox(page);
    profile_->addItem("Retângulo", int(Profile::Rectangle));
    profile_->addItem("Círculo", int(Profile::Circle));
    profile_->addItem("Triângulo", int(Profile::Triangle));
    profile_->addItem("Hexágono", int(Profile::Hexagon));
    form->addRow("Perfil do esboço", profile_);

    auto make_num = [page](double lo, double hi, double value, int decimals = 2) {
        auto* s = new QDoubleSpinBox(page);
        s->setRange(lo, hi);
        s->setDecimals(decimals);
        s->setValue(value);
        s->setSuffix(" mm");
        return s;
    };

    width_ = make_num(.1, 100000, 100);
    height_ = make_num(.1, 100000, 60);
    start_z_ = make_num(-100000, 100000, 0);
    depth_ = make_num(.1, 100000, 18);
    depth2_ = make_num(0, 100000, 18);
    target_z_ = make_num(-100000, 100000, 18);
    offset_ = make_num(-100000, 100000, 0);
    draft_ = make_num(-89, 89, 0, 2);
    draft_->setSuffix(" °");

    form->addRow("Largura / diâmetro", width_);
    form->addRow("Altura", height_);
    form->addRow("Plano do esboço Z", start_z_);

    end_ = new QComboBox(page);
    end_->addItem("Cego", int(EndCondition::Blind));
    end_->addItem("Atravessar tudo", int(EndCondition::ThroughAll));
    end_->addItem("Atravessar tudo · ambos os lados", int(EndCondition::ThroughAllBoth));
    end_->addItem("Plano médio", int(EndCondition::MidPlane));
    end_->addItem("Até próximo", int(EndCondition::UpToNext));
    end_->addItem("Até vértice", int(EndCondition::UpToVertex));
    end_->addItem("Até superfície", int(EndCondition::UpToSurface));
    end_->addItem("Offset da superfície", int(EndCondition::OffsetFromSurface));
    end_->addItem("Até corpo", int(EndCondition::UpToBody));

    form->addRow("Condição final", end_);
    form->addRow("Profundidade 1", depth_);
    form->addRow("Profundidade 2", depth2_);
    form->addRow("Referência Z", target_z_);
    form->addRow("Offset da referência", offset_);
    form->addRow("Ângulo de saída", draft_);
    layout->addLayout(form);

    reverse_ = new QCheckBox("Inverter direção", page);
    two_directions_ = new QCheckBox("Extrudar na direção 2", page);
    suppressed_ = new QCheckBox("Suprimir este recurso", page);
    layout->addWidget(reverse_);
    layout->addWidget(two_directions_);
    layout->addWidget(suppressed_);

    auto* actions = new QHBoxLayout;
    auto* add = new QPushButton("+ Criar extrusão", page);
    add->setProperty("primary", true);
    auto* apply = new QPushButton("Aplicar edição", page);
    actions->addWidget(add);
    actions->addWidget(apply);
    layout->addLayout(actions);

    connect(add, &QPushButton::clicked, this, &Modeling3DWorkspace::add_extrusion);
    connect(apply, &QPushButton::clicked, this, &Modeling3DWorkspace::apply_extrusion);

    connect(end_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const auto e = static_cast<EndCondition>(end_->currentData().toInt());
        target_z_->setEnabled(e == EndCondition::UpToVertex || e == EndCondition::UpToSurface ||
                              e == EndCondition::OffsetFromSurface || e == EndCondition::UpToBody);
        offset_->setEnabled(e == EndCondition::OffsetFromSurface);
        depth_->setEnabled(e == EndCondition::Blind || e == EndCondition::MidPlane ||
                           e == EndCondition::ThroughAll || e == EndCondition::ThroughAllBoth ||
                           e == EndCondition::UpToNext);
    });

    layout->addStretch(1);
    return page;
}

QWidget* Modeling3DWorkspace::create_relief_page() {
    auto* page = new QWidget(left_tabs_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(8);

    // 1. Imagem
    auto* grp1 = new QGroupBox("1. Imagem e Entrada", page);
    auto* l1 = new QVBoxLayout(grp1);
    l1->setSpacing(6);

    auto* btn_row1 = new QHBoxLayout;
    auto* open_img_btn = new QPushButton("Abrir imagem...", grp1);
    open_img_btn->setProperty("primary", true);
    auto* crop_btn = new QPushButton("Recortar área", grp1);
    btn_row1->addWidget(open_img_btn);
    btn_row1->addWidget(crop_btn);
    l1->addLayout(btn_row1);

    connect(open_img_btn, &QPushButton::clicked, this, &Modeling3DWorkspace::open_relief_image);
    connect(crop_btn, &QPushButton::clicked, this, [this]{
        QMessageBox::information(this, "Recorte de Relevo",
            "Para restringir o relevo a uma forma exata, desenhe um contorno fechado na aba Desenho 2D ou use as dimensões em milímetros abaixo.");
    });

    img_status_label_ = new QLabel("Nenhuma imagem carregada", grp1);
    img_status_label_->setObjectName("MutedText");
    img_status_label_->setWordWrap(true);
    l1->addWidget(img_status_label_);

    auto* contrast_row = new QHBoxLayout;
    contrast_row->addWidget(new QLabel("Contraste:", grp1));
    contrast_slider_ = new QSlider(Qt::Horizontal, grp1);
    contrast_slider_->setRange(20, 300);
    contrast_slider_->setValue(100);
    contrast_spin_ = new QDoubleSpinBox(grp1);
    contrast_spin_->setRange(0.2, 3.0);
    contrast_spin_->setSingleStep(0.1);
    contrast_spin_->setValue(1.0);
    contrast_row->addWidget(contrast_slider_, 1);
    contrast_row->addWidget(contrast_spin_);
    l1->addLayout(contrast_row);

    connect(contrast_slider_, &QSlider::valueChanged, this, [this](int val){
        contrast_spin_->setValue(val / 100.0);
    });
    connect(contrast_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double val){
        const int sval = std::clamp(static_cast<int>(val * 100.0), 20, 300);
        if (contrast_slider_->value() != sval) contrast_slider_->setValue(sval);
    });

    invert_check_ = new QCheckBox("Inverter relevo (tons escuros viram picos)", grp1);
    l1->addWidget(invert_check_);
    layout->addWidget(grp1);

    // 2. Resultado
    auto* grp2 = new QGroupBox("2. Resultado Geométrico", page);
    auto* l2 = new QVBoxLayout(grp2);
    l2->setSpacing(6);

    auto* nature_row = new QHBoxLayout;
    nature_raised_ = new QRadioButton("Relevo elevado", grp2);
    nature_carved_ = new QRadioButton("Entalhe na peça", grp2);
    nature_raised_->setChecked(true);
    nature_row->addWidget(nature_raised_);
    nature_row->addWidget(nature_carved_);
    l2->addLayout(nature_row);

    auto* dim_form = new QFormLayout;
    dim_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto make_sbox = [page](double lo, double hi, double val, const QString& suffix) {
        auto* s = new QDoubleSpinBox(page);
        s->setRange(lo, hi);
        s->setValue(val);
        s->setSuffix(suffix);
        return s;
    };

    shape_height_ = make_sbox(0.1, 200.0, 3.0, " mm");
    base_height_  = make_sbox(-100.0, 100.0, 0.0, " mm");
    relief_w_     = make_sbox(1.0, 10000.0, 100.0, " mm");
    relief_h_     = make_sbox(1.0, 10000.0, 100.0, " mm");
    relief_x_     = make_sbox(-5000.0, 5000.0, 0.0, " mm");
    relief_y_     = make_sbox(-5000.0, 5000.0, 0.0, " mm");

    dim_form->addRow("Altura da forma", shape_height_);
    dim_form->addRow("Altura da base", base_height_);
    dim_form->addRow("Largura X", relief_w_);
    dim_form->addRow("Altura Y", relief_h_);
    dim_form->addRow("Posição X", relief_x_);
    dim_form->addRow("Posição Y", relief_y_);
    l2->addLayout(dim_form);

    auto* smooth_row = new QHBoxLayout;
    smooth_row->addWidget(new QLabel("Suavidade:", grp2));
    smoothness_slider_ = new QSlider(Qt::Horizontal, grp2);
    smoothness_slider_->setRange(0, 100);
    smoothness_slider_->setValue(0);
    smoothness_label_ = new QLabel("0 %", grp2);
    smoothness_label_->setMinimumWidth(36);
    smooth_row->addWidget(smoothness_slider_, 1);
    smooth_row->addWidget(smoothness_label_);
    l2->addLayout(smooth_row);

    connect(smoothness_slider_, &QSlider::valueChanged, this, [this](int val){
        smoothness_label_->setText(QString("%1 %").arg(val));
    });
    layout->addWidget(grp2);

    // 3. Combinar com peça
    auto* grp3 = new QGroupBox("3. Combinar com a Peça", page);
    auto* l3 = new QVBoxLayout(grp3);
    l3->setSpacing(6);

    auto* mode_grid = new QGridLayout;
    const std::vector<std::pair<QString, CombineMode>> modes = {
        {"Adicionar (+)", CombineMode::Add},
        {"Subtrair (-)", CombineMode::Subtract},
        {"Mesclar Alto (M)", CombineMode::MergeHigh},
        {"Mesclar Baixo", CombineMode::MergeLow},
        {"Multiplicar", CombineMode::Multiply}
    };

    combine_mode_buttons_.clear();
    for (size_t i = 0; i < modes.size(); ++i) {
        auto* btn = new QPushButton(modes[i].first, grp3);
        combine_mode_buttons_.push_back(btn);
        mode_grid->addWidget(btn, static_cast<int>(i / 2), static_cast<int>(i % 2));
        const auto mode = modes[i].second;
        connect(btn, &QPushButton::clicked, this, [this, mode]{ set_combine_mode(mode); });
    }
    set_combine_mode(CombineMode::Add);
    l3->addLayout(mode_grid);

    auto* action_btns = new QHBoxLayout;
    auto* add_comp_btn = new QPushButton("+ Criar Relevo", grp3);
    add_comp_btn->setProperty("primary", true);
    auto* apply_comp_btn = new QPushButton("Aplicar Edição", grp3);
    action_btns->addWidget(add_comp_btn);
    action_btns->addWidget(apply_comp_btn);
    l3->addLayout(action_btns);

    connect(add_comp_btn, &QPushButton::clicked, this, &Modeling3DWorkspace::add_relief_component);
    connect(apply_comp_btn, &QPushButton::clicked, this, &Modeling3DWorkspace::apply_relief_component);

    layout->addWidget(grp3);
    layout->addStretch(1);
    return page;
}

void Modeling3DWorkspace::set_combine_mode(CombineMode mode) {
    current_combine_mode_ = mode;
    const std::vector<CombineMode> mode_list = {
        CombineMode::Add, CombineMode::Subtract, CombineMode::MergeHigh, CombineMode::MergeLow, CombineMode::Multiply
    };
    for (size_t i = 0; i < combine_mode_buttons_.size() && i < mode_list.size(); ++i) {
        const bool active = (mode_list[i] == mode);
        combine_mode_buttons_[i]->setProperty("primary", active);
        combine_mode_buttons_[i]->style()->unpolish(combine_mode_buttons_[i]);
        combine_mode_buttons_[i]->style()->polish(combine_mode_buttons_[i]);
    }
}

QWidget* Modeling3DWorkspace::create_center_panel() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);

    // Barra de ferramentas superior do Viewport (HUD)
    auto* hud = new QHBoxLayout;
    hud->setSpacing(8);

    auto* hud_title = new QLabel("PRÉVIA 3D · ILUMINAÇÃO & MALHA", container);
    hud_title->setObjectName("SectionHeader");
    hud->addWidget(hud_title);
    hud->addStretch(1);

    for (const auto& [name, code] : std::vector<std::pair<QString, QString>>{
             {"Iso", "iso"}, {"Top", "top"}, {"Frontal", "front"}, {"Lateral", "right"}}) {
        auto* vb = new QPushButton(name, container);
        vb->setMaximumWidth(60);
        hud->addWidget(vb);
        connect(vb, &QPushButton::clicked, this, [this, code]{
            if (viewport_) viewport_->set_standard_view(code);
        });
    }

    hud->addWidget(new QLabel("Resolução:", container));
    resolution_combo_ = new QComboBox(container);
    resolution_combo_->addItem("0.25 mm (Fino)", 0.25);
    resolution_combo_->addItem("0.50 mm (Normal)", 0.50);
    resolution_combo_->addItem("1.00 mm (Rápido)", 1.00);
    resolution_combo_->setCurrentIndex(1); // 0.5 mm
    hud->addWidget(resolution_combo_);

    connect(resolution_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]{
        preview_resolution_mm_ = resolution_combo_->currentData().toDouble();
        rebuild_view();
    });

    hud->addWidget(new QLabel("Luz:", container));
    light_slider_ = new QSlider(Qt::Horizontal, container);
    light_slider_->setRange(0, 360);
    light_slider_->setValue(45);
    light_slider_->setMaximumWidth(90);
    hud->addWidget(light_slider_);

    connect(light_slider_, &QSlider::valueChanged, this, [this](int val){
        light_angle_deg_ = static_cast<double>(val);
        rebuild_view();
    });

    layout->addLayout(hud);

    viewport_ = new vp::Viewport3D(container);
    viewport_->setMinimumSize(480, 360);
    layout->addWidget(viewport_, 1);

    return container;
}

QWidget* Modeling3DWorkspace::create_right_panel() {
    auto* container = new QWidget(this);
    container->setMinimumWidth(260);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 0, 0, 0);
    layout->setSpacing(6);

    auto* header = new QLabel("COMPONENTES DA PEÇA", container);
    header->setObjectName("SectionHeader");
    layout->addWidget(header);

    tree_ = new QTreeWidget(container);
    tree_->setHeaderHidden(true);
    tree_->setObjectName("Model3DFeatureTree");
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        select_tree_item(current);
    });

    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(4);

    auto* btn_vis = new QPushButton("👁 Visível", container);
    auto* btn_lock = new QPushButton("🔒 Travar", container);
    auto* btn_up = new QPushButton("▲", container);
    btn_up->setMaximumWidth(28);
    auto* btn_down = new QPushButton("▼", container);
    btn_down->setMaximumWidth(28);
    auto* btn_del = new QPushButton("🗑", container);
    btn_del->setMaximumWidth(28);

    btn_row->addWidget(btn_vis);
    btn_row->addWidget(btn_lock);
    btn_row->addWidget(btn_up);
    btn_row->addWidget(btn_down);
    btn_row->addWidget(btn_del);
    layout->addLayout(btn_row);

    connect(btn_vis, &QPushButton::clicked, this, &Modeling3DWorkspace::toggle_selected_visibility);
    connect(btn_lock, &QPushButton::clicked, this, &Modeling3DWorkspace::toggle_selected_lock);
    connect(btn_up, &QPushButton::clicked, this, &Modeling3DWorkspace::move_selected_up);
    connect(btn_down, &QPushButton::clicked, this, &Modeling3DWorkspace::move_selected_down);
    connect(btn_del, &QPushButton::clicked, this, &Modeling3DWorkspace::delete_selected);

    return container;
}

QWidget* Modeling3DWorkspace::create_bottom_sculpt_bar() {
    auto* bar = new QFrame(this);
    bar->setObjectName("BottomSculptBar");
    bar->setStyleSheet("QFrame#BottomSculptBar { background-color: #1e1e1e; border-top: 1px solid #2b2b2b; padding: 4px; }");

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    auto* title = new QLabel("ESCULPIR:", bar);
    title->setObjectName("SectionHeader");
    layout->addWidget(title);

    sculpt_smooth_btn_ = new QPushButton("Suavizar", bar);
    sculpt_smudge_btn_ = new QPushButton("Esfumar", bar);
    sculpt_elevate_btn_ = new QPushButton("Elevar", bar);
    sculpt_remove_btn_  = new QPushButton("Remover", bar);
    sculpt_undo_btn_    = new QPushButton("Desfazer pincel", bar);

    layout->addWidget(sculpt_smooth_btn_);
    layout->addWidget(sculpt_smudge_btn_);
    layout->addWidget(sculpt_elevate_btn_);
    layout->addWidget(sculpt_remove_btn_);
    layout->addWidget(sculpt_undo_btn_);

    auto set_sculpt_btn_active = [this](SculptTool tool) {
        active_sculpt_tool_ = tool;
        sculpt_smooth_btn_->setProperty("primary", tool == SculptTool::Smooth);
        sculpt_smudge_btn_->setProperty("primary", tool == SculptTool::Smudge);
        sculpt_elevate_btn_->setProperty("primary", tool == SculptTool::Elevate);
        sculpt_remove_btn_->setProperty("primary", tool == SculptTool::Remove);
        for (auto* b : {sculpt_smooth_btn_, sculpt_smudge_btn_, sculpt_elevate_btn_, sculpt_remove_btn_}) {
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
    };

    connect(sculpt_smooth_btn_, &QPushButton::clicked, this, [set_sculpt_btn_active]{ set_sculpt_btn_active(SculptTool::Smooth); });
    connect(sculpt_smudge_btn_, &QPushButton::clicked, this, [set_sculpt_btn_active]{ set_sculpt_btn_active(SculptTool::Smudge); });
    connect(sculpt_elevate_btn_, &QPushButton::clicked, this, [set_sculpt_btn_active]{ set_sculpt_btn_active(SculptTool::Elevate); });
    connect(sculpt_remove_btn_, &QPushButton::clicked, this, [set_sculpt_btn_active]{ set_sculpt_btn_active(SculptTool::Remove); });

    set_sculpt_btn_active(SculptTool::Smooth);

    layout->addWidget(new QLabel("Tamanho:", bar));
    brush_size_spin_ = new QDoubleSpinBox(bar);
    brush_size_spin_->setRange(1.0, 300.0);
    brush_size_spin_->setValue(25.0);
    brush_size_spin_->setSuffix(" mm");
    layout->addWidget(brush_size_spin_);

    layout->addWidget(new QLabel("Força:", bar));
    brush_strength_slider_ = new QSlider(Qt::Horizontal, bar);
    brush_strength_slider_->setRange(1, 100);
    brush_strength_slider_->setValue(50);
    brush_strength_slider_->setMaximumWidth(90);
    layout->addWidget(brush_strength_slider_);

    layout->addStretch(1);

    layout->addWidget(new QLabel("Altura total máx.:", bar));
    max_height_spin_ = new QDoubleSpinBox(bar);
    max_height_spin_->setRange(1.0, 1000.0);
    max_height_spin_->setValue(200.0);
    max_height_spin_->setSuffix(" mm");
    layout->addWidget(max_height_spin_);

    connect(max_height_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double val){
        max_total_height_ = val;
        rebuild_view();
    });

    return bar;
}

void Modeling3DWorkspace::open_relief_image() {
    const QString file = QFileDialog::getOpenFileName(
        this, "Abrir Imagem para Relevo 3D", {},
        "Imagens e Mapas de Altura (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;PNG (*.png);;TIFF 16-bit (*.tif *.tiff);;Todos (*.*)"
    );
    if (file.isEmpty()) return;

    ReliefComponent comp;
    std::string err;
    if (!comp.load_from_file(file.toStdString(), &err)) {
        QMessageBox::warning(this, "Abrir Imagem", QString::fromStdString(err));
        return;
    }

    QFileInfo fi(file);
    comp.name = fi.baseName().toStdString();
    comp.id = "comp_" + std::to_string(part_.relief_components.size() + 1);
    comp.width = relief_w_->value();
    comp.height = relief_h_->value();
    comp.shape_height = shape_height_->value();
    comp.base_height = base_height_->value();
    comp.combine_mode = current_combine_mode_;

    img_status_label_->setText(QString("%1 · %2x%3 px · %4")
        .arg(fi.fileName())
        .arg(comp.img_width)
        .arg(comp.img_height)
        .arg(comp.is_16bit ? "16-bit Alta Precisão" : "8-bit Grayscale"));

    // Se já estiver editando um relevo existente, atualiza a imagem dele
    if (editing_relief_idx_ >= 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        part_.relief_components[size_t(editing_relief_idx_)].original_filename = comp.original_filename;
        part_.relief_components[size_t(editing_relief_idx_)].original_encoded_base64 = comp.original_encoded_base64;
        part_.relief_components[size_t(editing_relief_idx_)].img_width = comp.img_width;
        part_.relief_components[size_t(editing_relief_idx_)].img_height = comp.img_height;
        part_.relief_components[size_t(editing_relief_idx_)].is_16bit = comp.is_16bit;
        part_.relief_components[size_t(editing_relief_idx_)].raw_8bit = comp.raw_8bit;
        part_.relief_components[size_t(editing_relief_idx_)].raw_16bit = comp.raw_16bit;
    } else {
        // Cria novo componente automaticamente
        part_.relief_components.push_back(comp);
        editing_relief_idx_ = int(part_.relief_components.size()) - 1;
        editing_cad_idx_ = -1;
    }

    rebuild_tree();
    rebuild_view();
}

ReliefComponent Modeling3DWorkspace::read_relief_form() const {
    ReliefComponent c;
    c.width = relief_w_->value();
    c.height = relief_h_->value();
    c.pos_x = relief_x_->value();
    c.pos_y = relief_y_->value();
    c.shape_height = shape_height_->value();
    c.base_height = base_height_->value();
    c.contrast = contrast_spin_->value();
    c.smoothness = smoothness_slider_->value() / 100.0;
    c.invert = invert_check_->isChecked();
    c.nature = nature_carved_->isChecked() ? ReliefNature::Carved : ReliefNature::Raised;
    c.combine_mode = current_combine_mode_;
    return c;
}

void Modeling3DWorkspace::sync_relief_form(const ReliefComponent& c) {
    relief_w_->setValue(c.width);
    relief_h_->setValue(c.height);
    relief_x_->setValue(c.pos_x);
    relief_y_->setValue(c.pos_y);
    shape_height_->setValue(c.shape_height);
    base_height_->setValue(c.base_height);
    contrast_spin_->setValue(c.contrast);
    smoothness_slider_->setValue(static_cast<int>(c.smoothness * 100.0));
    invert_check_->setChecked(c.invert);
    nature_carved_->setChecked(c.nature == ReliefNature::Carved);
    nature_raised_->setChecked(c.nature == ReliefNature::Raised);
    set_combine_mode(c.combine_mode);

    if (c.has_image()) {
        img_status_label_->setText(QString("%1 · %2x%3 px · %4")
            .arg(QString::fromStdString(c.original_filename.empty() ? c.name : c.original_filename))
            .arg(c.img_width)
            .arg(c.img_height)
            .arg(c.is_16bit ? "16-bit Alta Precisão" : "8-bit Grayscale"));
    } else {
        img_status_label_->setText("Nenhuma imagem carregada");
    }
}

void Modeling3DWorkspace::add_relief_component() {
    auto c = read_relief_form();
    c.name = "Relevo " + std::to_string(part_.relief_components.size() + 1);
    c.id = "comp_" + std::to_string(part_.relief_components.size() + 1);

    if (!c.has_image() && editing_relief_idx_ >= 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        // Herda imagem do selecionado se houver
        const auto& cur = part_.relief_components[size_t(editing_relief_idx_)];
        c.img_width = cur.img_width;
        c.img_height = cur.img_height;
        c.is_16bit = cur.is_16bit;
        c.raw_8bit = cur.raw_8bit;
        c.raw_16bit = cur.raw_16bit;
        c.original_filename = cur.original_filename;
        c.original_encoded_base64 = cur.original_encoded_base64;
    }

    part_.relief_components.push_back(c);
    editing_relief_idx_ = int(part_.relief_components.size()) - 1;
    editing_cad_idx_ = -1;
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::apply_relief_component() {
    if (editing_relief_idx_ < 0 || editing_relief_idx_ >= int(part_.relief_components.size())) {
        QMessageBox::information(this, "Relevo 3D", "Selecione um componente de relevo na árvore para editar.");
        return;
    }
    auto& target = part_.relief_components[size_t(editing_relief_idx_)];
    auto f = read_relief_form();
    target.width = f.width;
    target.height = f.height;
    target.pos_x = f.pos_x;
    target.pos_y = f.pos_y;
    target.shape_height = f.shape_height;
    target.base_height = f.base_height;
    target.contrast = f.contrast;
    target.smoothness = f.smoothness;
    target.invert = f.invert;
    target.nature = f.nature;
    target.combine_mode = f.combine_mode;

    rebuild_tree();
    rebuild_view();
}

Extrusion Modeling3DWorkspace::read_cad_form() const {
    Extrusion f;
    f.profile = static_cast<Profile>(profile_->currentData().toInt());
    f.width = width_->value();
    f.height = height_->value();
    f.start_z = start_z_->value();
    f.depth = depth_->value();
    f.second_depth = depth2_->value();
    f.target_z = target_z_->value();
    f.offset = offset_->value();
    f.draft_degrees = draft_->value();
    f.end = static_cast<EndCondition>(end_->currentData().toInt());
    f.reverse = reverse_->isChecked();
    f.second_direction = two_directions_->isChecked();
    f.suppressed = suppressed_->isChecked();
    return f;
}

void Modeling3DWorkspace::sync_cad_form(const Extrusion& f) {
    profile_->setCurrentIndex(profile_->findData(int(f.profile)));
    width_->setValue(f.width);
    height_->setValue(f.height);
    start_z_->setValue(f.start_z);
    depth_->setValue(f.depth);
    depth2_->setValue(f.second_depth);
    target_z_->setValue(f.target_z);
    offset_->setValue(f.offset);
    draft_->setValue(f.draft_degrees);
    end_->setCurrentIndex(end_->findData(int(f.end)));
    reverse_->setChecked(f.reverse);
    two_directions_->setChecked(f.second_direction);
    suppressed_->setChecked(f.suppressed);
}

void Modeling3DWorkspace::add_extrusion() {
    auto f = read_cad_form();
    const int n = int(part_.features.size()) + 1;
    f.name = "Extrusão " + std::to_string(n);
    part_.features.push_back(f);
    editing_cad_idx_ = n - 1;
    editing_relief_idx_ = -1;
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::apply_extrusion() {
    if (editing_cad_idx_ < 0 || editing_cad_idx_ >= int(part_.features.size())) {
        QMessageBox::information(this, "Modelagem 3D", "Selecione uma extrusão na árvore para editar.");
        return;
    }
    auto f = read_cad_form();
    f.name = part_.features[size_t(editing_cad_idx_)].name;
    part_.features[size_t(editing_cad_idx_)] = f;
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::select_tree_item(QTreeWidgetItem* item) {
    if (!item) return;
    const int type = item->data(0, Qt::UserRole).toInt();
    const int idx  = item->data(0, Qt::UserRole + 1).toInt();

    if (type == 1 && idx >= 0 && idx < int(part_.features.size())) {
        // Extrusão CAD
        editing_cad_idx_ = idx;
        editing_relief_idx_ = -1;
        sync_cad_form(part_.features[size_t(idx)]);
        left_tabs_->setCurrentIndex(1); // Aba Esboço CAD
    } else if (type == 2 && idx >= 0 && idx < int(part_.relief_components.size())) {
        // Componente de Relevo
        editing_relief_idx_ = idx;
        editing_cad_idx_ = -1;
        sync_relief_form(part_.relief_components[size_t(idx)]);
        left_tabs_->setCurrentIndex(0); // Aba Imagem e Relevo
    }
}

void Modeling3DWorkspace::toggle_selected_visibility() {
    if (editing_relief_idx_ >= 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        auto& c = part_.relief_components[size_t(editing_relief_idx_)];
        c.visible = !c.visible;
        rebuild_tree();
        rebuild_view();
    } else if (editing_cad_idx_ >= 0 && editing_cad_idx_ < int(part_.features.size())) {
        auto& f = part_.features[size_t(editing_cad_idx_)];
        f.suppressed = !f.suppressed;
        rebuild_tree();
        rebuild_view();
    }
}

void Modeling3DWorkspace::toggle_selected_lock() {
    if (editing_relief_idx_ >= 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        auto& c = part_.relief_components[size_t(editing_relief_idx_)];
        c.locked = !c.locked;
        rebuild_tree();
    }
}

void Modeling3DWorkspace::move_selected_up() {
    if (editing_relief_idx_ > 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        std::swap(part_.relief_components[size_t(editing_relief_idx_)], part_.relief_components[size_t(editing_relief_idx_ - 1)]);
        editing_relief_idx_--;
        rebuild_tree();
        rebuild_view();
    }
}

void Modeling3DWorkspace::move_selected_down() {
    if (editing_relief_idx_ >= 0 && editing_relief_idx_ + 1 < int(part_.relief_components.size())) {
        std::swap(part_.relief_components[size_t(editing_relief_idx_)], part_.relief_components[size_t(editing_relief_idx_ + 1)]);
        editing_relief_idx_++;
        rebuild_tree();
        rebuild_view();
    }
}

void Modeling3DWorkspace::delete_selected() {
    if (editing_relief_idx_ >= 0 && editing_relief_idx_ < int(part_.relief_components.size())) {
        part_.relief_components.erase(part_.relief_components.begin() + editing_relief_idx_);
        editing_relief_idx_ = -1;
        rebuild_tree();
        rebuild_view();
    } else if (editing_cad_idx_ >= 0 && editing_cad_idx_ < int(part_.features.size())) {
        part_.features.erase(part_.features.begin() + editing_cad_idx_);
        editing_cad_idx_ = -1;
        rebuild_tree();
        rebuild_view();
    }
}

void Modeling3DWorkspace::rebuild_tree() {
    const QSignalBlocker blocker(tree_);
    tree_->clear();

    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(part_.name)});
    root->setData(0, Qt::UserRole, 0);

    // Grupo 1: Base da peça (CAD Paramétrico)
    auto* cad_grp = new QTreeWidgetItem(root, {"Base da Peça (Recursos CAD)"});
    for (size_t i = 0; i < part_.features.size(); ++i) {
        const auto& f = part_.features[i];
        const QString prof = (f.profile == Profile::Rectangle ? "Retângulo" :
                              f.profile == Profile::Circle ? "Círculo" :
                              f.profile == Profile::Triangle ? "Triângulo" : "Hexágono");
        auto* item = new QTreeWidgetItem(cad_grp, {
            QString("%1 · %2 (%3 mm)%4")
                .arg(QString::fromStdString(f.name))
                .arg(prof)
                .arg(f.depth, 0, 'f', 1)
                .arg(f.suppressed ? " [suprimido]" : "")
        });
        item->setData(0, Qt::UserRole, 1);     // tipo 1 = CAD
        item->setData(0, Qt::UserRole + 1, int(i)); // índice
        if (int(i) == editing_cad_idx_) tree_->setCurrentItem(item);
    }
    cad_grp->setExpanded(true);

    // Grupo 2: Componentes de Relevo (Aspire 3D)
    auto* relief_grp = new QTreeWidgetItem(root, {"Relevos 3D e Imagens"});
    for (size_t i = 0; i < part_.relief_components.size(); ++i) {
        const auto& c = part_.relief_components[i];
        const QString mode_str = QString::fromStdString(ReliefEngine::combine_mode_name(c.combine_mode));
        auto* item = new QTreeWidgetItem(relief_grp, {
            QString("%1%2%3 · %4 mm (%5)")
                .arg(QString::fromStdString(c.name))
                .arg(c.visible ? "" : " [oculto]")
                .arg(c.locked ? " [travado]" : "")
                .arg(c.shape_height, 0, 'f', 1)
                .arg(mode_str)
        });
        item->setData(0, Qt::UserRole, 2);     // tipo 2 = Relevo
        item->setData(0, Qt::UserRole + 1, int(i));
        if (int(i) == editing_relief_idx_) tree_->setCurrentItem(item);
    }
    relief_grp->setExpanded(true);
    root->setExpanded(true);
}

void Modeling3DWorkspace::rebuild_view() {
    if (!viewport_) return;

    const auto mesh = build_mesh(part_);
    viewport_->clear_simulation();

    if (mesh.indices.empty()) {
        viewport_->set_workpiece(100, 60, 0, 18);
        viewport_->set_standard_view("iso");
        return;
    }

    double minx = 1e100, miny = 1e100, minz = 1e100;
    double maxx = -1e100, maxy = -1e100, maxz = -1e100;
    for (size_t i = 0; i + 5 < mesh.vertices.size(); i += 6) {
        minx = std::min(minx, double(mesh.vertices[i]));
        maxx = std::max(maxx, double(mesh.vertices[i]));
        miny = std::min(miny, double(mesh.vertices[i + 1]));
        maxy = std::max(maxy, double(mesh.vertices[i + 1]));
        minz = std::min(minz, double(mesh.vertices[i + 2]));
        maxz = std::max(maxz, double(mesh.vertices[i + 2]));
    }

    viewport_->set_workpiece(std::max(1.0, maxx - minx), std::max(1.0, maxy - miny), minz, maxz);
    viewport_->set_simulation_mesh(mesh.vertices, mesh.indices);
}

void Modeling3DWorkspace::new_part() {
    bool ok = false;
    const auto name = QInputDialog::getText(this, "Nova Peça 3D", "Nome da peça:", QLineEdit::Normal, "Peça 1", &ok);
    if (!ok) return;

    part_ = Part{};
    part_.name = name.isEmpty() ? "Peça 1" : name.toStdString();
    file_path_.clear();
    editing_cad_idx_ = -1;
    editing_relief_idx_ = -1;
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::open_part() {
    const auto path = QFileDialog::getOpenFileName(this, "Abrir Peça 3D", {}, "Peça CNC-CAM 3D (*.cncpart)");
    if (path.isEmpty()) return;

    Part loaded;
    std::string err;
    if (!load_part(loaded, path.toStdString(), &err)) {
        QMessageBox::critical(this, "Abrir Peça", QString::fromStdString(err));
        return;
    }
    part_ = std::move(loaded);
    file_path_ = path;
    editing_cad_idx_ = -1;
    editing_relief_idx_ = -1;
    rebuild_tree();
    rebuild_view();
}

void Modeling3DWorkspace::save_part_as() {
    const auto path = QFileDialog::getSaveFileName(this, "Salvar Peça 3D", file_path_, "Peça CNC-CAM 3D (*.cncpart)");
    if (path.isEmpty()) return;
    file_path_ = path;
    save_part();
}

void Modeling3DWorkspace::save_part() {
    if (file_path_.isEmpty()) {
        save_part_as();
        return;
    }
    std::string err;
    if (!cad::model3d::save_part(part_, file_path_.toStdString(), &err)) {
        QMessageBox::critical(this, "Salvar Peça", QString::fromStdString(err));
    }
}

} // namespace cnc::ui
