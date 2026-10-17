#include <doctest.h>

#include "cad/geometry.hpp"
#include "cad/relations.hpp"
#include "tool_fixture.hpp"
#include "tools/draw_tools.hpp"
#include "tools/edit_tools.hpp"
#include "ui/contextual_hud.hpp"

namespace cnc::testing {

TEST_CASE("solidworks: intersecao de segmentos geometricos") {
    Pt a{0, 5}, b{10, 5};
    Pt c{5, 0}, d{5, 10};
    auto inter = cad::segment_intersection(a, b, c, d);
    REQUIRE(inter.has_value());
    CHECK(inter->x == doctest::Approx(5.0));
    CHECK(inter->y == doctest::Approx(5.0));

    // Nao se cruzam
    Pt e{20, 0}, f{20, 10};
    CHECK_FALSE(cad::segment_intersection(a, b, e, f).has_value());
}

TEST_CASE("solidworks: autotransitioning na polilinha com tecla A") {
    ToolFixture fix;
    tools::PolylineTool tool(fix.ctx);
    tool.activate();

    // Primeiro segmento de reta de (0,0) ate (10,0)
    tool.on_press(0, 0, LEFT, NONE);
    tool.on_move(10, 0, NONE);
    tool.on_press(10, 0, LEFT, NONE);

    // Tecla 'A' alterna para modo de arco tangente
    CHECK(tool.on_key(Qt::Key_A));
    CHECK(tool.is_arc_mode());

    // Move o mouse para (20, 10) - deve gerar arco tangente em vez de reta
    tool.on_move(20, 10, NONE);
    CHECK(fix.canvas.preview.size() > 2); // Arco com multiplos pontos

    // Clica para posicionar o arco tangente: volta automaticamente para reta conforme SolidWorks
    tool.on_press(20, 10, LEFT, NONE);
    CHECK_FALSE(tool.is_arc_mode());
}

TEST_CASE("solidworks: aparagem inteligente (Power Trim) por rastro continuo") {
    ToolFixture fix;
    // Duas linhas verticais em x=10 e x=20 cruzadas por uma linha horizontal em y=15
    auto l_horiz = line(0, 15, 30, 15);
    auto l_vert1 = line(10, 0, 10, 30);
    auto l_vert2 = line(20, 0, 20, 30);
    fix.doc.add_entity(l_horiz);
    fix.doc.add_entity(l_vert1);
    fix.doc.add_entity(l_vert2);

    tools::InteractiveTrimTool trim(fix.ctx);
    trim.activate();

    // Rastro de corte arrastando de (15, 5) ate (15, 25), cortando a linha horizontal entre as verticais
    trim.on_press(15, 5, LEFT, NONE);
    trim.on_move(15, 25, NONE);
    trim.on_release(15, 25, LEFT, NONE);

    // O trecho do meio da linha horizontal deve ter sido aparado
    CHECK(fix.doc.entities.size() >= 3);
}

TEST_CASE("solidworks: estender inteligente em 1 clique (Smart Extend)") {
    ToolFixture fix;
    // Linha horizontal de (0, 10) ate (20, 10)
    auto l1 = line(0, 10, 20, 10);
    // Linha vertical limite em x=50, de (50, 0) ate (50, 40)
    auto boundary = line(50, 0, 50, 40);
    fix.doc.add_entity(l1);
    fix.doc.add_entity(boundary);

    tools::ExtendTool ext(fix.ctx);
    ext.activate();

    // Passar o mouse perto da ponta (20, 10) deve encontrar candidato ate (50, 10)
    auto cand = ext.find_candidate(20.0, 10.0);
    REQUIRE(cand.has_value());
    CHECK(cand->line == l1);
    CHECK(cand->endpoint == "end");
    CHECK(cand->target_pt.x == doctest::Approx(50.0));
    CHECK(cand->target_pt.y == doctest::Approx(10.0));

    // Um unico clique na ponta estende a linha ate o limite
    ext.on_press(20.0, 10.0, LEFT, NONE);
    CHECK(l1->x2 == doctest::Approx(50.0));
    CHECK(l1->y2 == doctest::Approx(10.0));
}

TEST_CASE("solidworks: entrada numerica direta no circulo (raio, diametro, backspace e esc)") {
    ToolFixture fix;
    tools::CircleTool circle_tool(fix.ctx);
    circle_tool.activate();

    // 1. Entrada de raio: arrasta e digita "25" -> Enter -> cria círculo R=25 em (10, 10)
    circle_tool.on_press(10, 10, LEFT, NONE);
    circle_tool.on_move(15, 10, NONE);
    circle_tool.on_key(Qt::Key_2);
    circle_tool.on_key(Qt::Key_5);
    CHECK(fix.canvas.hud_text.find("25") != std::string::npos);
    CHECK(fix.canvas.hud_text.find("Raio") != std::string::npos);
    circle_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 1);
    auto c1 = std::dynamic_pointer_cast<cad::Circle>(fix.doc.entities.back());
    REQUIRE(c1);
    CHECK(c1->cx == doctest::Approx(10.0));
    CHECK(c1->cy == doctest::Approx(10.0));
    CHECK(c1->radius == doctest::Approx(25.0));

    // 2. Entrada de diâmetro com 'D': arrasta e digita "D50" -> Enter -> cria círculo R=25 em (0, 0)
    circle_tool.on_press(0, 0, LEFT, NONE);
    circle_tool.on_move(5, 0, NONE);
    circle_tool.on_key(Qt::Key_D);
    circle_tool.on_key(Qt::Key_5);
    circle_tool.on_key(Qt::Key_0);
    CHECK(fix.canvas.hud_text.find("50") != std::string::npos);
    CHECK(fix.canvas.hud_text.find("Diâmetro") != std::string::npos);
    circle_tool.on_key(Qt::Key_Enter);

    REQUIRE(fix.doc.entities.size() == 2);
    auto c2 = std::dynamic_pointer_cast<cad::Circle>(fix.doc.entities.back());
    REQUIRE(c2);
    CHECK(c2->cx == doctest::Approx(0.0));
    CHECK(c2->cy == doctest::Approx(0.0));
    CHECK(c2->radius == doctest::Approx(25.0));

    // 3. Backspace: digita "30", backspace, digita "5" -> R=35
    circle_tool.on_press(0, 0, LEFT, NONE);
    circle_tool.on_move(5, 0, NONE);
    circle_tool.on_key(Qt::Key_3);
    circle_tool.on_key(Qt::Key_0);
    circle_tool.on_key(Qt::Key_Backspace);
    circle_tool.on_key(Qt::Key_5);
    circle_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 3);
    auto c3 = std::dynamic_pointer_cast<cad::Circle>(fix.doc.entities.back());
    REQUIRE(c3);
    CHECK(c3->radius == doctest::Approx(35.0));

    // 4. Escape: cancela apenas a entrada numérica, mantendo o arraste
    circle_tool.on_press(0, 0, LEFT, NONE);
    circle_tool.on_move(5, 0, NONE);
    circle_tool.on_key(Qt::Key_4);
    circle_tool.on_key(Qt::Key_0);
    CHECK(!fix.canvas.hud_text.empty());
    circle_tool.on_key(Qt::Key_Escape);
    CHECK(fix.canvas.hud_text.empty());
    CHECK(circle_tool.is_dragging());
    // Segundo Escape cancela a criação da entidade
    circle_tool.on_key(Qt::Key_Escape);
    CHECK_FALSE(circle_tool.is_dragging());
}

TEST_CASE("solidworks: entrada numerica direta no retangulo (largura, tab, altura e quadrante)") {
    ToolFixture fix;
    tools::RectangleTool rect_tool(fix.ctx);
    rect_tool.activate();

    // 1. Largura 80, Tab, Altura 40 -> Enter
    rect_tool.on_press(10, 20, LEFT, NONE);
    rect_tool.on_move(15, 25, NONE);
    rect_tool.on_key(Qt::Key_8);
    rect_tool.on_key(Qt::Key_0);
    CHECK(fix.canvas.hud_text.find("Largura: [80]") != std::string::npos);
    rect_tool.on_key(Qt::Key_Tab);
    rect_tool.on_key(Qt::Key_4);
    rect_tool.on_key(Qt::Key_0);
    CHECK(fix.canvas.hud_text.find("Altura: [40]") != std::string::npos);
    rect_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 1);
    auto r1 = std::dynamic_pointer_cast<cad::Rectangle>(fix.doc.entities.back());
    REQUIRE(r1);
    CHECK(r1->x == doctest::Approx(10.0));
    CHECK(r1->y == doctest::Approx(20.0));
    CHECK(r1->width == doctest::Approx(80.0));
    CHECK(r1->height == doctest::Approx(40.0));

    // 2. Arraste em quadrante negativo (-X, -Y)
    rect_tool.on_press(100, 100, LEFT, NONE);
    rect_tool.on_move(50, 50, NONE);
    rect_tool.on_key(Qt::Key_6);
    rect_tool.on_key(Qt::Key_0);
    rect_tool.on_key(Qt::Key_Tab);
    rect_tool.on_key(Qt::Key_3);
    rect_tool.on_key(Qt::Key_0);
    rect_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 2);
    auto r2 = std::dynamic_pointer_cast<cad::Rectangle>(fix.doc.entities.back());
    REQUIRE(r2);
    CHECK(r2->x == doctest::Approx(40.0));
    CHECK(r2->y == doctest::Approx(70.0));
    CHECK(r2->width == doctest::Approx(60.0));
    CHECK(r2->height == doctest::Approx(30.0));

    // 3. Escape cancela entrada numérica
    rect_tool.on_press(0, 0, LEFT, NONE);
    rect_tool.on_move(10, 10, NONE);
    rect_tool.on_key(Qt::Key_5);
    rect_tool.on_key(Qt::Key_0);
    CHECK(!fix.canvas.hud_text.empty());
    rect_tool.on_key(Qt::Key_Escape);
    CHECK(fix.canvas.hud_text.empty());
    CHECK(rect_tool.is_dragging());
    rect_tool.on_key(Qt::Key_Escape);
    CHECK_FALSE(rect_tool.is_dragging());
}

TEST_CASE("solidworks: entrada numerica direta na linha (comprimento exato, direcao e esc)") {
    ToolFixture fix;
    tools::LineTool line_tool(fix.ctx);
    line_tool.activate();

    // 1. Linha horizontal (+X) com comprimento 75
    line_tool.on_press(0, 0, LEFT, NONE);
    line_tool.on_move(10, 0, NONE);
    line_tool.on_key(Qt::Key_7);
    line_tool.on_key(Qt::Key_5);
    CHECK(fix.canvas.hud_text.find("75") != std::string::npos);
    line_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 1);
    auto l1 = std::dynamic_pointer_cast<cad::Line>(fix.doc.entities.back());
    REQUIRE(l1);
    CHECK(l1->x1 == doctest::Approx(0.0));
    CHECK(l1->y1 == doctest::Approx(0.0));
    CHECK(l1->x2 == doctest::Approx(75.0));
    CHECK(l1->y2 == doctest::Approx(0.0));

    // 2. Linha inclinada na direção (3, 4) com comprimento 50 -> ponto final (10+30, 10+40) = (40, 50)
    line_tool.on_press(10, 10, LEFT, NONE);
    line_tool.on_move(13, 14, NONE);
    line_tool.on_key(Qt::Key_5);
    line_tool.on_key(Qt::Key_0);
    line_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 2);
    auto l2 = std::dynamic_pointer_cast<cad::Line>(fix.doc.entities.back());
    REQUIRE(l2);
    CHECK(l2->x1 == doctest::Approx(10.0));
    CHECK(l2->y1 == doctest::Approx(10.0));
    CHECK(l2->x2 == doctest::Approx(40.0));
    CHECK(l2->y2 == doctest::Approx(50.0));

    // 3. Backspace e Escape
    line_tool.on_press(0, 0, LEFT, NONE);
    line_tool.on_move(10, 0, NONE);
    line_tool.on_key(Qt::Key_5);
    line_tool.on_key(Qt::Key_0);
    line_tool.on_key(Qt::Key_Backspace);
    line_tool.on_key(Qt::Key_2);
    CHECK(fix.canvas.hud_text.find("52") != std::string::npos);
    line_tool.on_key(Qt::Key_Escape);
    CHECK(fix.canvas.hud_text.empty());
}

TEST_CASE("solidworks: relacoes geometricas - concentrico e igualar tamanho com undo") {
    ToolFixture fix;
    auto c1 = circle(10, 20, 15);
    auto c2 = circle(80, 90, 30);
    fix.doc.add_entity(c1);
    fix.doc.add_entity(c2);

    // Concêntrico: alinha o centro de c2 ao centro da primária c1
    cad::apply_concentric_command(fix.doc, &fix.stack, {c1, c2});
    CHECK(c2->cx == doctest::Approx(10.0));
    CHECK(c2->cy == doctest::Approx(20.0));
    CHECK(c1->cx == doctest::Approx(10.0));
    CHECK(c1->cy == doctest::Approx(20.0));

    // Undo restaura a posição original
    fix.stack.undo();
    CHECK(c2->cx == doctest::Approx(80.0));
    CHECK(c2->cy == doctest::Approx(90.0));

    // Redo reaplica
    fix.stack.redo();
    CHECK(c2->cx == doctest::Approx(10.0));
    CHECK(c2->cy == doctest::Approx(20.0));

    // Igualar tamanho: círculo c3 (R=35) com círculo c4 (R=10) e retângulo r1 (100x80)
    auto c3 = circle(0, 0, 35);
    auto c4 = circle(50, 50, 10);
    auto r1 = rect(10, 10, 100, 80);
    fix.doc.add_entity(c3);
    fix.doc.add_entity(c4);
    fix.doc.add_entity(r1);

    cad::apply_equal_size_command(fix.doc, &fix.stack, {c3, c4, r1});
    CHECK(c4->radius == doctest::Approx(35.0));
    CHECK(r1->width == doctest::Approx(70.0));
    CHECK(r1->height == doctest::Approx(70.0));

    fix.stack.undo();
    CHECK(c4->radius == doctest::Approx(10.0));
    CHECK(r1->width == doctest::Approx(100.0));
    CHECK(r1->height == doctest::Approx(80.0));
}

TEST_CASE("solidworks: relacoes geometricas - alinhamento de centros horizontal e vertical") {
    ToolFixture fix;
    auto c1 = circle(100, 50, 20);
    auto c2 = circle(250, 80, 15);
    fix.doc.add_entity(c1);
    fix.doc.add_entity(c2);

    // Alinhamento de centro horizontal (alinha eixo X ao centro de c1, mantendo Y)
    cad::apply_align_center_h_command(fix.doc, &fix.stack, {c1, c2});
    CHECK(c2->cx == doctest::Approx(100.0));
    CHECK(c2->cy == doctest::Approx(80.0));

    fix.stack.undo();
    CHECK(c2->cx == doctest::Approx(250.0));
    CHECK(c2->cy == doctest::Approx(80.0));

    // Alinhamento de centro vertical (alinha eixo Y ao centro de c1, mantendo X)
    cad::apply_align_center_v_command(fix.doc, &fix.stack, {c1, c2});
    CHECK(c2->cx == doctest::Approx(250.0));
    CHECK(c2->cy == doctest::Approx(50.0));

    fix.stack.undo();
    CHECK(c2->cx == doctest::Approx(250.0));
    CHECK(c2->cy == doctest::Approx(80.0));
}

TEST_CASE("solidworks: relacoes geometricas - tangente circulo-linha e circulo-circulo") {
    ToolFixture fix;
    auto c1 = circle(0, 0, 10);
    auto l1 = line(-20, 25, 20, 25);
    fix.doc.add_entity(c1);
    fix.doc.add_entity(l1);

    // Tangência círculo-linha: desloca a linha para y=10
    cad::apply_tangent_command(fix.doc, &fix.stack, {c1, l1});
    CHECK(l1->y1 == doctest::Approx(10.0));
    CHECK(l1->y2 == doctest::Approx(10.0));

    fix.stack.undo();
    CHECK(l1->y1 == doctest::Approx(25.0));

    // Tangência círculo-círculo: distância entre centros torna-se R1 + R2 = 20
    auto c2 = circle(30, 0, 10);
    fix.doc.add_entity(c2);
    cad::apply_tangent_command(fix.doc, &fix.stack, {c1, c2});
    CHECK(c2->cx == doctest::Approx(20.0));
    CHECK(c2->cy == doctest::Approx(0.0));

    fix.stack.undo();
    CHECK(c2->cx == doctest::Approx(30.0));
}

TEST_CASE("solidworks: relacoes geometricas - uniao rapida de vetores e agrupamento") {
    ToolFixture fix;
    auto l1 = line(0, 0, 10, 0);
    auto l2 = line(10, 0, 20, 10);
    fix.doc.add_entity(l1);
    fix.doc.add_entity(l2);

    // Vetores abertos que se tocam são unidos em uma polilinha
    cad::apply_quick_group_or_join_command(fix.doc, &fix.stack, {l1, l2});
    CHECK(fix.doc.entities.size() == 1);
    auto poly = std::dynamic_pointer_cast<cad::Polyline>(fix.doc.entities.front());
    REQUIRE(poly);
    CHECK(poly->points.size() == 3);

    fix.stack.undo();
    CHECK(fix.doc.entities.size() == 2);

    // Formas fechadas (círculos) não unem pontas abertas: são agrupadas
    ToolFixture fix2;
    auto c1 = circle(0, 0, 10);
    auto c2 = circle(50, 0, 10);
    fix2.doc.add_entity(c1);
    fix2.doc.add_entity(c2);

    cad::apply_quick_group_or_join_command(fix2.doc, &fix2.stack, {c1, c2});
    CHECK(fix2.doc.entities.size() == 2);
    REQUIRE(c1->group.has_value());
    REQUIRE(c2->group.has_value());
    CHECK(!c1->group->empty());
    CHECK(c1->group.value() == c2->group.value());

    fix2.stack.undo();
    CHECK((!c1->group.has_value() || c1->group->empty()));
    CHECK((!c2->group.has_value() || c2->group->empty()));
}

TEST_CASE("solidworks: mini-menu flutuante contextual (ContextualHudBar)") {
    auto c1 = circle(0, 0, 10);
    auto c2 = circle(50, 0, 10);

    ui::ContextualHudBar hud;
    bool concentric_called = false;
    bool equal_size_called = false;
    bool align_h_called = false;
    bool align_v_called = false;
    bool tangent_called = false;
    bool group_join_called = false;

    hud.set_actions(
        [&]() { concentric_called = true; },
        [&]() { equal_size_called = true; },
        [&]() { align_h_called = true; },
        [&]() { align_v_called = true; },
        [&]() { tangent_called = true; },
        [&]() { group_join_called = true; }
    );

    // Com menos de 2 entidades, não ativa
    hud.show_at(QPoint(100, 100), {c1});
    CHECK_FALSE(hud.has_selection());

    // Com 2 ou mais entidades, ativa e permite acionar comandos
    hud.show_at(QPoint(100, 100), {c1, c2});
    CHECK(hud.has_selection());
    CHECK(hud.selection_count() == 2);

    hud.trigger_concentric();
    CHECK(concentric_called);

    hud.trigger_equal_size();
    CHECK(equal_size_called);

    hud.trigger_align_center_h();
    CHECK(align_h_called);

    hud.trigger_align_center_v();
    CHECK(align_v_called);

    hud.trigger_tangent();
    CHECK(tangent_called);

    hud.trigger_group_join();
    CHECK(group_join_called);

    hud.hide_hud();
    CHECK_FALSE(hud.has_selection());
    CHECK(hud.selection_count() == 0);
}

TEST_CASE("solidworks: clique unico de ancora, movimento livre e entrada numerica direta") {
    ToolFixture fix;
    tools::CircleTool circle_tool(fix.ctx);
    circle_tool.activate();

    // Clique único (press e release no mesmo ponto sem arrasto)
    circle_tool.on_press(10, 20, LEFT, NONE);
    circle_tool.on_release(10, 20, LEFT, NONE);
    CHECK(circle_tool.is_dragging()); // Âncora permanece ativa para criação por 2 cliques ou numérica

    // Move livremente o cursor sem botão pressionado
    circle_tool.on_move(30, 20, NONE);

    // Digita cota exata "18" e confirma com Enter
    circle_tool.on_key(Qt::Key_1);
    circle_tool.on_key(Qt::Key_8);
    circle_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 1);
    auto c = std::dynamic_pointer_cast<cad::Circle>(fix.doc.entities.back());
    REQUIRE(c);
    CHECK(c->cx == doctest::Approx(10.0));
    CHECK(c->cy == doctest::Approx(20.0));
    CHECK(c->radius == doctest::Approx(18.0));
    CHECK_FALSE(circle_tool.is_dragging());

    // Retângulo por clique único + movimento livre + digitação com Tab
    tools::RectangleTool rect_tool(fix.ctx);
    rect_tool.activate();
    rect_tool.on_press(0, 0, LEFT, NONE);
    rect_tool.on_release(0, 0, LEFT, NONE);
    CHECK(rect_tool.is_dragging());

    rect_tool.on_move(15, 20, NONE);
    rect_tool.on_key(Qt::Key_4);
    rect_tool.on_key(Qt::Key_0);
    rect_tool.on_key(Qt::Key_Tab);
    rect_tool.on_key(Qt::Key_2);
    rect_tool.on_key(Qt::Key_5);
    rect_tool.on_key(Qt::Key_Return);

    REQUIRE(fix.doc.entities.size() == 2);
    auto r = std::dynamic_pointer_cast<cad::Rectangle>(fix.doc.entities.back());
    REQUIRE(r);
    CHECK(r->x == doctest::Approx(0.0));
    CHECK(r->y == doctest::Approx(0.0));
    CHECK(r->width == doctest::Approx(40.0));
    CHECK(r->height == doctest::Approx(25.0));
    CHECK_FALSE(rect_tool.is_dragging());
}

TEST_CASE("solidworks: igualar tamanho preserva o centro do retangulo") {
    ToolFixture fix;
    auto c1 = circle(0, 0, 35);
    auto r1 = rect(10, 10, 100, 80); // centro em (60, 50)
    fix.doc.add_entity(c1);
    fix.doc.add_entity(r1);

    const Pt initial_center = r1->center();
    CHECK(initial_center.x == doctest::Approx(60.0));
    CHECK(initial_center.y == doctest::Approx(50.0));

    cad::apply_equal_size_command(fix.doc, &fix.stack, {c1, r1});
    CHECK(r1->width == doctest::Approx(70.0));
    CHECK(r1->height == doctest::Approx(70.0));
    CHECK(r1->center().x == doctest::Approx(60.0));
    CHECK(r1->center().y == doctest::Approx(50.0));

    fix.stack.undo();
    CHECK(r1->width == doctest::Approx(100.0));
    CHECK(r1->height == doctest::Approx(80.0));
    CHECK(r1->center().x == doctest::Approx(60.0));
    CHECK(r1->center().y == doctest::Approx(50.0));
}

TEST_CASE("solidworks: selecao mista de vetores abertos e fechados agrupa") {
    ToolFixture fix;
    auto l1 = line(0, 0, 10, 0);
    auto l2 = line(10, 0, 20, 0);
    auto c1 = circle(50, 50, 15);
    fix.doc.add_entity(l1);
    fix.doc.add_entity(l2);
    fix.doc.add_entity(c1);

    // Quando há seleção mista de formas fechadas e abertas, o comando agrupa o conjunto
    cad::apply_quick_group_or_join_command(fix.doc, &fix.stack, {l1, l2, c1});
    CHECK(fix.doc.entities.size() == 3);
    REQUIRE(l1->group.has_value());
    REQUIRE(l2->group.has_value());
    REQUIRE(c1->group.has_value());
    CHECK(!l1->group->empty());
    CHECK(l1->group.value() == l2->group.value());
    CHECK(l1->group.value() == c1->group.value());

    fix.stack.undo();
    CHECK((!l1->group.has_value() || l1->group->empty()));
    CHECK((!l2->group.has_value() || l2->group->empty()));
    CHECK((!c1->group.has_value() || c1->group->empty()));
}

} // namespace cnc::testing

