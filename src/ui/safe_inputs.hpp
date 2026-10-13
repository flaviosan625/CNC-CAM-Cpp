// Porte de src/ui/safe_inputs.py: a roda rola o painel, nao altera valores.
#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

namespace cnc::ui {

class SafeDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override;
};

class SafeSpinBox : public QSpinBox {
    Q_OBJECT
public:
    using QSpinBox::QSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override;
};

class SafeComboBox : public QComboBox {
    Q_OBJECT
public:
    using QComboBox::QComboBox;

protected:
    void wheelEvent(QWheelEvent* event) override;
};

}  // namespace cnc::ui
