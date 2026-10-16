#include "ui/responsive.hpp"
#include "ui/safe_inputs.hpp"
#include <algorithm>
#include <QBoxLayout>
#include <QEvent>
#include <QEventLoop>
#include <QFormLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>

namespace cnc::ui {
ResponsiveDialog::ResponsiveDialog(QWidget* parent) : QDialog(parent) {
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    setSizeGripEnabled(true);
}

int ResponsiveDialog::exec() {
    setWindowModality(Qt::NonModal);
    QEventLoop loop;
    QObject::connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    show();
    loop.exec();
    return result();
}

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
        if (minimumWidth() <= 0) setMinimumSize(340, 280);
        setSizeGripEnabled(true);
    }
    if (screen() && !positioned_) {
        positioned_ = true;
        const QRect available = screen()->availableGeometry().adjusted(8, 34, -8, -8);
        QWidget* host = parentWidget() ? parentWidget()->window() : nullptr;
        if (!host || host == this) host = nullptr;
        const QRect host_rect = host
            ? QRect(host->mapToGlobal(QPoint(0, 0)), host->size())
            : available;

        const bool is_docked = property("dockRight").toBool()
            || (!property("centerModal").toBool()
                && (windowTitle().startsWith("Percurso de ")
                    || (windowTitle().isEmpty() && minimumWidth() <= 420)));

        if (is_docked) {
            const int dock_width = std::clamp(host_rect.width() / 4, 340, 420);
            const int dock_height = std::min(host_rect.height(), available.height());
            setMinimumSize(std::min(320, dock_width), std::min(280, dock_height));
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resize(dock_width, dock_height);
            const int right = host_rect.right() + 1;
            const int x = std::clamp(right - dock_width, available.left(),
                                     std::max(available.left(), available.right() - dock_width + 1));
            const int y = std::clamp(host_rect.top(), available.top(),
                                     std::max(available.top(), available.bottom() - dock_height + 1));
            move(x, y);
        } else {
            const int target_w = std::min(std::max(width(), minimumWidth()), available.width());
            const int target_h = std::min(std::max(height(), minimumHeight()), available.height());
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resize(target_w, target_h);
            const int cx = host_rect.center().x() - target_w / 2;
            const int cy = host_rect.center().y() - target_h / 2;
            const int x = std::clamp(cx, available.left(), std::max(available.left(), available.right() - target_w + 1));
            const int y = std::clamp(cy, available.top(), std::max(available.top(), available.bottom() - target_h + 1));
            move(x, y);
        }
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
