// Porte de src/ui/safe_inputs.py: a roda rola o painel, nao altera valores.
#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QSpinBox>

class QWheelEvent;

namespace cnc::ui {

void redirect_wheel(QWidget* widget, QWheelEvent* event);

class SafeDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit SafeDoubleSpinBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

class SafeSpinBox : public QSpinBox {
    Q_OBJECT
public:
    explicit SafeSpinBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

class SafeComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SafeComboBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

class SafeWheelFilter final : public QObject {
    Q_OBJECT
public:
    explicit SafeWheelFilter(QObject* parent = nullptr);
    bool eventFilter(QObject* watched, QEvent* event) override;
};

}  // namespace cnc::ui
