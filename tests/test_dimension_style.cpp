#include <doctest.h>

#include "ui/dimension_style_dialog.hpp"
#include "cad/document.hpp"
#include "cad/entities.hpp"
#include "cad/selection.hpp"
#include "cam/job.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>

using namespace cnc;

TEST_CASE("ui: DimensionStyleDialog tem 7 abas com todos os detalhes e controles") {
    int argc = 0;
    auto* app = QApplication::instance();
    std::unique_ptr<QApplication> owned_app;
    if (!app) {
        owned_app = std::make_unique<QApplication>(argc, nullptr);
        app = owned_app.get();
    }

    cad::DimensionStyle initial;
    initial.line_color = "#FF0000";
    initial.extension_color = "#00FF00";
    initial.text_color = "#0000FF";
    initial.line_width = 0.25;
    initial.extension_gap = 2.0;
    initial.extension_overshoot = 3.0;
    initial.arrow_style = "open";
    initial.arrow_size = 4.0;
    initial.text_height = 3.5;
    initial.text_gap = 1.5;
    initial.arrow_fit = "inside";
    initial.precision = 3;
    initial.show_unit = false;
    initial.alternate_units = true;
    initial.alternate_precision = 4;
    initial.tolerance_mode = "symmetric";
    initial.tolerance_upper = 0.05;
    initial.tolerance_lower = -0.05;

    ui::DimensionStyleDialog dialog(initial, 2, nullptr);

    auto* tabs = dialog.findChild<QTabWidget*>("DimensionStyleTabs");
    REQUIRE(tabs != nullptr);
    CHECK(tabs->count() == 7);
    CHECK(tabs->tabText(0).toStdString() == "Linhas");
    CHECK(tabs->tabText(1).toStdString() == "Símbolos e setas");
    CHECK(tabs->tabText(2).toStdString() == "Texto");
    CHECK(tabs->tabText(3).toStdString() == "Ajustar");
    CHECK(tabs->tabText(4).toStdString() == "Unidades principais");
    CHECK(tabs->tabText(5).toStdString() == "Unidades alternativas");
    CHECK(tabs->tabText(6).toStdString() == "Tolerâncias");

    // Check apply_to_selected
    CHECK(dialog.apply_to_selected() == true);

    // Verify initial values in result_style
    cad::DimensionStyle res = dialog.result_style();
    CHECK(res.line_color.has_value());
    CHECK(res.line_color.value() == "#FF0000");
    CHECK(res.extension_color.has_value());
    CHECK(res.extension_color.value() == "#00FF00");
    CHECK(res.text_color.has_value());
    CHECK(res.text_color.value() == "#0000FF");
    CHECK(res.line_width == doctest::Approx(0.25));
    CHECK(res.extension_gap == doctest::Approx(2.0));
    CHECK(res.extension_overshoot == doctest::Approx(3.0));
    CHECK(res.arrow_style == "open");
    CHECK(res.arrow_size == doctest::Approx(4.0));
    CHECK(res.text_height == doctest::Approx(3.5));
    CHECK(res.text_gap == doctest::Approx(1.5));
    CHECK(res.arrow_fit == "inside");
    CHECK(res.precision == 3);
    CHECK(res.show_unit == false);
    CHECK(res.alternate_units == true);
    CHECK(res.alternate_precision == 4);
    CHECK(res.tolerance_mode == "symmetric");
    CHECK(res.tolerance_upper == doctest::Approx(0.05));

    // Modifying controls through result_style verification
    cad::DimensionStyle default_style;
    CHECK(default_style.text_height == doctest::Approx(20.0));
    CHECK(default_style.line_color.has_value());
    CHECK(default_style.line_color.value() == "#FF007F");
    ui::DimensionStyleDialog dialog_zero(default_style, 0, nullptr);
    CHECK(dialog_zero.apply_to_selected() == false);
}

TEST_CASE("cad: Dimension formata label com o estilo configurado") {
    cad::Dimension dim;
    dim.x1 = 0; dim.y1 = 0; dim.x2 = 100; dim.y2 = 0;
    dim.kind_name = "linear";

    dim.style.precision = 1;
    dim.style.show_unit = true;
    dim.style.tolerance_mode = "none";
    dim.style.alternate_units = false;
    CHECK(dim.format_label("mm", 1.0) == "100 mm");

    dim.style.tolerance_mode = "symmetric";
    dim.style.tolerance_upper = 0.2;
    CHECK(dim.format_label("mm", 1.0) == "100 mm ±0.2");

    dim.style.alternate_units = true;
    dim.style.alternate_precision = 2;
    std::string formatted = dim.format_label("mm", 1.0);
    CHECK(formatted.find("100 mm ±0.2") != std::string::npos);
    CHECK(formatted.find("pol") != std::string::npos);
}

TEST_CASE("ui: DimensionStyleDialog com 1 cota selecionada permite editar offset, prefixo e sufixo") {
    int argc = 0;
    auto* app = QApplication::instance();
    std::unique_ptr<QApplication> owned_app;
    if (!app) {
        owned_app = std::make_unique<QApplication>(argc, nullptr);
        app = owned_app.get();
    }

    cad::DimensionStyle initial;
    initial.prefix = "Ø";
    initial.suffix = " H7";
    std::optional<double> initial_offset = 25.0;

    ui::DimensionStyleDialog dialog(initial, 1, initial_offset, nullptr);
    CHECK(dialog.apply_to_selected() == true);
    CHECK(dialog.result_offset().has_value());
    CHECK(*dialog.result_offset() == doctest::Approx(25.0));

    cad::DimensionStyle res = dialog.result_style();
    CHECK(res.prefix == "Ø");
    CHECK(res.suffix == " H7");
}

TEST_CASE("cad: ensure_import_layers cria camadas Cotas e Linha de centro automaticamente") {
    cad::Document doc;
    CHECK(doc.get_layer("Cotas") == nullptr);
    CHECK(doc.get_layer("Linha de centro") == nullptr);

    doc.ensure_import_layers();
    const auto* cotas = doc.get_layer("Cotas");
    const auto* centro = doc.get_layer("Linha de centro");

    REQUIRE(cotas != nullptr);
    REQUIRE(centro != nullptr);
    CHECK(cotas->name == "Cotas");
    CHECK(centro->name == "Linha de centro");
}

TEST_CASE("cam: Bloqueio estrito impede que cotas e camada Cotas virem percurso de usinagem") {
    cad::Document doc;
    doc.ensure_import_layers();

    auto dim = std::make_shared<cad::Dimension>();
    dim->x1 = 0; dim->y1 = 0; dim->x2 = 100; dim->y2 = 0;
    dim->layer = "Cotas";
    doc.add_entity(dim);

    auto rect_on_cotas = std::make_shared<cad::Rectangle>();
    rect_on_cotas->x = 10; rect_on_cotas->y = 10;
    rect_on_cotas->width = 50; rect_on_cotas->height = 50;
    rect_on_cotas->layer = "Cotas";
    doc.add_entity(rect_on_cotas);

    auto rect_on_peca = std::make_shared<cad::Rectangle>();
    rect_on_peca->x = 200; rect_on_peca->y = 200;
    rect_on_peca->width = 80; rect_on_peca->height = 80;
    rect_on_peca->layer = "Peça";
    doc.add_entity(rect_on_peca);

    // 1. is_vector deve ser estritamente falso para Dimension e qualquer elemento na camada Cotas
    CHECK_FALSE(cad::is_vector(*dim));
    CHECK_FALSE(cad::is_vector(*rect_on_cotas));
    CHECK(cad::is_vector(*rect_on_peca));

    // 2. machinable_for deve filtrar e descartar cotas e elementos da camada Cotas
    const auto mixed = std::vector<cad::EntityPtr>{dim, rect_on_cotas, rect_on_peca};
    const auto valid = cad::machinable_for(mixed, cam::ToolpathType::Profile);
    REQUIRE(valid.size() == 1);
    CHECK(valid.front()->id == rect_on_peca->id);

    const auto only_cotas = std::vector<cad::EntityPtr>{dim, rect_on_cotas};
    CHECK(cad::machinable_for(only_cotas, cam::ToolpathType::Profile).empty());
    CHECK(cad::machinable_for(only_cotas, cam::ToolpathType::Pocket).empty());
    CHECK(cad::machinable_for(only_cotas, cam::ToolpathType::Drill).empty());

    // 3. ToolpathJob::calculate deve falhar claramente caso receba apenas entidades da camada Cotas
    auto job = std::make_shared<cam::ToolpathJob>();
    job->kind = cam::ToolpathType::Profile;
    job->name = "Perfil Teste";
    job->entity_ids = {dim->id, rect_on_cotas->id};
    doc.add_toolpath(job);

    const auto errors = doc.recalculate_toolpaths(job);
    CHECK_FALSE(errors.empty());
    CHECK(job->results.empty());
    CHECK(job->error.find("bloqueados para percursos") != std::string::npos);
}

TEST_CASE("cad: Dimension label inclui prefixo e sufixo configurados") {
    cad::Dimension dim;
    dim.x1 = 0; dim.y1 = 0; dim.x2 = 50; dim.y2 = 0;
    dim.style.precision = 0;
    dim.style.show_unit = false;
    dim.style.prefix = "Ø";
    dim.style.suffix = " mm";
    CHECK(dim.format_label("mm", 1.0) == "Ø50 mm");
}


