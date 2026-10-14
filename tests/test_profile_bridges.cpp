#include <doctest.h>

#include "cam/profile.hpp"

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
