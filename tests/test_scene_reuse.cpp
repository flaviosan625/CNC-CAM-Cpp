#include <doctest.h>

#include <cmath>

#include <QSignalSpy>
#include <QPainterPath>

#include "cad/document.hpp"
#include "tools/draw_tools.hpp"
#include "viewport/canvas_2d.hpp"
#include "viewport/entity_items.hpp"

TEST_CASE("canvas: cache de clique acompanha edicao e cor de camada sem recriar a peca") {
    cnc::cad::Document document;
    auto& layer = document.add_layer("Pecas");
    layer.color = {0.2, 0.7, 0.4};
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->layer = "Pecas";
    rectangle->x = 100;
    rectangle->y = 100;
    rectangle->width = 20;
    rectangle->height = 10;
    document.add_entity(rectangle);
    cnc::vp::Canvas2D canvas;
    canvas.set_document(&document);
    CHECK(canvas.entity_at(100, 100) == rectangle);
    CHECK(canvas.entity_at(100, 100) == rectangle);

    rectangle->translate(100, 0);
    canvas.refresh_entities();
    CHECK(canvas.entity_at(100, 100) == nullptr);
    CHECK(canvas.entity_at(200, 100) == rectangle);

    document.get_layer("Pecas")->color = {0.8, 0.2, 0.3};
    canvas.refresh_entities();
    cnc::vp::EntityItem* item = nullptr;
    for (auto* candidate : canvas.scene()->items())
        if (auto* drawing = dynamic_cast<cnc::vp::EntityItem*>(candidate)) item = drawing;
    REQUIRE(item != nullptr);
    CHECK(item->pen().color() == QColor::fromRgbF(0.8f, 0.2f, 0.3f));
}

TEST_CASE("canvas: zoom e pan nao notificam alteracao da selecao") {
    cnc::cad::Document document;
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    document.add_entity(rectangle);
    document.set_selected(rectangle, true);
    cnc::UndoStack undo;
    cnc::tools::ToolContext context(document, undo);
    cnc::tools::SelectTool select(context);
    cnc::vp::Canvas2D canvas;
    context.canvas = &canvas;
    canvas.set_document(&document);
    canvas.set_tool(&select);
    QSignalSpy changes(&canvas, &cnc::vp::Canvas2D::selection_changed);
    canvas.zoom_by(1.2);
    canvas.view_changed();
    CHECK(changes.count() == 0);
    rectangle->translate(20, 0);
    canvas.refresh_entities();
    CHECK(changes.count() == 1);
    REQUIRE(canvas.handles.bbox().has_value());
    CHECK(canvas.handles.bbox()->x0 == doctest::Approx(rectangle->bounding_box().x0));
}

TEST_CASE("canvas: cache de clique inclui linhas de extensao das cotas") {
    auto dimension = std::make_shared<cnc::cad::Dimension>();
    dimension->x1 = 100;
    dimension->y1 = 100;
    dimension->x2 = 200;
    dimension->y2 = 100;
    dimension->offset = 30;
    cnc::vp::EntityItem item(dimension, 2440.0);
    REQUIRE(item.extension_item() != nullptr);
    const auto segments = dimension->segments();
    REQUIRE(segments.size() >= 2);
    for (size_t i = 0; i < 2; ++i) {
        const auto& [a, b] = segments[i];
        const double x = (a.x + b.x) / 2;
        const double y = (a.y + b.y) / 2;
        REQUIRE(cnc::vp::hit_test(*dimension, x, y, 0.5));
        CHECK(item.hit_test_world(x, y, 0.5));
    }
    dimension->translate(50, 0);
    item.refresh();
    CHECK_FALSE(item.hit_test_world(100, 115, 0.5));
    CHECK(item.hit_test_world(150, 115, 0.5));
}

TEST_CASE("canvas: edicao de propriedades invalida desenho clique e encaixe") {
    cnc::cad::Document document;
    auto circle = std::make_shared<cnc::cad::Circle>();
    circle->cx = circle->cy = 100;
    circle->radius = 20;
    document.add_entity(circle);
    cnc::UndoStack undo;
    cnc::tools::ToolContext context(document, undo);
    cnc::vp::Canvas2D canvas;
    canvas.set_document(&document);
    context.canvas = &canvas;
    CHECK(canvas.entity_at(120, 100) == circle);
    REQUIRE(canvas.snap.snap(120, 100, 1).has_value());
    const auto version = circle->version;
    context.transform({circle}, [](cnc::cad::Entity& entity) {
        static_cast<cnc::cad::Circle&>(entity).radius = 60;
    }, "Editar raio");
    CHECK(circle->version > version);
    CHECK(canvas.entity_at(120, 100) == nullptr);
    CHECK(canvas.entity_at(160, 100) == circle);
    auto hit = canvas.snap.snap(160, 100, 1);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == cnc::vp::SnapKind::Node);
    CHECK(hit->x == doctest::Approx(160));
    REQUIRE(undo.undo());
    canvas.refresh_entities();
    CHECK(canvas.entity_at(120, 100) == circle);
    CHECK(canvas.entity_at(160, 100) == nullptr);
    REQUIRE(undo.redo());
    canvas.refresh_entities();
    CHECK(canvas.entity_at(160, 100) == circle);
}

TEST_CASE("performance: mover etiquetas preserva os contornos ja calculados") {
    cnc::cad::Text label;
    label.set_cached_outlines({{{0, 0}, {10, 0}, {10, 5}, {0, 5}}});
    const auto version = label.version;
    label.translate(12, 7);
    const cnc::Points expected{{12, 7}, {22, 7}, {22, 12}, {12, 12}};
    REQUIRE(label.outline_polygons().size() == 1);
    CHECK(label.outline_polygons().front() == expected);
    CHECK(label.version > version);
    const auto snapshot = label.copy();
    label.translate(50, 0);
    label.restore_from(*snapshot);
    CHECK(label.outline_polygons().front() == expected);
}

TEST_CASE("viewport: texto desenhado com curvas mantem a caixa dos contornos CAM") {
    cnc::cad::Text label;
    label.x = 35.0;
    label.y = 62.0;
    label.height = 18.0;
    label.content = "CNC CAM 2500";
    label.font_family = "Arial";
    label.align = "center";
    label.rotation = 17.0;

    QPainterPath polygon_path;
    for (const cnc::Points& polygon : label.outline_polygons()) {
        if (polygon.size() < 2) continue;
        polygon_path.moveTo(polygon.front().x, 120.0 - polygon.front().y);
        for (size_t i = 1; i < polygon.size(); ++i)
            polygon_path.lineTo(polygon[i].x, 120.0 - polygon[i].y);
        polygon_path.closeSubpath();
    }
    const QPainterPath display_path = cnc::vp::build_path(label, 120.0);
    REQUIRE_FALSE(polygon_path.isEmpty());
    REQUIRE_FALSE(display_path.isEmpty());
    CHECK(display_path.elementCount() < polygon_path.elementCount());

    const QRectF expected = polygon_path.boundingRect();
    const QRectF actual = display_path.boundingRect();
    CHECK(std::fabs(actual.left() - expected.left()) < 0.1);
    CHECK(std::fabs(actual.right() - expected.right()) < 0.1);
    CHECK(std::fabs(actual.top() - expected.top()) < 0.1);
    CHECK(std::fabs(actual.bottom() - expected.bottom()) < 0.1);
}

TEST_CASE("viewport: LOD simplifica polilinha somente na pintura") {
    auto contour = std::make_shared<cnc::cad::Polyline>();
    for (int i = 0; i < 128; ++i)
        contour->points.push_back({static_cast<double>(i), 50.0 + std::sin(i * 0.1)});

    cnc::vp::EntityItem item(contour, 100.0);
    REQUIRE(item.display_lod_element_count() > 0);
    CHECK(item.display_lod_element_count() < contour->points.size());
    CHECK(item.path().elementCount() == contour->points.size());
    CHECK(item.hit_test_world(64.0, contour->points[64].y, 0.1));
}
