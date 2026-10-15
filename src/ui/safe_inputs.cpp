// Porte de src/ui/safe_inputs.py: a roda rola o painel, nao altera valores.
#include "ui/safe_inputs.hpp"

#include <algorithm>
#include <cmath>

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QScrollBar>
#include <QWheelEvent>

namespace cnc::ui {

void redirect_wheel(QWidget* widget, QWheelEvent* event) {
    if (!widget || !event) return;

    QAbstractScrollArea* scroll_area = nullptr;
    QWidget* parent = widget->parentWidget();
    while (parent) {
        scroll_area = qobject_cast<QAbstractScrollArea*>(parent);
        if (scroll_area && scroll_area->verticalScrollBar() &&
            scroll_area->verticalScrollBar()->maximum() > 0) {
            break;
        }
        parent = parent->parentWidget();
    }

    if (!scroll_area || !scroll_area->verticalScrollBar()) {
        event->ignore();
        return;
    }

    QScrollBar* bar = scroll_area->verticalScrollBar();
    const int pixel = event->pixelDelta().y();
    const double distance = pixel != 0
                                ? pixel
                                : (event->angleDelta().y() / 120.0) *
                                      std::max(24, bar->singleStep() * 3);
    bar->setValue(static_cast<int>(std::lround(bar->value() - distance)));
    event->accept();
}

SafeDoubleSpinBox::SafeDoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
}
void SafeDoubleSpinBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }

SafeSpinBox::SafeSpinBox(QWidget* parent) : QSpinBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
}
void SafeSpinBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }

SafeComboBox::SafeComboBox(QWidget* parent) : QComboBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
}
void SafeComboBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }

SafeWheelFilter::SafeWheelFilter(QObject* parent) : QObject(parent) {}

bool SafeWheelFilter::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        auto* wheel_event = static_cast<QWheelEvent*>(event);
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget) {
            bool is_input = false;
            for (QObject* cur = widget; cur; cur = cur->parent()) {
                if (qobject_cast<QAbstractSpinBox*>(cur) ||
                    qobject_cast<QComboBox*>(cur) ||
                    qobject_cast<QAbstractSlider*>(cur)) {
                    // Nao bloquear o scroll da lista quando o dropdown do combo estiver aberto
                    if (widget->inherits("QComboBoxPrivateContainer") ||
                        widget->inherits("QComboMenuDelegate")) {
                        break;
                    }
                    is_input = true;
                    break;
                }
            }

            if (is_input) {
                redirect_wheel(widget, wheel_event);
                return true; // consome o evento para nunca alterar o valor por engano
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace cnc::ui
