#include <doctest.h>

#include <cmath>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include "app/main_window.hpp"
#include "viewport/canvas_2d.hpp"

TEST_CASE("pan: ativacao da ferramenta mao e navegacao livre em todas as direcoes") {
    cnc::app::MainWindow window;
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(canvas != nullptr);
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();

    // 1. Ativacao da ferramenta Pan ("Mao")
    window.activate_tool("pan");
    QApplication::processEvents();

    CHECK(canvas->is_pan_active());
    REQUIRE(canvas->tool() != nullptr);
    CHECK(canvas->tool()->name() == "pan");
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);

    // 2. Arraste com botao esquerdo move a vista livremente
    const QPoint start_pt(300, 300);
    const QPointF scene_center_start = canvas->mapToScene(canvas->viewport()->rect().center());

    // Pressiona o botao esquerdo
    QMouseEvent press_event(QEvent::MouseButtonPress, start_pt, canvas->viewport()->mapToGlobal(start_pt),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &press_event);
    CHECK(canvas->cursor().shape() == Qt::ClosedHandCursor);

    // Arraste para a direita e para baixo (+50px, +40px)
    const QPoint move_pt1(350, 340);
    QMouseEvent move_event1(QEvent::MouseMove, move_pt1, canvas->viewport()->mapToGlobal(move_pt1),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move_event1);

    const QPointF scene_center_after_drag1 = canvas->mapToScene(canvas->viewport()->rect().center());
    // Ao arrastar para a direita/baixo na tela, o centro da cena deve se deslocar
    CHECK(scene_center_after_drag1 != scene_center_start);

    // Arraste para a esquerda e para cima (movimento em todas as direcoes)
    const QPoint move_pt2(200, 200);
    QMouseEvent move_event2(QEvent::MouseMove, move_pt2, canvas->viewport()->mapToGlobal(move_pt2),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move_event2);

    const QPointF scene_center_after_drag2 = canvas->mapToScene(canvas->viewport()->rect().center());
    CHECK(scene_center_after_drag2 != scene_center_after_drag1);

    // Solta o botao esquerdo -> cursor volta a ser mao aberta
    QMouseEvent release_event(QEvent::MouseButtonRelease, move_pt2, canvas->viewport()->mapToGlobal(move_pt2),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &release_event);
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);

    // 3. Panning com botao do meio a partir da ferramenta selecao
    window.activate_tool("select");
    QApplication::processEvents();
    CHECK_FALSE(canvas->is_pan_active());

    const QPointF center_before_middle = canvas->mapToScene(canvas->viewport()->rect().center());
    QMouseEvent middle_press(QEvent::MouseButtonPress, start_pt, canvas->viewport()->mapToGlobal(start_pt),
                             Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &middle_press);
    CHECK(canvas->cursor().shape() == Qt::ClosedHandCursor);

    QMouseEvent middle_move(QEvent::MouseMove, move_pt1, canvas->viewport()->mapToGlobal(move_pt1),
                            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &middle_move);

    const QPointF center_after_middle = canvas->mapToScene(canvas->viewport()->rect().center());
    CHECK(center_after_middle != center_before_middle);

    QMouseEvent middle_release(QEvent::MouseButtonRelease, move_pt1, canvas->viewport()->mapToGlobal(move_pt1),
                               Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &middle_release);
    CHECK(canvas->cursor().shape() == Qt::ArrowCursor);

    // 4. Barra de espaco para pan temporario a partir de qualquer ferramenta
    QKeyEvent space_press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(canvas, &space_press);
    CHECK(canvas->is_pan_active());
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);

    QKeyEvent space_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(canvas, &space_release);
    CHECK_FALSE(canvas->is_pan_active());
    CHECK(canvas->cursor().shape() == Qt::ArrowCursor);

    // 5. Movimento infinito e desimpedido mesmo com zoom reduzido (zoom out)
    window.activate_tool("pan");
    canvas->zoom_by(0.1); // zoom out forte
    QApplication::processEvents();

    const QPointF center_zoomed_start = canvas->mapToScene(canvas->viewport()->rect().center());
    QMouseEvent zoom_press(QEvent::MouseButtonPress, start_pt, canvas->viewport()->mapToGlobal(start_pt),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &zoom_press);

    QMouseEvent zoom_move(QEvent::MouseMove, move_pt1, canvas->viewport()->mapToGlobal(move_pt1),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &zoom_move);

    const QPointF center_zoomed_after = canvas->mapToScene(canvas->viewport()->rect().center());
    // Nao pode ficar travado nem restrito mesmo com zoom out
    CHECK(center_zoomed_after != center_zoomed_start);

    QMouseEvent zoom_release(QEvent::MouseButtonRelease, move_pt1, canvas->viewport()->mapToGlobal(move_pt1),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &zoom_release);
}

