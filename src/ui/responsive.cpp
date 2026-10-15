#include "ui/responsive.hpp"
#include "ui/safe_inputs.hpp"
#include <algorithm>
#include <QBoxLayout>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>

namespace cnc::ui {
void ResponsiveDialog::resizeEvent(QResizeEvent* event) {
    for (auto* splitter : findChildren<QSplitter*>()) {
        if (splitter->property("keepOrientation").toBool()) continue;
        splitter->setOrientation(width() < 760 ? Qt::Vertical : Qt::Horizontal);
    }
    QDialog::resizeEvent(event);
}
void ResponsiveDialog::showEvent(QShowEvent* event) {
    if (!prepared_) {
        prepared_ = true;
        installEventFilter(new SafeWheelFilter(this));
        for (auto* form : findChildren<QFormLayout*>()) {
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        }
        // A top-level scrolling body makes every field reachable on short displays.
        // Existing scrolling forms retain their fixed dialog action row.
        bool has_body_scroll = false;
        for (auto* area : findChildren<QScrollArea*>())
            if (area->parentWidget() == this) has_body_scroll = true;
        if (layout() && !has_body_scroll) {
            auto* body = new QWidget(this);
            body->setLayout(layout());
            auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
            auto* area = new QScrollArea(this); area->setObjectName("ResponsiveDialogScroll");
            area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame);
            area->setWidget(body); outer->addWidget(area);
        }
        setMinimumSize(340, 280); setSizeGripEnabled(true);
    }
    if (screen()) {
        const QRect available = screen()->availableGeometry().adjusted(18, 34, -18, -18);
        setMinimumSize(std::min(340, available.width()), std::min(280, available.height()));
        resize(std::min(width(), available.width()), std::min(height(), available.height()));
        move(std::clamp(x(), available.left(), std::max(available.left(), available.right() - width())),
             std::clamp(y(), available.top(), std::max(available.top(), available.bottom() - height())));
    }
    QDialog::showEvent(event);
}
AdaptiveSection::AdaptiveSection(QWidget* widget, QBoxLayout* layout, int breakpoint)
    : QObject(widget), layout_(layout), breakpoint_(breakpoint) {
    widget->installEventFilter(this);
    layout_->setDirection(QBoxLayout::TopToBottom);
}
bool AdaptiveSection::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        const auto* widget = static_cast<QWidget*>(object);
        const auto direction = widget->width() < breakpoint_ ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
        if (layout_->direction() != direction) layout_->setDirection(direction);
        if (layout_->count() > 0 && layout_->itemAt(0)->widget()) {
            auto* fields = layout_->itemAt(0)->widget();
            fields->setSizePolicy(QSizePolicy::Expanding, direction == QBoxLayout::TopToBottom ? QSizePolicy::Maximum : QSizePolicy::Preferred);
            layout_->setStretch(0, direction == QBoxLayout::TopToBottom ? 0 : 1);
        }
    }
    return false;
}
FlowLayout::FlowLayout(int spacing) { setSpacing(spacing); setContentsMargins(0, 0, 0, 0); }
FlowLayout::~FlowLayout() { while (auto* item = takeAt(0)) delete item; }
void FlowLayout::addItem(QLayoutItem* item) { items_.push_back(item); }
int FlowLayout::count() const { return int(items_.size()); }
QLayoutItem* FlowLayout::itemAt(int index) const { return items_.value(index, nullptr); }
QLayoutItem* FlowLayout::takeAt(int index) { return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr; }
QSize FlowLayout::sizeHint() const { return minimumSize(); }
QSize FlowLayout::minimumSize() const {
    QSize size; for (auto* item : items_) size = size.expandedTo(item->minimumSize());
    const auto m = contentsMargins(); return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}
int FlowLayout::heightForWidth(int width) const { return arrange(QRect(0, 0, width, 0), true); }
void FlowLayout::setGeometry(const QRect& rect) { QLayout::setGeometry(rect); arrange(rect, false); }
int FlowLayout::arrange(const QRect& rect, bool test) const {
    const auto m = contentsMargins(); const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x(), y = area.y(), line_height = 0;
    for (auto* item : items_) {
        const QSize size = item->sizeHint();
        if (x > area.x() && x + size.width() > area.right() + 1) { x = area.x(); y += line_height + spacing(); line_height = 0; }
        if (!test) item->setGeometry(QRect(QPoint(x, y), size));
        x += size.width() + spacing(); line_height = std::max(line_height, size.height());
    }
    return y + line_height - rect.y() + m.bottom();
}
} // namespace cnc::ui
