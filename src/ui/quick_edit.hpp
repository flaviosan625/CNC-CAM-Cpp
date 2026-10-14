#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <QString>
#include <QWidget>

#include "cad/entities.hpp"

class QCheckBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QToolButton;

namespace cnc::ui {

// Caixa de edição rápida portada de src/ui/quick_edit.py. Dois cliques em um
// vetor abrem os campos que o operador normalmente mede na peça.
class QuickEditPopup final : public QWidget {
public:
    using Transform = std::function<void(cad::Entity&)>;
    using ApplyCallback = std::function<void(Transform, const QString&)>;

    QuickEditPopup(cad::EntityPtr entity, ApplyCallback apply, QWidget* parent = nullptr);

    static bool supports(const cad::Entity& entity);
    static QString entity_name(const cad::Entity& entity);
    void show_at(const QPoint& global_position);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    using Value = std::variant<double, QString, bool>;
    struct Field {
        QString key;
        QString label;
        QString suffix = "mm";
        std::optional<double> minimum = 0.001;
        bool text = false;
        bool boolean = false;
        std::function<Value(const cad::Entity&)> read;
        std::function<void(cad::Entity&, const Value&)> write;
    };

    void build_fields();
    void build_ui();
    void apply();
    void sync_proportion(int source, const QString& text);
    void update_proportion_lock(bool locked);
    void show_error(const QString& message);

    cad::EntityPtr entity_;
    ApplyCallback apply_callback_;
    std::vector<Field> fields_;
    std::vector<QWidget*> editors_;
    std::optional<std::pair<int, int>> dimension_pair_;
    double dimension_ratio_ = 1.0;
    QToolButton* proportion_lock_ = nullptr;
    QLabel* hint_ = nullptr;
};

}  // namespace cnc::ui
