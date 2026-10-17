#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QPoint>
#include <QWidget>

#include "cad/entities.hpp"

class QToolButton;

namespace cnc::ui {

// Mini-Menu Flutuante de Alinhamento Rápido (Contextual HUD Bar).
// Aparece automaticamente ao lado do cursor quando 2 ou mais entidades estão selecionadas,
// permitindo aplicar relações do SolidWorks (Concêntrico, Igualar raio/tamanho, Alinhar centro H/V,
// Tangente, Agrupar/Unir) em 1 clique rápido com zero lag.
class ContextualHudBar : public QWidget {
    Q_OBJECT
public:
    explicit ContextualHudBar(QWidget* parent = nullptr);

    void set_actions(std::function<void()> on_concentric,
                     std::function<void()> on_equal_size,
                     std::function<void()> on_align_center_h,
                     std::function<void()> on_align_center_v,
                     std::function<void()> on_tangent,
                     std::function<void()> on_group_join);

    void show_at(const QPoint& viewport_pos, const std::vector<cad::EntityPtr>& selection);
    void hide_hud();

    // Métodos para acionamento direto (útil para testes unitários e automação)
    void trigger_concentric();
    void trigger_equal_size();
    void trigger_align_center_h();
    void trigger_align_center_v();
    void trigger_tangent();
    void trigger_group_join();

    bool has_selection() const { return selection_count_ >= 2; }
    size_t selection_count() const { return selection_count_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QToolButton* create_button(const QString& glyph, const QString& tooltip,
                               const std::function<void()>& callback);

    QToolButton* btn_concentric_ = nullptr;
    QToolButton* btn_equal_size_ = nullptr;
    QToolButton* btn_align_h_ = nullptr;
    QToolButton* btn_align_v_ = nullptr;
    QToolButton* btn_tangent_ = nullptr;
    QToolButton* btn_group_join_ = nullptr;

    std::function<void()> on_concentric_;
    std::function<void()> on_equal_size_;
    std::function<void()> on_align_center_h_;
    std::function<void()> on_align_center_v_;
    std::function<void()> on_tangent_;
    std::function<void()> on_group_join_;

    size_t selection_count_ = 0;
};

}  // namespace cnc::ui
