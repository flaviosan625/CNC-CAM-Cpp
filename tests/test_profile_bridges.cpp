#include <doctest.h>

#include "cam/profile.hpp"

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/gcode.hpp"
#include "cam/tools.hpp"

using namespace cnc;

TEST_CASE("pontes manuais aparecem no vetor correto e podem substituir distribuição automática") {
    auto circle = std::make_shared<cad::Circle>();
    circle->id = "contorno-1";
    circle->cx = 0;
    circle->cy = 0;
    circle->radius = 50;

    cam::ProfileSettings settings;
    settings.tabs = true;
    settings.tab_manual_only = true;
    settings.manual_tabs = {{"contorno-1", 50, 0}, {"outro-vetor", 0, 50}};

    const auto preview = cam::profile_tab_preview({circle}, settings);
    REQUIRE(preview.size() == 1);
    CHECK(preview[0].manual);
    CHECK(preview[0].entity_id == "contorno-1");
    CHECK(preview[0].x == doctest::Approx(50.0));
    CHECK(preview[0].y == doctest::Approx(0.0));

    settings.tab_manual_only = false;
    const auto mixed_preview = cam::profile_tab_preview({circle}, settings);
    CHECK(mixed_preview.size() > preview.size());
}

TEST_CASE("pontes manuais: movimentação livre, exclusão por clique e verificação no Gcode") {
    auto rect = std::make_shared<cad::Rectangle>();
    rect->id = "peca-retangular";
    rect->x = 0;
    rect->y = 0;
    rect->width = 100;
    rect->height = 100;

    cad::MaterialSetup material;
    material.thickness = 15.0;
    material.zero_z = "top";

    cam::Tool tool;
    tool.diameter = 6.0;
    tool.pass_depth = 5.0;
    tool.feed_rate = 1200.0;
    tool.plunge_rate = 400.0;

    cam::ProfileSettings settings;
    settings.name = "Perfil com pontes";
    settings.side = cam::ProfileSide::Outside;
    settings.cut_depth = 10.0;
    settings.tabs = true;
    settings.tab_thickness = 3.0;
    settings.tab_length = 8.0;
    settings.tab_manual_only = true;

    // 1. Movimentação livre: projetar no contorno mais próximo mesmo soltando longe da linha
    cam::ProfileSettings proj1 = settings;
    proj1.manual_tabs = {{"peca-retangular", 50.0, -40.0}};
    auto p1 = cam::profile_tab_preview({rect}, proj1, &tool);
    REQUIRE(p1.size() == 1);
    CHECK(p1[0].x == doctest::Approx(50.0).epsilon(0.5));
    CHECK(p1[0].y == doctest::Approx(-3.0).epsilon(0.5)); // raio de 3mm fora da borda Y=0

    cam::ProfileSettings proj2 = settings;
    proj2.manual_tabs = {{"peca-retangular", 150.0, 50.0}};
    auto p2 = cam::profile_tab_preview({rect}, proj2, &tool);
    REQUIRE(p2.size() == 1);
    CHECK(p2[0].x == doctest::Approx(103.0).epsilon(0.5)); // 100 + 3mm fora da borda X=100
    CHECK(p2[0].y == doctest::Approx(50.0).epsilon(0.5));

    // Configuramos as 2 pontes manuais na peça
    settings.manual_tabs = {
        {"peca-retangular", 50.0, -3.0},
        {"peca-retangular", 103.0, 50.0}
    };

    // Gera o percurso e o G-code com 2 pontes
    auto paths_2_tabs = cam::generate_profile({rect}, tool, material, settings);
    REQUIRE_FALSE(paths_2_tabs.empty());
    std::string gcode_2_tabs = cam::generate_gcode(paths_2_tabs, material);
    REQUIRE_FALSE(gcode_2_tabs.empty());

    // Na cota Z-10, as pontes de altura 3.0mm sobem a ferramenta para Z-7
    const auto count_bridge_retracts = [](const std::string& gcode) {
        int count = 0;
        size_t pos = 0;
        while ((pos = gcode.find("Z-7", pos)) != std::string::npos) {
            ++count;
            pos += 3;
        }
        return count;
    };

    const int bridges_in_gcode_2 = count_bridge_retracts(gcode_2_tabs);
    CHECK(bridges_in_gcode_2 == 2);
    CHECK(gcode_2_tabs.find("Z-10") != std::string::npos);

    // 2. Exclusão por clique: remover a ponte do índice 1
    settings.manual_tabs.erase(settings.manual_tabs.begin() + 1);
    CHECK(settings.manual_tabs.size() == 1);

    auto paths_1_tab = cam::generate_profile({rect}, tool, material, settings);
    std::string gcode_1_tab = cam::generate_gcode(paths_1_tab, material);
    const int bridges_in_gcode_1 = count_bridge_retracts(gcode_1_tab);
    CHECK(bridges_in_gcode_1 == 1);

    // 3. Exclusão de todas as pontes por clique: nenhuma ponte restante
    settings.manual_tabs.clear();
    settings.tabs = false;

    auto paths_no_tabs = cam::generate_profile({rect}, tool, material, settings);
    std::string gcode_no_tabs = cam::generate_gcode(paths_no_tabs, material);
    const int bridges_in_gcode_0 = count_bridge_retracts(gcode_no_tabs);
    CHECK(bridges_in_gcode_0 == 0);
    CHECK(gcode_no_tabs.find("Z-10") != std::string::npos);
}
