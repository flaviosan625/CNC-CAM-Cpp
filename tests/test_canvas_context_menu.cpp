#include <doctest.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>

#include "app/main_window.hpp"
#include "viewport/canvas_2d.hpp"

TEST_CASE("canvas: clique direito abre o menu contextual com estados coerentes") {
    cnc::app::MainWindow window;
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(canvas != nullptr);
    window.show();
    QApplication::processEvents();

    const QPoint local(canvas->viewport()->width() / 2, canvas->viewport()->height() / 2);
    QContextMenuEvent context(QContextMenuEvent::Mouse, local,
                              canvas->viewport()->mapToGlobal(local));
    QApplication::sendEvent(canvas->viewport(), &context);

    auto* menu = window.findChild<QMenu*>("CanvasContextMenu");
    REQUIRE(menu != nullptr);
    CHECK(menu->isVisible());
    const auto find_action = [menu](const QString& text) -> QAction* {
        for (QAction* action : menu->actions())
            if (action->text() == text) return action;
        return nullptr;
    };
    REQUIRE(find_action("Cortar") != nullptr);
    CHECK_FALSE(find_action("Cortar")->isEnabled());
    REQUIRE(find_action("Colar") != nullptr);
    CHECK_FALSE(find_action("Colar")->isEnabled());
    REQUIRE(find_action("Selecionar todos os vetores") != nullptr);
    CHECK(find_action("Selecionar todos os vetores")->isEnabled());
    REQUIRE(find_action("Propriedades do objeto...") != nullptr);
    CHECK_FALSE(find_action("Propriedades do objeto...")->isEnabled());
    menu->close();
    QApplication::processEvents();
}
