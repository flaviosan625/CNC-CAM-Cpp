// Teste de fumaca: confirma que toda a cadeia de dependencias compila e roda.
#include <doctest.h>

#include <nlohmann/json.hpp>

#include <QString>
#include <geos_c.h>

#include "clipper.hpp"
#include "geo/types.hpp"

TEST_CASE("toolchain: Qt, GEOS, Clipper e json funcionam juntos") {
    SUBCASE("Qt") {
        CHECK(QString("cnc").toUpper().toStdString() == "CNC");
    }
    SUBCASE("GEOS 3.14") {
        GEOSContextHandle_t h = GEOS_init_r();
        GEOSGeometry* g = GEOSGeomFromWKT_r(h, "POLYGON((0 0, 10 0, 10 5, 0 5, 0 0))");
        REQUIRE(g != nullptr);
        double area = 0;
        GEOSArea_r(h, g, &area);
        CHECK(area == doctest::Approx(50.0));
        GEOSGeom_destroy_r(h, g);
        GEOS_finish_r(h);
    }
    SUBCASE("Clipper 6.4.2: offset de 1 mm num quadrado de 10 mm") {
        ClipperLib::Path sq = {{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}};
        ClipperLib::ClipperOffset co;
        co.AddPath(sq, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
        ClipperLib::Paths out;
        co.Execute(out, 1000);
        REQUIRE(out.size() == 1);
        CHECK(std::fabs(ClipperLib::Area(out[0])) == doctest::Approx(144.0e6));
    }
    SUBCASE("nlohmann/json") {
        auto j = nlohmann::json::parse(R"({"a": [1, 2.5, "x"]})");
        CHECK(j["a"][1].get<double>() == doctest::Approx(2.5));
    }
}

TEST_CASE("aritmetica do Python: round, radians, divisao") {
    using namespace cnc;
    // round() do Python e banker's: 0.5 -> 0, 1.5 -> 2, 2.5 -> 2
    CHECK(py_round(0.5) == 0.0);
    CHECK(py_round(1.5) == 2.0);
    CHECK(py_round(2.5) == 2.0);
    CHECK(py_round(-0.5) == 0.0);
    // round(x, n) sobre o valor binario exato: round(0.285, 2) == 0.28
    CHECK(py_round(0.285, 2) == 0.28);
    CHECK(py_round(2.675, 2) == 2.67);
    // math.radians(180) == math.pi
    CHECK(radians(180.0) == kPi);
    // -7 // 2 == -4 e -7 % 2 == 1 no Python
    CHECK(floordiv(-7, 2) == -4);
    CHECK(fmod_py(-7.0, 2.0) == 1.0);
    CHECK(new_id().size() == 12);
    CHECK(new_id() != new_id());
}
