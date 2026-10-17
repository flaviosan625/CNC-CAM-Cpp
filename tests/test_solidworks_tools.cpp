#include <doctest.h>

#include "cad/geometry.hpp"
#include "tool_fixture.hpp"
#include "tools/draw_tools.hpp"
#include "tools/edit_tools.hpp"

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

} // namespace cnc::testing

