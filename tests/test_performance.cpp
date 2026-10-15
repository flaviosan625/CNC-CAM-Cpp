// Medicao opt-in: CNC_PERF_RUN=1 cnc_tests --test-case="performance: projeto real*"
// Nao grava o projeto e nao usa limites de tempo dependentes da maquina.
#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTest>

#include "geo/types.hpp"
#include "cad/document.hpp"
#include "tools/draw_tools.hpp"
#include "viewport/canvas_2d.hpp"
#include "viewport/entity_items.hpp"

namespace {

class CountedRectangle : public cnc::cad::Rectangle {
public:
    mutable int point_builds = 0;
    cnc::Points to_points(int segments = 64) const override {
        ++point_builds;
        return Rectangle::to_points(segments);
    }
};

template <typename Fn>
void measure(cnc::Json& measurements, const char* stage, int operations, Fn&& action) {
    QElapsedTimer timer;
    timer.start();
    action();
    const double ms = timer.nsecsElapsed() / 1.e6;
    measurements[stage] = {{"total_ms", ms}, {"operations", operations},
                           {"ms_per_operation", ms / operations}};
}

std::vector<int> copy_counts() {
    const QString configured = qEnvironmentVariable("CNC_PERF_COPIES", "1,4,16");
    std::vector<int> out;
    for (const QString& value : configured.split(',')) {
        bool valid = false;
        const int count = value.trimmed().toInt(&valid);
        if (valid && count >= 1 && count <= 256) out.push_back(count);
    }
    return out;
}

// Todas as copias ficam no mesmo documento. Cada bloco mantem a geometria e os
// grupos do projeto; os grupos de um bloco nao selecionam pecas de outro bloco.
void duplicate_drawing(cnc::cad::Document& document, int copies) {
    const auto original = document.entities;
    const double width = document.material.width;
    const double height = document.material.height;
    const int columns = static_cast<int>(std::ceil(std::sqrt(copies)));
    const int rows = (copies + columns - 1) / columns;
    document.entities.reserve(original.size() * copies);
    for (int copy = 1; copy < copies; ++copy) {
        auto entities = cnc::tools::clone_entities(original);
        const double dx = (copy % columns) * (width + 25.0);
        const double dy = (copy / columns) * (height + 25.0);
        for (const auto& entity : entities) {
            entity->translate(dx, dy);
            document.entities.push_back(entity);
        }
    }
    document.material.width = columns * (width + 25.0) - 25.0;
    document.material.height = rows * (height + 25.0) - 25.0;
    document.toolpaths.clear();
    document.clear_selection();
}

std::vector<cnc::Pt> cursor_queries(const cnc::cad::Document& document) {
    std::vector<cnc::Pt> out;
    for (const auto& entity : document.entities) {
        if (const auto* polyline = cnc::cad::as<cnc::cad::Polyline>(*entity);
            polyline && polyline->points.size() > 1) {
            out.push_back(polyline->points.front());
            if (out.size() == 6) break;
        }
    }
    return out;
}

// Preserva a busca por forca bruta anterior para validar que o indice nao muda
// o resultado do encaixe durante o benchmark do arquivo real.
bool legacy_moving_hit(const cnc::cad::Document& document,
                       const std::vector<cnc::cad::EntityPtr>& moving,
                       double dx, double dy, double px_per_mm) {
    const double radius = 12.0 / px_per_mm;
    std::set<std::string> ids;
    std::vector<cnc::vp::NotablePoint> mobile, fixed;
    for (const auto& entity : moving) {
        ids.insert(entity->id);
        for (const auto& [point, kind] : cnc::vp::entity_points(*entity))
            mobile.push_back({{point.x + dx, point.y + dy}, kind});
    }
    if (mobile.empty()) return false;
    cnc::BBox bounds{mobile.front().first.x, mobile.front().first.y,
                     mobile.front().first.x, mobile.front().first.y};
    for (const auto& [point, kind] : mobile) {
        bounds.x0 = std::min(bounds.x0, point.x); bounds.x1 = std::max(bounds.x1, point.x);
        bounds.y0 = std::min(bounds.y0, point.y); bounds.y1 = std::max(bounds.y1, point.y);
    }
    for (const auto& entity : document.entities) {
        if (ids.count(entity->id)) continue;
        const auto* layer = document.get_layer(entity->layer);
        if (layer && !layer->visible) continue;
        const auto box = entity->bounding_box();
        if (box.x1 + radius < bounds.x0 || box.x0 - radius > bounds.x1 ||
            box.y1 + radius < bounds.y0 || box.y0 - radius > bounds.y1) continue;
        const auto points = cnc::vp::entity_points(*entity);
        fixed.insert(fixed.end(), points.begin(), points.end());
    }
    const auto& material = document.material;
    const std::array<cnc::Pt, 9> stock_points = {{{0.0, 0.0}, {material.width, 0.0},
        {0.0, material.height}, {material.width, material.height},
        {material.width / 2, material.height / 2}, {material.width / 2, 0.0},
        {material.width / 2, material.height}, {0.0, material.height / 2},
        {material.width, material.height / 2}}};
    for (const cnc::Pt point : stock_points)
        fixed.push_back({point, cnc::vp::SnapKind::Material});
    if (const auto margin = material.margin_rect()) {
        const double x0 = margin->x0, x1 = margin->x1, y0 = margin->y0, y1 = margin->y1;
        const std::array<cnc::Pt, 8> margin_points = {{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1},
            {(x0 + x1) / 2, y0}, {(x0 + x1) / 2, y1},
            {x0, (y0 + y1) / 2}, {x1, (y0 + y1) / 2}}};
        for (const cnc::Pt point : margin_points)
            fixed.push_back({point, cnc::vp::SnapKind::Material});
    }
    for (const auto& [mobile_point, mobile_kind] : mobile)
        for (const auto& [fixed_point, fixed_kind] : fixed)
            if (cnc::hypot(fixed_point.x - mobile_point.x, fixed_point.y - mobile_point.y) <= radius)
                return true;
    return false;
}

void send_move(QWidget* viewport, const QPoint& position, Qt::MouseButtons buttons) {
    QMouseEvent event(QEvent::MouseMove, QPointF(position),
                      QPointF(viewport->mapToGlobal(position)),
                      Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
}

cnc::vp::EntityItem* item_for(cnc::vp::Canvas2D& canvas, const cnc::cad::EntityPtr& entity) {
    for (auto* item : canvas.scene()->items())
        if (auto* drawing = dynamic_cast<cnc::vp::EntityItem*>(item); drawing && drawing->entity() == entity)
            return drawing;
    return nullptr;
}

}  // namespace

TEST_CASE("performance: selecao reutiliza caminhos e edicao invalida apenas a peca alterada") {
    auto first = std::make_shared<CountedRectangle>();
    first->rotation = 12.0;
    auto second = std::make_shared<CountedRectangle>();
    second->rotation = 24.0;
    cnc::vp::EntityItem first_item(first, 2440.0);
    cnc::vp::EntityItem second_item(second, 2440.0);
    const int first_builds = first->point_builds;
    const int second_builds = second->point_builds;
    REQUIRE(first_builds > 0);
    REQUIRE(second_builds > 0);
    const QPainterPath before = first_item.path();

    for (int frame = 0; frame < 50; ++frame) {
        first->selected = frame % 2 == 0;
        first_item.refresh();
        second_item.refresh();
    }
    CHECK(first->point_builds == first_builds);
    CHECK(second->point_builds == second_builds);
    CHECK(first_item.path() == before);

    first->translate(13.0, 7.0);
    first_item.refresh();
    second_item.refresh();
    CHECK(first->point_builds == first_builds + 1);
    CHECK(second->point_builds == second_builds);
    CHECK(first_item.path().boundingRect().x() == doctest::Approx(before.boundingRect().x() + 13.0));
    CHECK(first_item.path().boundingRect().y() == doctest::Approx(before.boundingRect().y() - 7.0));
}

TEST_CASE("performance: inserir e remover pecas preserva itens existentes e atualiza o material") {
    cnc::cad::Document document;
    auto first = std::make_shared<cnc::cad::Rectangle>();
    auto second = std::make_shared<cnc::cad::Circle>();
    document.add_entity(first);
    document.add_entity(second);
    cnc::vp::Canvas2D canvas;
    canvas.set_document(&document);
    auto* first_item = item_for(canvas, first);
    auto* second_item = item_for(canvas, second);
    REQUIRE(first_item != nullptr);
    REQUIRE(second_item != nullptr);
    const double original_y = first_item->path().boundingRect().y();

    auto third = std::make_shared<cnc::cad::Rectangle>();
    document.add_entity(third);
    canvas.refresh_entities();
    CHECK(item_for(canvas, first) == first_item);
    CHECK(item_for(canvas, second) == second_item);
    REQUIRE(item_for(canvas, third) != nullptr);

    document.remove_entity(second->id);
    canvas.refresh_entities();
    CHECK(item_for(canvas, first) == first_item);
    CHECK(item_for(canvas, second) == nullptr);
    REQUIRE(item_for(canvas, third) != nullptr);

    document.material.height += 500.0;
    canvas.setup_material(document.material.width, document.material.height);
    REQUIRE(item_for(canvas, first) == first_item);
    CHECK(first_item->path().boundingRect().y() == doctest::Approx(original_y + 500.0));
}

TEST_CASE("performance: projeto real com muitas pecas no mesmo documento") {
    if (!qEnvironmentVariableIsSet("CNC_PERF_RUN")) return;
    const QString project = qEnvironmentVariable(
        "CNC_PERF_PROJECT", "D:/CORTES ROUTER/UNIPAR/UNIPAR_TETO_2500MM.cnc");
    const std::filesystem::path path(project.toStdWString());
    REQUIRE_MESSAGE(std::filesystem::exists(path), "CNC_PERF_PROJECT deve apontar para um projeto existente");
    const auto counts = copy_counts();
    REQUIRE_FALSE(counts.empty());
    const int iterations = std::clamp(qEnvironmentVariableIntValue("CNC_PERF_ITERATIONS"), 1, 100);

    for (const int copies : counts) {
        cnc::Json measurements;
        cnc::cad::Document document;
        measure(measurements, "load", 1, [&] { document.load(path); });
        const size_t originals = document.entities.size();
        REQUIRE(originals > 0);
        const auto original_bounds = document.bounding_box();
        REQUIRE(original_bounds.has_value());
        measure(measurements, "duplicate", copies, [&] { duplicate_drawing(document, copies); });
        REQUIRE(document.entities.size() == originals * copies);

        size_t points = 0, texts = 0;
        for (const auto& entity : document.entities) {
            if (const auto* polyline = cnc::cad::as<cnc::cad::Polyline>(*entity)) points += polyline->points.size();
            if (cnc::cad::is_a<cnc::cad::Text>(*entity)) ++texts;
        }

        cnc::UndoStack undo;
        cnc::tools::ToolContext context(document, undo);
        cnc::tools::SelectTool select(context);
        // Destruir o canvas antes do documento/contexto de ferramentas.
        cnc::vp::Canvas2D canvas;
        context.canvas = &canvas;
        canvas.resize(1280, 800);
        canvas.show();
        QApplication::processEvents();
        measure(measurements, "first_refresh", 1, [&] { canvas.set_document(&document); });
        canvas.set_tool(&select);
        canvas.fit_view();
        QApplication::processEvents();

        const auto queries = cursor_queries(document);
        REQUIRE_FALSE(queries.empty());
        const auto moving_it = std::find_if(document.entities.begin(), document.entities.end(), [](const auto& entity) {
            return cnc::cad::is_a<cnc::cad::Polyline>(*entity);
        });
        REQUIRE(moving_it != document.entities.end());
        const auto moving = *moving_it;
        const auto moving_before = moving->copy();
        measure(measurements, "unchanged_refresh", 10 * iterations, [&] {
            for (int i = 0; i < 10 * iterations; ++i) canvas.refresh_entities();
        });
        measure(measurements, "selection_refresh", 10 * iterations, [&] {
            for (int i = 0; i < 10 * iterations; ++i) {
                document.set_selected(moving, i % 2 == 0);
                canvas.refresh_entities();
            }
        });
        document.clear_selection();
        measure(measurements, "single_piece_edit", 10 * iterations, [&] {
            for (int i = 0; i < 10 * iterations; ++i) {
                moving->translate(0.25, 0.0);
                canvas.refresh_entities();
            }
        });
        moving->restore_from(*moving_before);
        canvas.refresh_entities();

        // Zoom constante nas consultas geometricas permite comparar escalabilidade
        // sem mudar a tolerancia em mm quando aumentamos o tamanho da chapa.
        size_t snap_hits = 0, moving_hits = 0;
        measure(measurements, "cursor_snap", static_cast<int>(queries.size()) * iterations, [&] {
            for (int i = 0; i < iterations; ++i)
                for (const auto& p : queries) if (canvas.snap.snap(p.x + 0.125, p.y + 0.125, 2.0)) ++snap_hits;
        });
        std::vector<bool> expected_moving_hits;
        for (int i = 0; i < 3 * iterations; ++i)
            expected_moving_hits.push_back(legacy_moving_hit(document, {moving}, 8.0 + i * .25, 5.0, 2.0));
        for (const bool expected : expected_moving_hits) if (expected) ++moving_hits;
        measure(measurements, "moving_snap", 3 * iterations, [&] {
            for (int i = 0; i < 3 * iterations; ++i) {
                const auto result = canvas.snap.snap_moving({moving}, 8.0 + i * .25, 5.0, 2.0);
                CHECK(result.hit.has_value() == expected_moving_hits[static_cast<size_t>(i)]);
                CHECK(std::isfinite(result.dx));
                CHECK(std::isfinite(result.dy));
            }
        });
        measure(measurements, "pick", static_cast<int>(queries.size()) * iterations, [&] {
            for (int i = 0; i < iterations; ++i)
                for (const auto& p : queries) (void)canvas.entity_at(p.x, p.y);
        });

        QImage image(canvas.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        measure(measurements, "render_whole_sheet", 3 * iterations, [&] {
            for (int i = 0; i < 3 * iterations; ++i) {
                image.fill(Qt::transparent);
                QPainter painter(&image);
                canvas.viewport()->render(&painter);
            }
        });
        CHECK_FALSE(image.isNull());

        // Zoom invalida caches em pixels. Medir somente quadros parados poderia
        // esconder uma pausa grande ao aproximar/afastar muitos vetores e textos.
        measure(measurements, "zoom_repaint", 6 * iterations, [&] {
            for (int pair = 0; pair < 3 * iterations; ++pair) {
                for (const double factor : {1.15, 1.0 / 1.15}) {
                    canvas.zoom_by(factor);
                    image.fill(Qt::transparent);
                    QPainter painter(&image);
                    canvas.viewport()->render(&painter);
                }
            }
        });

        // Eventos reais no viewport: seleciona uma aresta, arrasta e verifica undo.
        // O zoom mostra o bloco original com o mesmo tamanho em todos os cenarios.
        canvas.fitInView(QRectF(original_bounds->x0 - 25.0,
                               canvas.material_height - original_bounds->y1 - 25.0,
                               original_bounds->x1 - original_bounds->x0 + 50.0,
                               original_bounds->y1 - original_bounds->y0 + 50.0), Qt::KeepAspectRatio);
        QApplication::processEvents();
        const auto* polyline = cnc::cad::as<cnc::cad::Polyline>(*moving);
        REQUIRE(polyline != nullptr);
        REQUIRE(polyline->points.size() > 1);
        size_t edge = 0;
        double edge_length = 0.0;
        for (size_t i = 0; i + 1 < polyline->points.size(); ++i) {
            const auto a = polyline->points[i], b = polyline->points[i + 1];
            const double length = std::hypot(a.x - b.x, a.y - b.y);
            if (length > edge_length) { edge_length = length; edge = i; }
        }
        const auto a = polyline->points[edge], b = polyline->points[edge + 1];
        const QPoint start = canvas.mapFromScene(canvas.world_to_scene((a.x + b.x) / 2, (a.y + b.y) / 2));
        REQUIRE_MESSAGE(canvas.viewport()->rect().contains(start), "aresta para arrastar deve estar visivel");
        document.clear_selection();
        canvas.refresh_entities();
        const auto before_drag = document.to_json().at("entities");
        measure(measurements, "interactive_drag_8_frames", 8, [&] {
            QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start, 0);
            for (int frame = 1; frame <= 8; ++frame) {
                send_move(canvas.viewport(), start + QPoint(frame * 4, frame * -2), Qt::LeftButton);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(32, -16), 0);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });
        REQUIRE_MESSAGE(undo.can_undo(), "arrastar deve modificar a geometria e criar um comando reversivel");
        undo.undo();
        canvas.refresh_entities();
        document.clear_selection();
        // Comparar geometria via JSON evita que o benchmark aceite arrastos vazios.
        CHECK(document.to_json().at("entities") == before_drag);
        REQUIRE(document.entities.size() == originals * copies);

        cnc::Json report = {{"copies", copies}, {"entities", document.entities.size()},
                            {"polyline_points", points}, {"texts", texts},
                            {"iterations", iterations}, {"viewport", {1280, 800}},
                            {"snap_hits", snap_hits}, {"moving_snap_hits", moving_hits},
                            {"stages", measurements}};
        std::cout << "CNC_PERF " << report.dump() << '\n' << std::flush;
        canvas.set_tool(nullptr);
        canvas.set_document(nullptr);
    }
}
