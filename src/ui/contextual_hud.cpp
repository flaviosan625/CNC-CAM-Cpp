#include "ui/contextual_hud.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>

namespace cnc::ui {

ContextualHudBar::ContextualHudBar(QWidget* parent) : QWidget(parent) {
    setObjectName("ContextualHudBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);

    setStyleSheet(
        "QWidget#ContextualHudBar {"
        "  background-color: rgba(26, 32, 44, 0.95);"
        "  border: 1px solid rgba(148, 163, 184, 0.35);"
        "  border-radius: 8px;"
        "}"
        "QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  color: #f1f5f9;"
        "  font-family: 'Segoe UI', sans-serif;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  min-width: 28px;"
        "  min-height: 28px;"
        "  max-width: 28px;"
        "  max-height: 28px;"
        "  padding: 0px;"
        "}"
        "QToolButton:hover {"
        "  background-color: rgba(59, 130, 246, 0.65);"
        "  border: 1px solid #60a5fa;"
        "  color: #ffffff;"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(37, 99, 235, 0.9);"
        "}"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(3);

    btn_concentric_ = create_button("◎", "Concêntrico (alinha centros à seleção primária)", [this] {
        if (on_concentric_) on_concentric_();
    });

    btn_equal_size_ = create_button("⩵", "Igualar raio / tamanho (iguala dimensões à seleção primária)", [this] {
        if (on_equal_size_) on_equal_size_();
    });

    btn_align_h_ = create_button("⌖H", "Alinhar centro horizontal", [this] {
        if (on_align_center_h_) on_align_center_h_();
    });

    btn_align_v_ = create_button("⌖V", "Alinhar centro vertical", [this] {
        if (on_align_center_v_) on_align_center_v_();
    });

    btn_tangent_ = create_button("⊘", "Tornar tangente (tangencia entre peças)", [this] {
        if (on_tangent_) on_tangent_();
    });

    btn_group_join_ = create_button("⧉", "Agrupar / Unir (1 clique rápido)", [this] {
        if (on_group_join_) on_group_join_();
    });

    layout->addWidget(btn_concentric_);
    layout->addWidget(btn_equal_size_);
    layout->addWidget(btn_align_h_);
    layout->addWidget(btn_align_v_);
    layout->addWidget(btn_tangent_);
    layout->addWidget(btn_group_join_);

    adjustSize();
    hide();
}

QToolButton* ContextualHudBar::create_button(const QString& glyph, const QString& tooltip,
                                           const std::function<void()>& callback) {
    auto* btn = new QToolButton(this);
    btn->setText(glyph);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QToolButton::clicked, this, [callback]() {
        callback();
    });
    return btn;
}

void ContextualHudBar::set_actions(std::function<void()> on_concentric,
                                   std::function<void()> on_equal_size,
                                   std::function<void()> on_align_center_h,
                                   std::function<void()> on_align_center_v,
                                   std::function<void()> on_tangent,
                                   std::function<void()> on_group_join) {
    on_concentric_ = std::move(on_concentric);
    on_equal_size_ = std::move(on_equal_size);
    on_align_center_h_ = std::move(on_align_center_h);
    on_align_center_v_ = std::move(on_align_center_v);
    on_tangent_ = std::move(on_tangent);
    on_group_join_ = std::move(on_group_join);
}

void ContextualHudBar::show_at(const QPoint& viewport_pos, const std::vector<cad::EntityPtr>& selection) {
    selection_count_ = selection.size();
    if (selection_count_ < 2) {
        hide_hud();
        return;
    }

    adjustSize();
    int x = viewport_pos.x() + 15;
    int y = viewport_pos.y() - height() - 8;
    if (y < 5) y = viewport_pos.y() + 25;

    if (parentWidget()) {
        const int max_x = std::max(5, parentWidget()->width() - width() - 5);
        const int max_y = std::max(5, parentWidget()->height() - height() - 5);
        if (x > max_x) x = max_x;
        if (x < 5) x = 5;
        if (y > max_y) y = max_y;
        if (y < 5) y = 5;
    }

    move(x, y);
    show();
    raise();
}

void ContextualHudBar::hide_hud() {
    selection_count_ = 0;
    hide();
}

void ContextualHudBar::trigger_concentric() {
    if (on_concentric_) on_concentric_();
}

void ContextualHudBar::trigger_equal_size() {
    if (on_equal_size_) on_equal_size_();
}

void ContextualHudBar::trigger_align_center_h() {
    if (on_align_center_h_) on_align_center_h_();
}

void ContextualHudBar::trigger_align_center_v() {
    if (on_align_center_v_) on_align_center_v_();
}

void ContextualHudBar::trigger_tangent() {
    if (on_tangent_) on_tangent_();
}

void ContextualHudBar::trigger_group_join() {
    if (on_group_join_) on_group_join_();
}

void ContextualHudBar::paintEvent(QPaintEvent*) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

}  // namespace cnc::ui
