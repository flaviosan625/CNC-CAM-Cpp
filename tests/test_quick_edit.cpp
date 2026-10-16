#include <doctest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QToolButton>

#include "cad/entities.hpp"
#include "ui/layer_manager_dialog.hpp"
#include "ui/quick_edit.hpp"

namespace {

template <typename Entity>
auto popup_for(const std::shared_ptr<Entity>& entity, bool& applied) {
    return new cnc::ui::QuickEditPopup(
        entity,
        [&applied, entity](cnc::ui::QuickEditPopup::Transform transform, const QString&) {
            transform(*entity);
            applied = true;
        });
}

}  // namespace

TEST_CASE("edicao rapida aceita virgula decimal no diametro") {
    auto circle = std::make_shared<cnc::cad::Circle>();
    circle->radius = 25.0;
    bool applied = false;
    auto* popup = popup_for(circle, applied);

    auto* diameter = popup->findChild<QLineEdit*>("QuickEdit_diameter");
    REQUIRE(diameter != nullptr);
    diameter->setText("80,5");
    QMetaObject::invokeMethod(diameter, "returnPressed", Qt::DirectConnection);

    CHECK(applied);
    CHECK(circle->radius == doctest::Approx(40.25));
}

TEST_CASE("edicao rapida mantem largura e altura proporcionais") {
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->width = 100.0;
    rectangle->height = 50.0;
    bool applied = false;
    auto* popup = popup_for(rectangle, applied);

    auto* width = popup->findChild<QLineEdit*>("QuickEdit_width");
    auto* height = popup->findChild<QLineEdit*>("QuickEdit_height");
    REQUIRE(width != nullptr);
    REQUIRE(height != nullptr);
    width->setText("200");
    QMetaObject::invokeMethod(width, "textEdited", Qt::DirectConnection,
                              Q_ARG(QString, QString("200")));
    CHECK(height->text().toStdString() == "100");
    QMetaObject::invokeMethod(width, "returnPressed", Qt::DirectConnection);

    CHECK(applied);
    CHECK(rectangle->width == doctest::Approx(200.0));
    CHECK(rectangle->height == doctest::Approx(100.0));
}

TEST_CASE("linha de construcao pode participar dos percursos") {
    auto line = std::make_shared<cnc::cad::ConstructionLine>();
    bool applied = false;
    auto* popup = popup_for(line, applied);

    auto* machinable = popup->findChild<QCheckBox*>("QuickEdit_machinable");
    REQUIRE(machinable != nullptr);
    machinable->click();

    CHECK(applied);
    CHECK(line->machinable);
}

TEST_CASE("gerenciador de camadas preserva cor visibilidade e bloqueio") {
    std::vector<cnc::cad::Layer> layers(2);
    layers[0].name = "Camada 1";
    layers[1].name = "Acabamento";
    layers[1].color = {0.2, 0.4, 0.8};

    cnc::ui::LayerManagerDialog dialog(layers, "Acabamento");
    auto* list = dialog.findChild<QListWidget*>("LayerManagerList");
    auto* locked = dialog.findChild<QCheckBox*>("LayerManagerLocked");
    REQUIRE(list != nullptr);
    REQUIRE(locked != nullptr);
    REQUIRE(list->count() == 2);
    REQUIRE(list->currentItem() != nullptr);
    CHECK(list->currentItem()->text().toStdString() == "Acabamento");

    list->currentItem()->setCheckState(Qt::Unchecked);
    locked->setChecked(true);
    const auto result = dialog.result_layers();
    REQUIRE(result.size() == 2);
    CHECK_FALSE(result[1].visible);
    CHECK(result[1].locked);
    CHECK(result[1].color[0] == doctest::Approx(0.2));
    CHECK(result[1].color[1] == doctest::Approx(0.4));
    CHECK(result[1].color[2] == doctest::Approx(0.8));
}
