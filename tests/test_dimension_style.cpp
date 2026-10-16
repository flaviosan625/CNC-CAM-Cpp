#include <doctest.h>

#include "ui/dimension_style_dialog.hpp"
#include "cad/document.hpp"
#include "cad/entities.hpp"

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
    CHECK(default_style.line_color.value() == "#00e5ff");
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

