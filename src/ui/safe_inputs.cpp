// Porte de src/ui/safe_inputs.py: a roda rola o painel, nao altera valores.
#include "ui/safe_inputs.hpp"

#include <algorithm>
#include <cmath>

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

namespace cnc::ui {
namespace {

void redirect_wheel(QWidget* widget, QWheelEvent* event) {
    QAbstractScrollArea* scroll_area = nullptr;
    QWidget* parent = widget->parentWidget();
    while (parent) {
        scroll_area = qobject_cast<QAbstractScrollArea*>(parent);
        if (scroll_area) break;
        parent = parent->parentWidget();
    }

    if (!scroll_area) {
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

}  // namespace

void SafeDoubleSpinBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }
void SafeSpinBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }
void SafeComboBox::wheelEvent(QWheelEvent* event) { redirect_wheel(this, event); }

}  // namespace cnc::ui
