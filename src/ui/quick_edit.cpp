#include "ui/quick_edit.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/icons.hpp"

namespace cnc::ui {
namespace {

QString format_value(double value) {
    QString text = QString::number(value, 'f', 4);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text.isEmpty() || text == "-0" ? "0" : text;
}

std::optional<double> parse_number(QString text) {
    text = text.trimmed();
    text.replace(',', '.');
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value)) return std::nullopt;
    return value;
}

void set_line_length(cad::Line& line, double length) {
    const double angle = line.angle() * std::numbers::pi / 180.0;
    line.x2 = line.x1 + length * std::cos(angle);
    line.y2 = line.y1 + length * std::sin(angle);
}

void set_line_angle(cad::Line& line, double degrees) {
    const double length = line.length();
    const double angle = degrees * std::numbers::pi / 180.0;
    line.x2 = line.x1 + length * std::cos(angle);
    line.y2 = line.y1 + length * std::sin(angle);
}

double bbox_width(const cad::Entity& entity) {
    const BBox box = entity.bounding_box();
    return box.x1 - box.x0;
}

double bbox_height(const cad::Entity& entity) {
    const BBox box = entity.bounding_box();
    return box.y1 - box.y0;
}

void scale_bbox(cad::Entity& entity, const std::optional<double>& width,
                const std::optional<double>& height) {
    const BBox box = entity.bounding_box();
    const double current_width = box.x1 - box.x0;
    const double current_height = box.y1 - box.y0;
    const double sx = width && current_width > 1e-9 ? *width / current_width : 1.0;
    const double sy = height && current_height > 1e-9 ? *height / current_height : 1.0;
    if (std::fabs(sx - 1.0) > 1e-12 || std::fabs(sy - 1.0) > 1e-12)
        entity.scale(sx, sy, box.x0, box.y0);
}

}  // namespace

QuickEditPopup::QuickEditPopup(cad::EntityPtr entity, ApplyCallback apply, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      entity_(std::move(entity)), apply_callback_(std::move(apply)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    build_fields();
    build_ui();
}

bool QuickEditPopup::supports(const cad::Entity& entity) {
    switch (entity.kind()) {
        case cad::EntityKind::Line:
        case cad::EntityKind::ConstructionLine:
        case cad::EntityKind::DrillPoint:
        case cad::EntityKind::Circle:
        case cad::EntityKind::Ellipse:
        case cad::EntityKind::Arc:
        case cad::EntityKind::Rectangle:
        case cad::EntityKind::Polyline:
        case cad::EntityKind::Text:
        case cad::EntityKind::Dimension:
            return true;
    }
    return false;
}

QString QuickEditPopup::entity_name(const cad::Entity& entity) {
    switch (entity.kind()) {
        case cad::EntityKind::ConstructionLine: return "linha de construção";
        case cad::EntityKind::DrillPoint: return "ponto de furação";
        case cad::EntityKind::Circle: return "círculo";
        case cad::EntityKind::Rectangle: return "retângulo";
        case cad::EntityKind::Line: return "linha";
        case cad::EntityKind::Ellipse: return "elipse";
        case cad::EntityKind::Arc: return "arco";
        case cad::EntityKind::Text: return "texto";
        case cad::EntityKind::Polyline: return "polilinha";
        case cad::EntityKind::Dimension: return "cota";
    }
    return "vetor";
}

void QuickEditPopup::build_fields() {
    const auto number = [this](QString key, QString label, QString suffix,
                               std::optional<double> minimum,
                               std::function<double(const cad::Entity&)> read,
                               std::function<void(cad::Entity&, double)> write) {
        Field field;
        field.key = std::move(key);
        field.label = std::move(label);
        field.suffix = std::move(suffix);
        field.minimum = minimum;
        field.read = [read = std::move(read)](const cad::Entity& entity) -> Value {
            return read(entity);
        };
        field.write = [write = std::move(write)](cad::Entity& entity, const Value& value) {
            write(entity, std::get<double>(value));
        };
        fields_.push_back(std::move(field));
    };
    const auto text = [this](QString key, QString label,
                             std::function<QString(const cad::Entity&)> read,
                             std::function<void(cad::Entity&, const QString&)> write) {
        Field field;
        field.key = std::move(key);
        field.label = std::move(label);
        field.suffix.clear();
        field.minimum.reset();
        field.text = true;
        field.read = [read = std::move(read)](const cad::Entity& entity) -> Value {
            return read(entity);
        };
        field.write = [write = std::move(write)](cad::Entity& entity, const Value& value) {
            write(entity, std::get<QString>(value));
        };
        fields_.push_back(std::move(field));
    };
    const auto boolean = [this](QString key, QString label,
                                std::function<bool(const cad::Entity&)> read,
                                std::function<void(cad::Entity&, bool)> write) {
        Field field;
        field.key = std::move(key);
        field.label = std::move(label);
        field.suffix.clear();
        field.minimum.reset();
        field.boolean = true;
        field.read = [read = std::move(read)](const cad::Entity& entity) -> Value {
            return read(entity);
        };
        field.write = [write = std::move(write)](cad::Entity& entity, const Value& value) {
            write(entity, std::get<bool>(value));
        };
        fields_.push_back(std::move(field));
    };

    if (!entity_) return;
    switch (entity_->kind()) {
        case cad::EntityKind::ConstructionLine:
            number("length", "Comprimento", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::ConstructionLine&>(e).length(); },
                   [](cad::Entity& e, double v) { set_line_length(static_cast<cad::ConstructionLine&>(e), v); });
            number("angle", "Ângulo", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::ConstructionLine&>(e).angle(); },
                   [](cad::Entity& e, double v) { set_line_angle(static_cast<cad::ConstructionLine&>(e), v); });
            boolean("machinable", "Usar em percursos",
                    [](const cad::Entity& e) { return static_cast<const cad::ConstructionLine&>(e).machinable; },
                    [](cad::Entity& e, bool v) { static_cast<cad::ConstructionLine&>(e).machinable = v; });
            break;
        case cad::EntityKind::DrillPoint:
            number("x", "Posição X", "mm", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::DrillPoint&>(e).x; },
                   [](cad::Entity& e, double v) { static_cast<cad::DrillPoint&>(e).x = v; });
            number("y", "Posição Y", "mm", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::DrillPoint&>(e).y; },
                   [](cad::Entity& e, double v) { static_cast<cad::DrillPoint&>(e).y = v; });
            break;
        case cad::EntityKind::Circle:
            number("diameter", "Diâmetro", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Circle&>(e).radius * 2.0; },
                   [](cad::Entity& e, double v) { static_cast<cad::Circle&>(e).radius = v / 2.0; });
            number("cx", "Centro X", "mm", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Circle&>(e).cx; },
                   [](cad::Entity& e, double v) { static_cast<cad::Circle&>(e).cx = v; });
            number("cy", "Centro Y", "mm", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Circle&>(e).cy; },
                   [](cad::Entity& e, double v) { static_cast<cad::Circle&>(e).cy = v; });
            break;
        case cad::EntityKind::Rectangle:
            number("width", "Largura", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Rectangle&>(e).width; },
                   [](cad::Entity& e, double v) { static_cast<cad::Rectangle&>(e).width = v; });
            number("height", "Altura", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Rectangle&>(e).height; },
                   [](cad::Entity& e, double v) { static_cast<cad::Rectangle&>(e).height = v; });
            number("corner_radius", "Raio do canto", "mm", 0.0,
                   [](const cad::Entity& e) { return static_cast<const cad::Rectangle&>(e).corner_radius; },
                   [](cad::Entity& e, double v) { static_cast<cad::Rectangle&>(e).corner_radius = v; });
            number("rotation", "Rotação", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Rectangle&>(e).rotation; },
                   [](cad::Entity& e, double v) { static_cast<cad::Rectangle&>(e).rotation = v; });
            break;
        case cad::EntityKind::Line:
            number("length", "Comprimento", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Line&>(e).length(); },
                   [](cad::Entity& e, double v) { set_line_length(static_cast<cad::Line&>(e), v); });
            number("angle", "Ângulo", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Line&>(e).angle(); },
                   [](cad::Entity& e, double v) { set_line_angle(static_cast<cad::Line&>(e), v); });
            break;
        case cad::EntityKind::Ellipse:
            number("width", "Largura", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Ellipse&>(e).rx * 2.0; },
                   [](cad::Entity& e, double v) { static_cast<cad::Ellipse&>(e).rx = v / 2.0; });
            number("height", "Altura", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Ellipse&>(e).ry * 2.0; },
                   [](cad::Entity& e, double v) { static_cast<cad::Ellipse&>(e).ry = v / 2.0; });
            number("rotation", "Rotação", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Ellipse&>(e).rotation; },
                   [](cad::Entity& e, double v) { static_cast<cad::Ellipse&>(e).rotation = v; });
            break;
        case cad::EntityKind::Arc:
            number("radius", "Raio", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Arc&>(e).radius; },
                   [](cad::Entity& e, double v) { static_cast<cad::Arc&>(e).radius = v; });
            number("start_angle", "Ângulo inicial", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Arc&>(e).start_angle; },
                   [](cad::Entity& e, double v) { static_cast<cad::Arc&>(e).start_angle = v; });
            number("end_angle", "Ângulo final", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Arc&>(e).end_angle; },
                   [](cad::Entity& e, double v) { static_cast<cad::Arc&>(e).end_angle = v; });
            break;
        case cad::EntityKind::Text:
            text("content", "Texto",
                 [](const cad::Entity& e) { return QString::fromStdString(static_cast<const cad::Text&>(e).content); },
                 [](cad::Entity& e, const QString& v) {
                     auto& text_entity = static_cast<cad::Text&>(e);
                     text_entity.content = v.toStdString();
                     text_entity.invalidate();
                 });
            number("width", "Largura", "mm", 0.001, bbox_width,
                   [](cad::Entity& e, double v) { scale_bbox(e, v, std::nullopt); });
            number("height", "Altura", "mm", 0.001, bbox_height,
                   [](cad::Entity& e, double v) { scale_bbox(e, std::nullopt, v); });
            number("rotation", "Rotação", "°", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Text&>(e).rotation; },
                   [](cad::Entity& e, double v) {
                       auto& text_entity = static_cast<cad::Text&>(e);
                       text_entity.rotation = v;
                       text_entity.invalidate();
                   });
            break;
        case cad::EntityKind::Polyline:
            number("width", "Largura", "mm", 0.001, bbox_width,
                   [](cad::Entity& e, double v) { scale_bbox(e, v, std::nullopt); });
            number("height", "Altura", "mm", 0.001, bbox_height,
                   [](cad::Entity& e, double v) { scale_bbox(e, std::nullopt, v); });
            break;
        case cad::EntityKind::Dimension:
            number("text_height", "Altura do número", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Dimension&>(e).style.text_height; },
                   [](cad::Entity& e, double v) { static_cast<cad::Dimension&>(e).style.text_height = v; });
            number("arrow_size", "Tamanho da seta", "mm", 0.001,
                   [](const cad::Entity& e) { return static_cast<const cad::Dimension&>(e).style.arrow_size; },
                   [](cad::Entity& e, double v) { static_cast<cad::Dimension&>(e).style.arrow_size = v; });
            number("offset", "Afastamento", "mm", std::nullopt,
                   [](const cad::Entity& e) { return static_cast<const cad::Dimension&>(e).offset; },
                   [](cad::Entity& e, double v) { static_cast<cad::Dimension&>(e).offset = v; });
            number("precision", "Casas decimais", "", 0.0,
                   [](const cad::Entity& e) { return static_cast<double>(static_cast<const cad::Dimension&>(e).style.precision); },
                   [](cad::Entity& e, double v) {
                       static_cast<cad::Dimension&>(e).style.precision = static_cast<int>(std::lround(v));
                   });
            break;
    }
}

void QuickEditPopup::build_ui() {
    QStringList keys;
    for (const Field& field : fields_) keys.push_back(field.key);
    for (const auto& pair : {std::pair<QString, QString>{"width", "height"}}) {
        const int first = keys.indexOf(pair.first);
        const int second = keys.indexOf(pair.second);
        if (first >= 0 && second >= 0) {
            dimension_pair_ = std::pair<int, int>{first, second};
            const double a = std::get<double>(fields_[static_cast<size_t>(first)].read(*entity_));
            const double b = std::get<double>(fields_[static_cast<size_t>(second)].read(*entity_));
            dimension_ratio_ = std::fabs(a) > 1e-12 ? b / a : 1.0;
            break;
        }
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 20, 20, 20);
    auto* card = new QFrame(this);
    card->setObjectName("QuickEditCard");
    card->setStyleSheet(R"(
        #QuickEditCard { background-color: #1f1f1f; border: 1px solid #0078d4; border-radius: 8px; }
        #QuickEditTitle { color: #75beff; font-size: 10px; font-weight: 600; }
        #QuickEditHint { color: #858585; font-size: 9px; }
        #QuickEditLabel { color: #cccccc; font-size: 11px; }
        #QuickEditUnit { color: #858585; font-size: 10px; }
        QLineEdit { background-color: #252526; color: #cccccc; border: 1px solid #3c3c3c;
                    border-radius: 5px; padding: 4px 7px; selection-background-color: #04395e; }
        QLineEdit:focus { border-color: #0078d4; background-color: #2a2a2a; }
    )");
    auto* glow = new QGraphicsDropShadowEffect(card);
    glow->setBlurRadius(36);
    glow->setOffset(0, 0);
    glow->setColor(QColor("#0078d4"));
    card->setGraphicsEffect(glow);
    outer->addWidget(card);

    auto* grid = new QGridLayout(card);
    grid->setContentsMargins(13, 10, 13, 11);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    auto* title = new QLabel(entity_name(*entity_).toUpper(), card);
    title->setObjectName("QuickEditTitle");
    grid->addWidget(title, 0, 0, 1, dimension_pair_ ? 2 : 3);

    if (dimension_pair_) {
        proportion_lock_ = new QToolButton(card);
        proportion_lock_->setCheckable(true);
        proportion_lock_->setChecked(true);
        proportion_lock_->setIcon(get_icon("lock"));
        proportion_lock_->setIconSize(QSize(16, 16));
        connect(proportion_lock_, &QToolButton::toggled, this, &QuickEditPopup::update_proportion_lock);
        grid->addWidget(proportion_lock_, 0, 2, Qt::AlignRight);
        update_proportion_lock(true);
    }

    for (size_t index = 0; index < fields_.size(); ++index) {
        const Field& field = fields_[index];
        const int row = static_cast<int>(index) + 1;
        auto* label = new QLabel(field.label, card);
        label->setObjectName("QuickEditLabel");
        grid->addWidget(label, row, 0);
        QWidget* editor = nullptr;
        if (field.boolean) {
            auto* checkbox = new QCheckBox(card);
            checkbox->setChecked(std::get<bool>(field.read(*entity_)));
            checkbox->setToolTip("Quando marcado, esta linha pode participar de percursos CAM.");
            connect(checkbox, &QCheckBox::clicked, this, [this] { apply(); });
            editor = checkbox;
        } else {
            auto* line_edit = new QLineEdit(card);
            const Value value = field.read(*entity_);
            line_edit->setText(field.text ? std::get<QString>(value) : format_value(std::get<double>(value)));
            line_edit->setFixedWidth(field.text ? 132 : 84);
            if (!field.text) line_edit->setAlignment(Qt::AlignRight);
            connect(line_edit, &QLineEdit::returnPressed, this, &QuickEditPopup::apply);
            editor = line_edit;
        }
        editor->setObjectName(QString("QuickEdit_%1").arg(field.key));
        grid->addWidget(editor, row, 1);
        auto* unit = new QLabel(field.suffix, card);
        unit->setObjectName("QuickEditUnit");
        grid->addWidget(unit, row, 2);
        editors_.push_back(editor);
    }

    if (dimension_pair_) {
        for (const int index : {dimension_pair_->first, dimension_pair_->second}) {
            if (auto* editor = qobject_cast<QLineEdit*>(editors_[static_cast<size_t>(index)])) {
                connect(editor, &QLineEdit::textEdited, this,
                        [this, index](const QString& value) { sync_proportion(index, value); });
            }
        }
    }
    hint_ = new QLabel("Enter aplica · Esc cancela", card);
    hint_->setObjectName("QuickEditHint");
    grid->addWidget(hint_, static_cast<int>(fields_.size()) + 1, 0, 1, 3);
    adjustSize();
}

void QuickEditPopup::apply() {
    if (!entity_) return;
    std::vector<std::pair<Field, Value>> changes;
    QStringList summary;
    for (size_t index = 0; index < fields_.size(); ++index) {
        const Field& field = fields_[index];
        const Value current = field.read(*entity_);
        Value value;
        if (field.boolean) {
            value = qobject_cast<QCheckBox*>(editors_[index])->isChecked();
            if (std::get<bool>(value) == std::get<bool>(current)) continue;
        } else {
            const QString text = qobject_cast<QLineEdit*>(editors_[index])->text().trimmed();
            if (field.text) {
                value = text;
                if (std::get<QString>(value) == std::get<QString>(current)) continue;
            } else {
                if (text.isEmpty()) continue;
                const auto parsed = parse_number(text);
                if (!parsed) {
                    show_error(QString("%1: ‘%2’ não é um número").arg(field.label, text));
                    return;
                }
                if (field.minimum && *parsed < *field.minimum) {
                    show_error(QString("%1 precisa ser maior ou igual a %2")
                                   .arg(field.label, format_value(*field.minimum)));
                    return;
                }
                value = *parsed;
                if (std::fabs(std::get<double>(value) - std::get<double>(current)) <= 1e-9) continue;
            }
        }
        const QString shown = field.text ? std::get<QString>(value)
                              : field.boolean ? (std::get<bool>(value) ? "Sim" : "Não")
                                              : format_value(std::get<double>(value));
        summary.push_back(QString("%1 %2%3").arg(field.label, shown, field.suffix));
        changes.emplace_back(field, value);
    }
    if (!changes.empty() && apply_callback_) {
        apply_callback_([changes = std::move(changes)](cad::Entity& target) {
            for (const auto& [field, value] : changes) field.write(target, value);
        }, summary.join(", "));
    }
    close();
}

void QuickEditPopup::sync_proportion(int source, const QString& text) {
    if (!dimension_pair_ || !proportion_lock_ || !proportion_lock_->isChecked()) return;
    const auto value = parse_number(text);
    if (!value) return;
    const int first = dimension_pair_->first;
    const int second = dimension_pair_->second;
    int target = first;
    double linked = *value;
    if (source == first) {
        target = second;
        linked = *value * dimension_ratio_;
    } else if (std::fabs(dimension_ratio_) > 1e-12) {
        linked = *value / dimension_ratio_;
    }
    if (auto* editor = qobject_cast<QLineEdit*>(editors_[static_cast<size_t>(target)]))
        editor->setText(format_value(linked));
}

void QuickEditPopup::update_proportion_lock(bool locked) {
    if (!proportion_lock_) return;
    proportion_lock_->setIcon(get_icon(locked ? "lock" : "lock_open"));
    proportion_lock_->setToolTip(
        locked ? "Proporção travada. Clique para liberar largura e altura."
               : "Proporção livre. Clique para manter largura e altura proporcionais.");
}

void QuickEditPopup::show_error(const QString& message) {
    if (!hint_) return;
    hint_->setText(message);
    hint_->setStyleSheet("color: #ff8f6b; font-size: 9px;");
}

void QuickEditPopup::show_at(const QPoint& global_position) {
    adjustSize();
    QPoint target(global_position.x() + 14, global_position.y() - height() / 2);
    QScreen* screen = QGuiApplication::screenAt(global_position);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry();
        if (target.x() + width() > area.right()) target.setX(global_position.x() - 14 - width());
        target.setX(std::max(area.left(), target.x()));
        target.setY(std::clamp(target.y(), area.top(), area.bottom() - height()));
    }
    move(target);
    show();
    if (!editors_.empty()) {
        editors_.front()->setFocus();
        if (auto* edit = qobject_cast<QLineEdit*>(editors_.front())) edit->selectAll();
    }
}

void QuickEditPopup::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace cnc::ui
