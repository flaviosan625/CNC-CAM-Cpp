#include <doctest.h>

#include <memory>

#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QTest>

#include "app/main_window.hpp"
#include "cad/entities.hpp"
#include "viewport/canvas_2d.hpp"

TEST_CASE("canvas: Ctrl+C e Ctrl+V copiam vetores selecionados e permitem desfazer") {
    cnc::app::MainWindow window;
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(canvas != nullptr);
    auto& document = window.document();
    document.add_layer("Origem");
    auto original = std::make_shared<cnc::cad::Rectangle>();
    original->x = 20;
    original->y = 30;
    original->width = 40;
    original->height = 50;
    original->layer = "Origem";
    document.add_entity(original);
    canvas->refresh_entities();
    document.set_selected(original, true);
    canvas->refresh_entities();
    window.show();
    QApplication::processEvents();
    canvas->setFocus();

    // Copiar precisa funcionar sem abrir o menu de clique direito, inclusive
    // com uma ferramenta que usa a letra C sem Ctrl.
    window.activate_tool("polyline");
    QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier);
    CHECK(document.entities.size() == 1);
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier);
    REQUIRE(document.entities.size() == 2);
    auto pasted = std::dynamic_pointer_cast<cnc::cad::Rectangle>(document.entities.back());
    REQUIRE(pasted != nullptr);
    CHECK(pasted->id != original->id);
    CHECK(pasted->x == doctest::Approx(30));
    CHECK(pasted->y == doctest::Approx(20));
    CHECK(pasted->layer == "Origem");
    CHECK(pasted->selected);
    CHECK_FALSE(original->selected);

    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    CHECK(document.entities.size() == 1);
    CHECK(document.entities.front() == original);
}

TEST_CASE("canvas: Ctrl+X recorta e campos de texto mantem o clipboard normal") {
    cnc::app::MainWindow window;
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(canvas != nullptr);
    auto& document = window.document();
    auto original = std::make_shared<cnc::cad::Rectangle>();
    document.add_entity(original);
    document.set_selected(original, true);
    canvas->refresh_entities();
    window.show();
    QApplication::processEvents();
    canvas->setFocus();

    QTest::keyClick(canvas, Qt::Key_X, Qt::ControlModifier);
    CHECK(document.entities.empty());
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier);
    CHECK(document.entities.size() == 1);

    QLineEdit field(&window);
    field.show();
    field.setFocus();
    QApplication::clipboard()->setText("texto");
    QTest::keyClick(&field, Qt::Key_V, Qt::ControlModifier);
    CHECK(field.text() == "texto");
    CHECK(document.entities.size() == 1);
    QApplication::clipboard()->clear();

    // O atalho tambem funciona se o foco ficou em um controle do painel.
    QPushButton control(&window);
    control.show();
    control.setFocus();
    QTest::keyClick(&control, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&control, Qt::Key_V, Qt::ControlModifier);
    CHECK(document.entities.size() == 2);
}

TEST_CASE("ferramentas: separar texto em linhas executa a ferramenta registrada") {
    cnc::app::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto& document = window.document();
    auto block = std::make_shared<cnc::cad::Text>();
    block->content = "Primeira\nSegunda";
    block->x = 10;
    block->y = 100;
    document.add_entity(block);
    document.set_selected(block, true);
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(canvas != nullptr);
    canvas->refresh_entities();

    window.activate_tool("text_lines");
    REQUIRE(document.entities.size() == 2);
    auto first = std::dynamic_pointer_cast<cnc::cad::Text>(document.entities[0]);
    auto second = std::dynamic_pointer_cast<cnc::cad::Text>(document.entities[1]);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->content == "Primeira");
    CHECK(second->content == "Segunda");
    CHECK(second->y < first->y);
}
