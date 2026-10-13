// Camada GEOS: os valores esperados foram gerados com o shapely 2.2.0 real
// (o mesmo GEOS 3.14.1), nao calculados a mao.
#include <doctest.h>

#include "geo/geom.hpp"

using namespace cnc;
using namespace cnc::geo;

namespace {
Geom square(double x0, double y0, double x1, double y1) {
    return make_polygon({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
}
}  // namespace

TEST_CASE("geom: construcao e medidas") {
    const Geom sq = square(0, 0, 10, 10);
    REQUIRE(sq);
    CHECK(sq.geom_type() == "Polygon");
    CHECK(sq.area() == doctest::Approx(100.0));
    CHECK(sq.length() == doctest::Approx(40.0));
    CHECK(sq.is_valid());
    const BBox b = sq.bounds();
    CHECK(b.x0 == 0.0);
    CHECK(b.y1 == 10.0);
    CHECK(sq.centroid().x == doctest::Approx(5.0));
    CHECK(sq.exterior_coords().size() == 5);  // com o ponto de fechamento
}

TEST_CASE("geom: entradas invalidas viram Geom nula, como o ValueError do shapely") {
    CHECK_FALSE(make_polygon({{0, 0}, {1, 1}}));
    CHECK_FALSE(make_linestring({{0, 0}}));
}

TEST_CASE("geom: buffer com os defaults do shapely (quad_segs=16)") {
    const Geom sq = square(0, 0, 10, 10);
    CHECK(sq.buffer(2).area() == doctest::Approx(192.5461939621837));
    CHECK(sq.buffer(2, 16, Join::Mitre).area() == doctest::Approx(196.0));
    CHECK(sq.buffer(-2).area() == doctest::Approx(36.0));
    CHECK(sq.buffer(-6).is_empty());
}

TEST_CASE("geom: booleanas") {
    const Geom a = square(0, 0, 10, 10), b = square(5, 5, 15, 15);
    const Geom u = unary_union({a, b});
    CHECK(u.geom_type() == "Polygon");
    CHECK(u.area() == doctest::Approx(175.0));
    CHECK(u.exterior_coords().size() == 9);
    CHECK(a.difference(b).area() == doctest::Approx(75.0));
    CHECK(a.intersection(b).area() == doctest::Approx(25.0));
    CHECK(a.symmetric_difference(b).area() == doctest::Approx(150.0));
    CHECK(a.intersects(b));
    CHECK_FALSE(a.contains(b));
    CHECK(unary_union({}).is_empty());
}

TEST_CASE("geom: orient reverte o anel horario, como shapely.geometry.polygon.orient") {
    const Geom cw = make_polygon({{0, 0}, {0, 10}, {10, 10}, {10, 0}});
    const Points ring = orient(cw, 1.0).exterior_coords();
    REQUIRE(ring.size() == 5);
    CHECK(ring[1] == Pt{10, 0});
    CHECK(ring[2] == Pt{10, 10});
    CHECK(ring[3] == Pt{0, 10});
    CHECK(ring_signed_area(ring) > 0);
}

TEST_CASE("geom: poligono com furo") {
    const Geom p = make_polygon({{0, 0}, {20, 0}, {20, 20}, {0, 20}}, {{{5, 5}, {15, 5}, {15, 15}, {5, 15}}});
    REQUIRE(p);
    CHECK(p.area() == doctest::Approx(300.0));
    CHECK(p.interior_coords().size() == 1);
    CHECK(p.shell_polygon().area() == doctest::Approx(400.0));
    const Pt rp = p.representative_point();
    CHECK(p.contains(make_point(rp)));
}

TEST_CASE("geom: offset_curve, interpolate e project") {
    const Geom ls = make_linestring({{0, 0}, {10, 0}, {10, 10}});
    const Points left = ls.offset_curve(2.0).coords();
    REQUIRE(left.size() == 3);
    CHECK(left[0] == Pt{0, 2});
    CHECK(left[1] == Pt{8, 2});
    CHECK(left[2] == Pt{8, 10});
    // pela direita, o canto externo ganha o arco (19 pontos no shapely)
    CHECK(ls.offset_curve(-2.0).coords().size() == 19);
    const Pt mid = ls.interpolate(15.0);
    CHECK(mid.x == doctest::Approx(10.0));
    CHECK(mid.y == doctest::Approx(5.0));
    CHECK(ls.project({12, 5}) == doctest::Approx(15.0));
}

TEST_CASE("geom: maior circulo inscrito") {
    const auto [c, r] = maximum_inscribed_circle(square(0, 0, 20, 10), 0.0);
    CHECK(r == doctest::Approx(5.0));
    CHECK(c.y == doctest::Approx(5.0));
}

TEST_CASE("geom: BoxIndex devolve candidatos em ordem crescente, bordas incluidas") {
    BoxIndex idx;
    idx.add({0, 0, 10, 10});
    idx.add({20, 20, 30, 30});
    idx.add({10, 10, 12, 12});  // so encosta no canto
    const auto hit = idx.query({10, 10, 10, 10});
    REQUIRE(hit.size() == 2);
    CHECK(hit[0] == 0);
    CHECK(hit[1] == 2);
}
