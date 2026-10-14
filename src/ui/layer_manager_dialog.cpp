#include "ui/layer_manager_dialog.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace cnc::ui {

LayerManagerDialog::LayerManagerDialog(const std::vector<cad::Layer>& layers,
                                       const std::string& current, QWidget* parent)
    : ResponsiveDialog(parent), layers_(layers) {
    setWindowTitle("Gerenciar camadas");
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    layer_combo_ = new QComboBox(this);
    layer_combo_->setObjectName("LayerManagerCombo");
    for (const auto& layer : layers_) layer_combo_->addItem(QString::fromStdString(layer.name));
    const int current_index = layer_combo_->findText(QString::fromStdString(current));
    layer_combo_->setCurrentIndex(std::max(0, current_index));
    root->addWidget(layer_combo_);

    auto* form = new QFormLayout();
    color_button_ = new QPushButton(this);
    color_button_->setObjectName("LayerManagerColor");
    visible_ = new QCheckBox("Visível", this);
    visible_->setObjectName("LayerManagerVisible");
    locked_ = new QCheckBox("Bloqueada", this);
    locked_->setObjectName("LayerManagerLocked");
    form->addRow("Cor", color_button_);
    form->addRow(QString(), visible_);
    form->addRow(QString(), locked_);
    root->addLayout(form);

    auto* actions = new QHBoxLayout();
    auto* add = new QPushButton("Nova camada", this);
    add->setObjectName("LayerManagerAdd");
    auto* remove = new QPushButton("Remover camada", this);
    remove->setObjectName("LayerManagerRemove");
    actions->addWidget(add);
    actions->addWidget(remove);
    root->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Aplicar");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    root->addWidget(buttons);

    connect(layer_combo_, &QComboBox::currentIndexChanged, this,
            [this](int) { load_current(); });
    connect(visible_, &QCheckBox::toggled, this, [this](bool) { save_current(); });
    connect(locked_, &QCheckBox::toggled, this, [this](bool) { save_current(); });
    connect(color_button_, &QPushButton::clicked, this, &LayerManagerDialog::choose_color);
    connect(add, &QPushButton::clicked, this, &LayerManagerDialog::add_layer);
    connect(remove, &QPushButton::clicked, this, &LayerManagerDialog::remove_layer);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    load_current();
}

std::vector<cad::Layer> LayerManagerDialog::result_layers() {
    save_current();
    return layers_;
}

cad::Layer* LayerManagerDialog::current_layer() {
    const int index = layer_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(layers_.size())) return nullptr;
    return &layers_[static_cast<size_t>(index)];
}

void LayerManagerDialog::load_current() {
    cad::Layer* layer = current_layer();
    const bool enabled = layer != nullptr;
    color_button_->setEnabled(enabled);
    visible_->setEnabled(enabled);
    locked_->setEnabled(enabled);
    if (!layer) return;
    const QSignalBlocker visible_blocker(visible_);
    const QSignalBlocker locked_blocker(locked_);
    visible_->setChecked(layer->visible);
    locked_->setChecked(layer->locked);
    const QColor color = QColor::fromRgbF(layer->color[0], layer->color[1], layer->color[2]);
    color_button_->setText(color.name().toUpper());
    color_button_->setStyleSheet(
        QString("background-color: %1; color: %2; padding: 6px;")
            .arg(color.name(), color.lightnessF() < 0.5 ? "#ffffff" : "#111111"));
}

void LayerManagerDialog::save_current() {
    if (cad::Layer* layer = current_layer()) {
        layer->visible = visible_->isChecked();
        layer->locked = locked_->isChecked();
    }
}

void LayerManagerDialog::choose_color() {
    cad::Layer* layer = current_layer();
    if (!layer) return;
    const QColor current = QColor::fromRgbF(layer->color[0], layer->color[1], layer->color[2]);
    const QColor chosen = QColorDialog::getColor(current, this, "Cor da camada");
    if (!chosen.isValid()) return;
    layer->color = {chosen.redF(), chosen.greenF(), chosen.blueF()};
    load_current();
}

void LayerManagerDialog::add_layer() {
    bool accepted = false;
    QString name = QInputDialog::getText(this, "Nova camada", "Nome da camada:",
                                         QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted) return;
    const bool duplicate = std::any_of(layers_.begin(), layers_.end(), [&](const cad::Layer& layer) {
        return QString::fromStdString(layer.name) == name;
    });
    if (name.isEmpty() || duplicate) {
        QMessageBox::warning(this, "Camada inválida", "Informe um nome novo para a camada.");
        return;
    }
    save_current();
    cad::Layer layer;
    layer.name = name.toStdString();
    layers_.push_back(layer);
    layer_combo_->addItem(name);
    layer_combo_->setCurrentIndex(static_cast<int>(layers_.size()) - 1);
}

void LayerManagerDialog::remove_layer() {
    if (layers_.size() <= 1) {
        QMessageBox::information(this, "Camadas", "O documento precisa manter uma camada.");
        return;
    }
    const int index = layer_combo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(layers_.size())) return;
    layers_.erase(layers_.begin() + index);
    layer_combo_->removeItem(index);
    layer_combo_->setCurrentIndex(std::max(0, index - 1));
}

}  // namespace cnc::ui
