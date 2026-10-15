#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "cad/document.hpp"
#include "viewport/entity_items.hpp"
#include "viewport/snapping.hpp"

using namespace cnc;
using namespace cnc::vp;

namespace {

struct CountingPolyline : cad::Polyline {
    mutable size_t bounds_calls = 0;
    mutable size_t points_calls = 0;
    BBox bounding_box() const override {
        ++bounds_calls;
        return cad::Polyline::bounding_box();
    }
    Points to_points(int segments = 64) const override {
        ++points_calls;
        return cad::Polyline::to_points(segments);
    }
};

// Referencia independente: conserva a ordem e o limite do algoritmo anterior.
Points brute_intersections(const cad::Entity& a, const cad::Entity& b) {
    const auto pa = a.to_points();
    const auto pb = b.to_points();
    Points out;
    if (pa.size() < 2 || pb.size() < 2) return out;
    const size_t na = a.closed() ? pa.size() : pa.size() - 1;
    const size_t nb = b.closed() ? pb.size() : pb.size() - 1;
    for (size_t i = 0; i < na; ++i) {
        const Pt p1 = pa[i], p2 = pa[(i + 1) % pa.size()];
        for (size_t j = 0; j < nb; ++j) {
            const Pt p3 = pb[j], p4 = pb[(j + 1) % pb.size()];
            const double den = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
            if (std::fabs(den) < 1e-12) continue;
            const double t = ((p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x)) / den;
            const double u = ((p1.x - p3.x) * (p1.y - p2.y) - (p1.y - p3.y) * (p1.x - p2.x)) / den;
            if (!(0 <= t && t <= 1 && 0 <= u && u <= 1)) continue;
            out.push_back({p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)});
            if (out.size() > 32) return out;
        }
    }
    return out;
}

MovingSnap brute_moving(const cad::Document& doc, const SnapSettings& settings,
                       const std::vector<cad::EntityPtr>& moving, double dx, double dy, double ppm) {
    const double radius = settings.radius_px / ppm;
    std::vector<NotablePoint> mobile, fixed;
    std::set<std::string> ids;
    for (const auto& e : moving) {
        ids.insert(e->id);
        for (const auto& [p, kind] : entity_points(*e)) mobile.push_back({{p.x + dx, p.y + dy}, kind});
    }
    REQUIRE(!mobile.empty());
    BBox box{mobile.front().first.x, mobile.front().first.y, mobile.front().first.x, mobile.front().first.y};
    for (const auto& [p, kind] : mobile) {
        box.x0 = std::min(box.x0, p.x); box.x1 = std::max(box.x1, p.x);
        box.y0 = std::min(box.y0, p.y); box.y1 = std::max(box.y1, p.y);
    }
    for (const auto& e : doc.entities) {
        if (ids.count(e->id)) continue;
        const auto* layer = doc.get_layer(e->layer);
        if (layer && !layer->visible) continue;
        const auto b = e->bounding_box();
        if (b.x1 + radius < box.x0 || b.x0 - radius > box.x1 || b.y1 + radius < box.y0 || b.y0 - radius > box.y1) continue;
        const auto points = entity_points(*e);
        fixed.insert(fixed.end(), points.begin(), points.end());
    }
    std::optional<std::pair<int, double>> best;
    MovingSnap result{dx, dy, std::nullopt};
    for (const auto& [m, mk] : mobile) {
        for (const auto& [f, fk] : fixed) {
            const double d = cnc::hypot(f.x - m.x, f.y - m.y);
            if (d > radius) continue;
            const std::pair<int, double> key{std::min(snap_priority(mk), snap_priority(fk)), d};
            if (!best || key < *best) {
                best = key;
                result = {dx + f.x - m.x, dy + f.y - m.y,
                          SnapResult{f.x, f.y, snap_priority(fk) <= snap_priority(mk) ? fk : mk, 0, nullptr}};
            }
        }
    }
    return result;
}

}  // namespace

TEST_CASE("snap: geometria parada e reutilizada e invalidada ao editar") {
    cad::Document doc;
    auto poly = std::make_shared<CountingPolyline>();
    poly->points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    poly->is_closed = true;
    doc.add_entity(poly);
    SnapEngine engine;
    engine.document = &doc;
    engine.settings.material = false;
    engine.settings.radius_px = 0.5;
    REQUIRE(engine.snap(0, 0, 1).has_value());
    const size_t bounds_calls = poly->bounds_calls;
    const size_t points_calls = poly->points_calls;
    for (int i = 0; i < 100; ++i) REQUIRE(engine.snap(0.001 * i, 0, 1).has_value());
    CHECK(poly->bounds_calls == bounds_calls);
    CHECK(poly->points_calls == points_calls);

    poly->translate(100, 0);
    CHECK_FALSE(engine.snap(0, 0, 1).has_value());
    REQUIRE(engine.snap(100, 0, 1).has_value());
    CHECK(poly->bounds_calls > bounds_calls);
    CHECK_FALSE(engine.snap(100, 0, 1, {poly->id}).has_value());
    doc.get_layer(poly->layer)->visible = false;
    CHECK_FALSE(engine.snap(100, 0, 1).has_value());
    doc.get_layer(poly->layer)->visible = true;
    REQUIRE(engine.snap(100, 0, 1).has_value());
    doc.remove_entity(poly->id);
    CHECK_FALSE(engine.snap(100, 0, 1).has_value());
    auto replacement = std::make_shared<cad::Line>();
    replacement->x1 = 100; replacement->y1 = 0;
    replacement->x2 = 110; replacement->y2 = 0;
    doc.add_entity(replacement);
    REQUIRE(engine.snap(100, 0, 1).has_value());
    CHECK(engine.snap(100, 0, 1)->entity == replacement);
}

TEST_CASE("snap: intersecoes indexadas mantem ordem e limite da referencia") {
    std::mt19937 generator(4751);
    std::uniform_real_distribution<double> coordinate(-100, 100);
    for (int sample = 0; sample < 40; ++sample) {
        cad::Polyline a, b;
        a.is_closed = sample % 2 == 0;
        b.is_closed = sample % 3 == 0;
        for (int i = 0; i < 90; ++i) {
            a.points.push_back({coordinate(generator), coordinate(generator)});
            b.points.push_back({coordinate(generator), coordinate(generator)});
        }
        const auto expected = brute_intersections(a, b);
        const auto actual = segment_intersections(a, b);
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) CHECK(actual[i] == expected[i]);
    }
}

TEST_CASE("snap: segmentos e intersecoes reutilizados acompanham versao") {
    cad::Document doc;
    auto a = std::make_shared<CountingPolyline>();
    auto b = std::make_shared<CountingPolyline>();
    a->points = {{-100, 0}, {100, 0}};
    b->points = {{0, -100}, {0, 100}};
    doc.add_entity(a); doc.add_entity(b);
    SnapEngine engine;
    engine.document = &doc;
    engine.settings.material = engine.settings.nodes = engine.settings.midpoints = engine.settings.centers = false;
    engine.settings.radius_px = 0.5;
    auto hit = engine.snap(0.1, 0.1, 1);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == SnapKind::Intersection);
    const size_t calls_a = a->points_calls, calls_b = b->points_calls;
    for (int i = 0; i < 30; ++i) REQUIRE(engine.snap(0.1, 0.1, 1).has_value());
    CHECK(a->points_calls == calls_a);
    CHECK(b->points_calls == calls_b);
    b->translate(10, 0);
    hit = engine.snap(10.1, 0.1, 1);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == SnapKind::Intersection);
    CHECK(hit->x == doctest::Approx(10));
    CHECK(hit->y == doctest::Approx(0));
    CHECK(a->points_calls == calls_a);
    CHECK(b->points_calls > calls_b);
    engine.settings.intersections = false;
    hit = engine.snap(-20, 0.1, 1);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == SnapKind::OnSpan);
    CHECK(hit->x == -20);
    CHECK(hit->y == 0);
}

TEST_CASE("snap: arrasto indexado mantem prioridade distancia e desempates") {
    cad::Document doc;
    for (int i = 0; i < 40; ++i) {
        auto r = std::make_shared<cad::Rectangle>();
        r->x = (i % 8) * 40; r->y = (i / 8) * 40;
        r->width = r->height = 20;
        doc.add_entity(r);
    }
    const std::vector<cad::EntityPtr> moving{doc.entities[0], doc.entities[1], doc.entities[8]};
    SnapEngine engine;
    engine.document = &doc;
    engine.settings.material = false;
    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 15; ++x) {
            const double dx = x * 4.3, dy = y * 3.7;
            const auto expected = brute_moving(doc, engine.settings, moving, dx, dy, 2);
            const auto actual = engine.snap_moving(moving, dx, dy, 2);
            CHECK(actual.dx == doctest::Approx(expected.dx));
            CHECK(actual.dy == doctest::Approx(expected.dy));
            REQUIRE(actual.hit.has_value() == expected.hit.has_value());
            if (actual.hit) {
                CHECK(actual.hit->point() == expected.hit->point());
                CHECK(actual.hit->kind == expected.hit->kind);
            }
        }
    }
}

TEST_CASE("snap: cotas vinculadas invalidam geometria ao mover e redimensionar a fonte") {
    cad::Document doc;
    auto circle = std::make_shared<cad::Circle>();
    circle->cx = circle->cy = 20;
    circle->radius = 10;
    doc.add_entity(circle);
    auto dimension = std::make_shared<cad::Dimension>();
    dimension->kind_name = "radius";
    dimension->source_id = circle->id;
    dimension->x1 = dimension->y1 = 20;
    dimension->x2 = 30; dimension->y2 = 20;
    dimension->x3 = 40; dimension->y3 = 20;
    doc.add_entity(dimension);
    doc.sync_dimensions();
    const auto initial_version = dimension->version;
    doc.sync_dimensions();
    CHECK(dimension->version == initial_version);

    SnapEngine engine;
    engine.document = &doc;
    engine.settings.material = false;
    engine.settings.radius_px = 0.25;
    REQUIRE(engine.snap(20, 20, 1, {circle->id}).has_value());
    EntityItem item(dimension, 400);
    REQUIRE(item.hit_test_world(25, 20, 0.2));
    CHECK(item.world_bounds().x1 == doctest::Approx(40));

    circle->translate(100, 40);
    doc.sync_dimensions();
    CHECK(dimension->version > initial_version);
    CHECK_FALSE(engine.snap(20, 20, 1, {circle->id}).has_value());
    auto hit = engine.snap(120, 60, 1, {circle->id});
    REQUIRE(hit.has_value());
    CHECK(hit->entity == dimension);
    item.refresh();
    CHECK_FALSE(item.hit_test_world(25, 20, 0.2));
    CHECK(item.hit_test_world(125, 60, 0.2));
    CHECK(item.world_bounds().x1 == doctest::Approx(140));
    const auto translated_version = dimension->version;
    doc.sync_dimensions();
    CHECK(dimension->version == translated_version);

    circle->scale(3, 3, circle->cx, circle->cy);
    doc.sync_dimensions();
    CHECK(dimension->version > translated_version);
    hit = engine.snap(150, 60, 1, {circle->id});
    REQUIRE(hit.has_value());
    CHECK(hit->entity == dimension);
    item.refresh();
    CHECK(item.world_bounds().x1 == doctest::Approx(150));
}
