#include <doctest.h>

#include "cad/document.hpp"
#include "tools/draw_tools.hpp"
#include "viewport/entity_items.hpp"

namespace {

class CountedLivePolyline : public cnc::cad::Polyline {
public:
    int* copies = nullptr;
    cnc::cad::EntityPtr copy() const override {
        if (copies) ++*copies;
        return std::make_shared<CountedLivePolyline>(*this);
    }
};

class LiveTransformCanvas : public cnc::tools::ICanvas {
public:
    cnc::vp::EntityItem* item = nullptr;
    int refreshes = 0;
    int previews = 0;
    void refresh_entities() override {
        ++refreshes;
        if (item) item->refresh();
    }
    void set_preview(const cnc::Points&, bool, bool) override {}
    void show_dimension_preview(const std::shared_ptr<cnc::cad::Dimension>&) override {}
    void clear_preview() override {}
    void preview_translation(const std::vector<cnc::cad::EntityPtr>&, double dx, double dy) override {
        ++previews;
        if (item) item->setPos(dx, -dy);
    }
    void clear_translation_preview() override {
        if (item) item->setPos(0, 0);
    }
    double pick_tolerance() const override { return 1.0; }
};

} // namespace

TEST_CASE("performance: arrastar atualiza pre-visualizacao e confirma uma vez ao soltar") {
    cnc::cad::Document document;
    cnc::UndoStack undo;
    cnc::tools::ToolContext context(document, undo);
    LiveTransformCanvas canvas;
    context.canvas = &canvas;
    cnc::tools::SelectTool select(context);
    int copies = 0;
    auto polyline = std::make_shared<CountedLivePolyline>();
    polyline->copies = &copies;
    polyline->points = {{0, 0}, {100, 0}, {100, 40}, {0, 40}};
    document.add_entity(polyline);
    document.set_selected(polyline, true);
    cnc::vp::EntityItem item(polyline, document.material.height);
    canvas.item = &item;
    const double original_x = item.path().boundingRect().x();

    REQUIRE(select.on_press(50, 0, Qt::LeftButton, Qt::NoModifier));
    CHECK(copies == 0);
    for (int frame = 1; frame <= 8; ++frame) {
        CAPTURE(frame);
        const int refreshes = canvas.refreshes;
        const auto version = polyline->version;
        REQUIRE(select.on_move(50 + frame * 10, 0, Qt::NoModifier));
        CHECK(polyline->points.front().x == doctest::Approx(0));
        CHECK(item.path().boundingRect().x() == doctest::Approx(original_x));
        CHECK(item.pos().x() == doctest::Approx(frame * 10));
        CHECK(polyline->version == version);
        CHECK(canvas.refreshes == refreshes);
        CHECK(canvas.previews == frame);
        CHECK(copies == 0);
    }
    const auto preview_version = polyline->version;
    REQUIRE(select.on_release(130, 0, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(undo.can_undo());
    CHECK(polyline->version > preview_version);
    CHECK(polyline->points.front().x == doctest::Approx(80));
    CHECK(copies == 2); // Apenas os snapshots anterior e posterior do comando de desfazer.
    const auto committed_version = polyline->version;
    REQUIRE(undo.undo());
    canvas.refresh_entities();
    CHECK(polyline->version > committed_version);
    CHECK(polyline->points.front().x == doctest::Approx(0));
    CHECK(item.path().boundingRect().x() == doctest::Approx(original_x));
    const auto undo_version = polyline->version;
    REQUIRE(undo.redo());
    canvas.refresh_entities();
    CHECK(polyline->version > undo_version);
    CHECK(item.path().boundingRect().x() == doctest::Approx(original_x + 80));

    select.deactivate();
    CHECK(polyline->points.front().x == doctest::Approx(80));
}

TEST_CASE("performance: cancelar arrasto limpa a pre-visualizacao sem editar a geometria") {
    cnc::cad::Document document;
    cnc::UndoStack undo;
    cnc::tools::ToolContext context(document, undo);
    LiveTransformCanvas canvas;
    context.canvas = &canvas;
    cnc::tools::SelectTool select(context);
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->width = 100;
    rectangle->height = 40;
    document.add_entity(rectangle);
    document.set_selected(rectangle, true);
    cnc::vp::EntityItem item(rectangle, document.material.height);
    canvas.item = &item;
    const QPainterPath original = item.path();
    const auto version = rectangle->version;
    REQUIRE(select.on_press(50, 0, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(select.on_move(65, 0, Qt::NoModifier));
    REQUIRE(select.on_move(80, 0, Qt::NoModifier));
    CHECK(item.path() == original);
    CHECK(item.pos().x() == doctest::Approx(30));
    REQUIRE(select.on_key(Qt::Key_Escape));
    CHECK(rectangle->version == version);
    CHECK(item.pos().x() == doctest::Approx(0));
    CHECK(item.path() == original);
    CHECK_FALSE(undo.can_undo());
}
