#pragma once
#include <QDialog>
#include <QLayout>
#include <QObject>
#include <QVector>
class QBoxLayout;

namespace cnc::ui {
// Screen/DPI aware dialogs, with scrolling as a fallback instead of clipped fields.
class ResponsiveDialog : public QDialog {
public:
    explicit ResponsiveDialog(QWidget* parent = nullptr);
    ~ResponsiveDialog() override = default;
    // Keep the synchronous form flow while leaving the drawing workspace interactive.
    int exec();
protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    bool prepared_ = false;
    bool positioned_ = false;
};

class AdaptiveSection final : public QObject {
public:
    AdaptiveSection(QWidget* widget, QBoxLayout* layout, int breakpoint = 760);
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    QBoxLayout* layout_;
    int breakpoint_;
};

class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(int spacing = 3);
    ~FlowLayout() override;
    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
private:
    int arrange(const QRect& rect, bool test) const;
    QVector<QLayoutItem*> items_;
};
} // namespace cnc::ui
