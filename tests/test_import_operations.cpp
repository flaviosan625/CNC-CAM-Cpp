#include <doctest.h>

#include "io/import_operations.hpp"

namespace {

std::shared_ptr<cnc::cad::Rectangle> rectangle(double x, double y, double width, double height) {
    auto entity = std::make_shared<cnc::cad::Rectangle>();
    entity->x = x;
    entity->y = y;
    entity->width = width;
    entity->height = height;
    return entity;
}

std::shared_ptr<cnc::cad::Polyline> polygon(cnc::Points points) {
    auto entity = std::make_shared<cnc::cad::Polyline>();
    entity->is_closed = true;
    entity->points = std::move(points);
    return entity;
}

using Operation = cnc::io::ImportedOperation;

}  // namespace

TEST_CASE("import operations: nested contours alternate cut sides without mutating inputs") {
    const auto outside = rectangle(0, 0, 100, 100);
    const auto hole = rectangle(10, 10, 80, 80);
    const auto island = rectangle(20, 20, 60, 60);
    const auto second_hole = rectangle(30, 30, 40, 40);
    const auto separate = rectangle(200, 0, 20, 20);
    outside->layer = "Cliente";
    outside->selected = true;
    const auto result = cnc::io::classify_imported_operations({outside, hole, island, second_hole, separate});
    REQUIRE(result.size() == 5);
    CHECK(result[0].entity_id == outside->id);
    CHECK(result[0].operation == Operation::CutExternal);
    CHECK(result[1].operation == Operation::CutInternal);
    CHECK(result[2].operation == Operation::CutExternal);
    CHECK(result[3].operation == Operation::CutInternal);
    CHECK(result[4].operation == Operation::CutExternal);
    CHECK(result[1].layer_name == "CORTE_INTERNO");
    CHECK(outside->layer == "Cliente");
    CHECK(outside->selected);
}

TEST_CASE("import operations: concave bounding box does not imply containment") {
    const auto l_shape = polygon({{0, 0}, {100, 0}, {100, 20}, {20, 20}, {20, 100}, {0, 100}});
    const auto in_empty_corner = rectangle(50, 50, 10, 10);
    const auto actual_hole = rectangle(5, 40, 10, 10);
    const auto result = cnc::io::classify_imported_operations({l_shape, in_empty_corner, actual_hole});
    REQUIRE(result.size() == 3);
    CHECK(result[0].operation == Operation::CutExternal);
    CHECK(result[1].operation == Operation::CutExternal);
    CHECK(result[2].operation == Operation::CutInternal);
}

TEST_CASE("import operations: overlapping touching and duplicate contours need review") {
    SUBCASE("partial overlap") {
        const auto result = cnc::io::classify_imported_operations({rectangle(0, 0, 40, 40), rectangle(20, 20, 40, 40)});
        REQUIRE(result.size() == 2);
        CHECK(result[0].operation == Operation::Review);
        CHECK(result[1].operation == Operation::Review);
    }
    SUBCASE("inside touching outside") {
        const auto result = cnc::io::classify_imported_operations({rectangle(0, 0, 40, 40), rectangle(0, 10, 20, 20)});
        REQUIRE(result.size() == 2);
        CHECK(result[0].operation == Operation::Review);
        CHECK(result[1].operation == Operation::Review);
    }
    SUBCASE("duplicate outside and its hole") {
        const auto result = cnc::io::classify_imported_operations({rectangle(0, 0, 40, 40), rectangle(0, 0, 40, 40), rectangle(10, 10, 10, 10)});
        REQUIRE(result.size() == 3);
        CHECK(result[0].operation == Operation::Review);
        CHECK(result[1].operation == Operation::Review);
        CHECK(result[2].operation == Operation::Review);
    }
    SUBCASE("separate contours with uncertain clearance") {
        const auto result = cnc::io::classify_imported_operations({rectangle(0, 0, 40, 40), rectangle(40.005, 0, 40, 40)});
        REQUIRE(result.size() == 2);
        CHECK(result[0].operation == Operation::Review);
        CHECK(result[1].operation == Operation::Review);
    }
}

TEST_CASE("import operations: open invalid and auxiliary vectors are never guessed as pockets") {
    const auto open = std::make_shared<cnc::cad::Polyline>();
    open->points = {{1, 2}, {3, 4}, {5, 8}};
    const auto invalid = polygon({{0, 0}, {10, 10}, {0, 10}, {10, 0}});
    const auto dimension = std::make_shared<cnc::cad::Dimension>();
    const auto text = std::make_shared<cnc::cad::Text>();
    const auto drill = std::make_shared<cnc::cad::DrillPoint>();
    const auto result = cnc::io::classify_imported_operations({nullptr, open, invalid, dimension, text, drill});
    REQUIRE(result.size() == 5);
    CHECK(result[0].operation == Operation::Open);
    for (size_t i = 1; i < result.size(); ++i) {
        CHECK(result[i].operation == Operation::Review);
        CHECK_FALSE(result[i].reason.empty());
    }
    CHECK(cnc::io::classify_imported_operations({}).empty());
}

TEST_CASE("import operations: thousands of independent pieces retain their own holes") {
    std::vector<cnc::cad::EntityPtr> entities;
    entities.reserve(4000);
    for (int i = 0; i < 2000; ++i) {
        const double x = (i % 50) * 30.0;
        const double y = (i / 50) * 30.0;
        entities.push_back(rectangle(x, y, 20, 20));
        entities.push_back(rectangle(x + 5, y + 5, 10, 10));
    }
    const auto result = cnc::io::classify_imported_operations(entities);
    REQUIRE(result.size() == entities.size());
    for (size_t i = 0; i < result.size(); i += 2) {
        REQUIRE(result[i].operation == Operation::CutExternal);
        REQUIRE(result[i + 1].operation == Operation::CutInternal);
    }
}

TEST_CASE("import operations: a malformed outer contour cannot imply an external child") {
    const auto malformed = polygon({{0, 0}, {100, 100}, {0, 100}, {100, 0}});
    const auto inside = rectangle(20, 20, 10, 10);
    const auto separate = rectangle(200, 0, 10, 10);
    const auto result = cnc::io::classify_imported_operations({malformed, inside, separate});
    REQUIRE(result.size() == 3);
    CHECK(result[0].operation == Operation::Review);
    CHECK(result[1].operation == Operation::Review);
    CHECK(result[2].operation == Operation::CutExternal);
}
