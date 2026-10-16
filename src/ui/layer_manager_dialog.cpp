#include "ui/layer_manager_dialog.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
    auto* layers_label = new QLabel("Camadas — marque para mostrar ou ocultar", this);
    root->addWidget(layers_label);
    layer_list_ = new QListWidget(this);
    layer_list_->setObjectName("LayerManagerList");
    layer_list_->setMinimumHeight(150);
    for (const auto& layer : layers_) {
        auto* item = new QListWidgetItem(QString::fromStdString(layer.name), layer_list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::UserRole, QString::fromStdString(layer.name));
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
    }
    const auto current_items = layer_list_->findItems(QString::fromStdString(current), Qt::MatchExactly);
    const int current_index = current_items.empty() ? 0 : layer_list_->row(current_items.front());
    if (layer_list_->count() > 0) layer_list_->setCurrentRow(current_index);
    root->addWidget(layer_list_);

    auto* rename_label = new QLabel("Nome da camada selecionada", this);
    root->addWidget(rename_label);
    auto* rename_row = new QHBoxLayout();
    current_layer_name_ = new QLineEdit(this);
    current_layer_name_->setObjectName("LayerManagerCurrentName");
    current_layer_name_->setPlaceholderText("Selecione uma camada para renomear");
    rename_button_ = new QPushButton("Renomear", this);
    rename_button_->setObjectName("LayerManagerRename");
    rename_button_->setAutoDefault(false);
    rename_row->addWidget(current_layer_name_, 1);
    rename_row->addWidget(rename_button_);
    root->addLayout(rename_row);

    auto* create_row = new QHBoxLayout();
    new_layer_name_ = new QLineEdit(this);
    new_layer_name_->setObjectName("LayerManagerNewName");
    new_layer_name_->setPlaceholderText("Nome da nova camada");
    auto* create = new QPushButton("Criar camada", this);
    create->setObjectName("LayerManagerAdd");
    create->setAutoDefault(false);
    create_row->addWidget(new_layer_name_, 1);
    create_row->addWidget(create);
    root->addLayout(create_row);

    auto* form = new QFormLayout();
    color_button_ = new QPushButton(this);
    color_button_->setObjectName("LayerManagerColor");
    locked_ = new QCheckBox("Bloqueada", this);
    locked_->setObjectName("LayerManagerLocked");
    form->addRow("Cor", color_button_);
    form->addRow(QString(), locked_);
    root->addLayout(form);

    auto* actions = new QHBoxLayout();
    remove_button_ = new QPushButton("Remover camada", this);
    remove_button_->setObjectName("LayerManagerRemove");
    remove_button_->setAutoDefault(false);
    actions->addStretch();
    actions->addWidget(remove_button_);
    root->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Aplicar");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    root->addWidget(buttons);

    connect(layer_list_, &QListWidget::currentRowChanged, this,
            [this](int) {
                if (current_name_row_ >= 0 && !rename_current()) {
                    const QSignalBlocker blocker(layer_list_);
                    layer_list_->setCurrentRow(current_name_row_);
                    return;
                }
                load_current();
            });
    connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int row = layer_list_->row(item);
        if (row >= 0 && row < static_cast<int>(layers_.size())) {
            auto& layer = layers_[static_cast<size_t>(row)];
            const QString name = item->text().trimmed();
            if (name != QString::fromStdString(layer.name)) {
                if (!valid_name(name, row)) {
                    const QSignalBlocker blocker(layer_list_);
                    item->setText(QString::fromStdString(layer.name));
                    return;
                }
                layer.name = name.toStdString();
                if (row == current_name_row_) current_layer_name_->setText(name);
            }
            {
                const QSignalBlocker blocker(layer_list_);
                item->setText(name);
            }
            layer.visible = item->checkState() == Qt::Checked;
            if (row == layer_list_->currentRow()) load_current();
        }
    });
    connect(locked_, &QCheckBox::toggled, this, [this](bool) { save_current(); });
    connect(color_button_, &QPushButton::clicked, this, &LayerManagerDialog::choose_color);
    connect(rename_button_, &QPushButton::clicked, this, [this] { rename_current(); });
    connect(current_layer_name_, &QLineEdit::returnPressed, this, [this] { rename_current(); });
    connect(create, &QPushButton::clicked, this, &LayerManagerDialog::add_layer);
    connect(new_layer_name_, &QLineEdit::returnPressed, this, &LayerManagerDialog::add_layer);
    connect(remove_button_, &QPushButton::clicked, this, &LayerManagerDialog::remove_layer);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (current_layer() && !rename_current()) return;
        for (int i = 0; i < layer_list_->count(); ++i) {
            if (!valid_name(layer_list_->item(i)->text().trimmed(), i)) return;
        }
        save_current();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    load_current();
}

std::vector<cad::Layer> LayerManagerDialog::result_layers() {
    save_current();
    for (int i = 0; i < layer_list_->count() && i < static_cast<int>(layers_.size()); ++i)
        layers_[static_cast<size_t>(i)].name = layer_list_->item(i)->text().trimmed().toStdString();
    return layers_;
}

std::vector<std::pair<std::string, std::string>> LayerManagerDialog::renamed_layers() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (int i = 0; i < layer_list_->count(); ++i) {
        const QListWidgetItem* item = layer_list_->item(i);
        if (!item->data(Qt::UserRole).isValid()) continue;
        const std::string old_name = item->data(Qt::UserRole).toString().toStdString();
        const std::string new_name = item->text().trimmed().toStdString();
        if (old_name != new_name) result.emplace_back(old_name, new_name);
    }
    return result;
}

cad::Layer* LayerManagerDialog::current_layer() {
    const int index = layer_list_->currentRow();
    if (index < 0 || index >= static_cast<int>(layers_.size())) return nullptr;
    return &layers_[static_cast<size_t>(index)];
}

void LayerManagerDialog::load_current() {
    cad::Layer* layer = current_layer();
    const bool enabled = layer != nullptr;
    current_layer_name_->setEnabled(enabled);
    rename_button_->setEnabled(enabled);
    remove_button_->setEnabled(enabled && layers_.size() > 1);
    color_button_->setEnabled(enabled);
    locked_->setEnabled(enabled);
    if (!layer) {
        current_name_row_ = -1;
        current_layer_name_->clear();
        color_button_->setText(QString());
        return;
    }
    if (current_name_row_ != layer_list_->currentRow() || !current_layer_name_->isModified())
        current_layer_name_->setText(QString::fromStdString(layer->name));
    current_name_row_ = layer_list_->currentRow();
    const QSignalBlocker locked_blocker(locked_);
    const QSignalBlocker list_blocker(layer_list_);
    if (auto* item = layer_list_->currentItem())
        item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
    locked_->setChecked(layer->locked);
    const QColor color = QColor::fromRgbF(layer->color[0], layer->color[1], layer->color[2]);
    color_button_->setText(color.name().toUpper());
    color_button_->setStyleSheet(
        QString("background-color: %1; color: %2; padding: 6px;")
            .arg(color.name(), color.lightnessF() < 0.5 ? "#ffffff" : "#111111"));
}

void LayerManagerDialog::save_current() {
    if (cad::Layer* layer = current_layer()) {
        if (auto* item = layer_list_->currentItem())
            layer->visible = item->checkState() == Qt::Checked;
        layer->locked = locked_->isChecked();
    }
}

void LayerManagerDialog::choose_color() {
    cad::Layer* layer = current_layer();
    if (!layer) return;
    const QColor current = QColor::fromRgbF(layer->color[0], layer->color[1], layer->color[2]);
    // The button paints itself with the selected color. Do not use it as the
    // dialog parent or its inline stylesheet will bleed into QColorDialog.
    const QColor chosen = QColorDialog::getColor(current, window(), "Cor da camada");
    if (!chosen.isValid()) return;
    layer->color = {chosen.redF(), chosen.greenF(), chosen.blueF()};
    load_current();
}

void LayerManagerDialog::add_layer() {
    if (current_layer() && !rename_current()) return;
    const QString name = new_layer_name_->text().trimmed();
    if (!valid_name(name)) return;
    save_current();
    cad::Layer layer;
    layer.name = name.toStdString();
    layers_.push_back(layer);
    {
        const QSignalBlocker blocker(layer_list_);
        auto* item = new QListWidgetItem(name, layer_list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setCheckState(Qt::Checked);
        layer_list_->setCurrentItem(item);
    }
    new_layer_name_->clear();
    load_current();
}

void LayerManagerDialog::remove_layer() {
    if (layers_.size() <= 1) {
        QMessageBox::information(this, "Camadas", "O documento precisa manter uma camada.");
        return;
    }
    const int index = layer_list_->currentRow();
    if (index < 0 || index >= static_cast<int>(layers_.size())) return;
    {
        const QSignalBlocker blocker(layer_list_);
        layers_.erase(layers_.begin() + index);
        delete layer_list_->takeItem(index);
        if (layer_list_->count() > 0) layer_list_->setCurrentRow(std::min(index, layer_list_->count() - 1));
    }
    current_name_row_ = -1;
    load_current();
}

bool LayerManagerDialog::rename_current() {
    if (current_name_row_ < 0 || current_name_row_ >= static_cast<int>(layers_.size())) return false;
    cad::Layer& layer = layers_[static_cast<size_t>(current_name_row_)];
    const QString name = current_layer_name_->text().trimmed();
    if (!valid_name(name, current_name_row_)) {
        current_layer_name_->setFocus();
        current_layer_name_->selectAll();
        return false;
    }
    layer.name = name.toStdString();
    {
        const QSignalBlocker blocker(layer_list_);
        layer_list_->item(current_name_row_)->setText(name);
    }
    current_layer_name_->setText(name);
    return true;
}

bool LayerManagerDialog::valid_name(const QString& name, int excluded_row) {
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Nome de camada inválido", "Informe o nome da camada.");
        return false;
    }
    for (int i = 0; i < layer_list_->count(); ++i) {
        if (i != excluded_row && layer_list_->item(i)->text().trimmed() == name) {
            QMessageBox::warning(this, "Nome de camada repetido",
                                 "Já existe uma camada com esse nome. Escolha outro nome.");
            return false;
        }
    }
    return true;
}

}  // namespace cnc::ui
