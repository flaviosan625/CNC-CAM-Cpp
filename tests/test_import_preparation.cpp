#include <doctest.h>

#include <algorithm>
#include <limits>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include "app/main_window.hpp"
#include "cad/geometry.hpp"
#include "cam/presets.hpp"
#include "ui/import_setup_panel.hpp"
#include "ui/layer_manager_dialog.hpp"
#include "viewport/canvas_2d.hpp"

namespace {

cnc::cam::Tool preparation_tool() {
    cnc::cam::Tool tool;
    tool.name = "Fresa teste rebaixo 4 mm";
    tool.diameter = 4.0;
    tool.tool_number = 7;
    tool.pass_depth = 2.0;
    tool.stepover = 1.5;
    tool.feed_rate = 1450.0;
    tool.plunge_rate = 350.0;
    tool.spindle_speed = 16000;
    return tool;
}

std::shared_ptr<cnc::cad::Rectangle> preparation_rectangle(double x, double y, double width,
                                                          double height) {
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->x = x;
    rectangle->y = y;
    rectangle->width = width;
    rectangle->height = height;
    return rectangle;
}

cnc::ui::ImportSetupPanel* preparation_panel(cnc::app::MainWindow& window) {
    return window.findChild<cnc::ui::ImportSetupPanel*>("ImportSetupPanel");
}

void configure_preparation(cnc::ui::ImportSetupPanel& panel, bool has_recess) {
    cnc::cad::MaterialSetup material;
    material.material_profile = "MDF";
    material.width = 1220.0;
    material.height = 2440.0;
    material.thickness = 18.0;
    material.zero_x = "left";
    material.zero_y = "bottom";
    material.zero_z = "top";
    const auto tool = preparation_tool();
    panel.begin_import(material, {tool}, tool);
    panel.set_recess_answer(has_recess);
    panel.set_recess_depth(4.5);
}

}  // namespace

TEST_CASE("import preparation: dropping an SVG separates cut layers and opens the right panel") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath("peca_com_miolo.svg");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    const QByteArray svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="200mm" height="120mm" viewBox="0 0 200 120">
        <rect x="10" y="10" width="180" height="100"/>
        <rect x="40" y="35" width="50" height="40"/>
    </svg>)";
    REQUIRE(file.write(svg) == svg.size());
    file.close();

    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    auto* canvas = window.findChild<cnc::vp::Canvas2DWidget*>();
    REQUIRE(panel != nullptr);
    REQUIRE(canvas != nullptr);
    configure_preparation(*panel, false);
    CHECK(panel->recess_answered());
    window.resize(1500, 900);
    window.show();
    QApplication::processEvents();
    canvas->files_dropped(QStringList{path}, 0.0, 0.0);
    QApplication::processEvents();
    const QString capture_directory = qEnvironmentVariable("CNC_IMPORT_CAPTURE_DIR");
    if (!capture_directory.isEmpty()) {
        REQUIRE(QDir().mkpath(capture_directory));
        CHECK(window.grab().save(QDir(capture_directory).filePath("import-workflow.png")));
    }

    const auto& document = window.document();
    CHECK(panel->isVisible());
    CHECK_FALSE(panel->recess_answered());
    CHECK(document.toolpaths.empty());
    REQUIRE(document.get_layer("CORTE_INTERNO") != nullptr);
    REQUIRE(document.get_layer("CORTE_EXTERNO") != nullptr);
    REQUIRE(document.preparation.contains("roles"));
    int external_count = 0;
    int internal_count = 0;
    for (const auto& entity : document.entities) {
        if (!entity || !entity->closed()) continue;
        REQUIRE(document.preparation.at("roles").contains(entity->id));
        if (entity->layer == "CORTE_EXTERNO") {
            ++external_count;
            CHECK(document.preparation.at("roles").at(entity->id) == "external");
        } else if (entity->layer == "CORTE_INTERNO") {
            ++internal_count;
            CHECK(document.preparation.at("roles").at(entity->id) == "internal");
        }
    }
    CHECK(external_count == 1);
    CHECK(internal_count == 1);

    cnc::cad::Document reopened;
    reopened.from_json(document.to_json());
    CHECK(reopened.preparation == document.preparation);
    CHECK(reopened.get_layer("CORTE_INTERNO") != nullptr);
    CHECK(reopened.get_layer("CORTE_EXTERNO") != nullptr);
    CHECK(reopened.toolpaths.empty());
}

TEST_CASE("import preparation: explicit recess answer and material do not restrict manual operations") {
    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    REQUIRE(panel != nullptr);
    configure_preparation(*panel, false);

    auto* answer = panel->findChild<QComboBox*>("ImportHasRecess");
    auto* apply = panel->findChild<QPushButton*>("ImportApply");
    auto* generate = panel->findChild<QPushButton*>("ImportCreateRecess");
    auto* materials = panel->findChild<QComboBox*>("ImportMaterial");
    REQUIRE(answer != nullptr);
    REQUIRE(apply != nullptr);
    REQUIRE(generate != nullptr);
    REQUIRE(materials != nullptr);
    for (const QString& name : {QString("MDF"), QString("PVC"), QString("Acrílico"),
                                QString("PS"), QString("Policarbonato")})
        CHECK(materials->findText(name) >= 0);

    panel->reset_recess_answer();
    CHECK_FALSE(panel->recess_answered());
    CHECK_FALSE(apply->isEnabled());
    answer->setCurrentIndex(answer->findData(0));
    CHECK(panel->recess_answered());
    CHECK(apply->isEnabled());
    CHECK_FALSE(generate->isEnabled());
    apply->click();

    const auto& document = window.document();
    CHECK(document.material.material_profile == "MDF");
    CHECK(document.material.width == doctest::Approx(1220.0));
    CHECK(document.material.height == doctest::Approx(2440.0));
    CHECK(document.material.thickness == doctest::Approx(18.0));
    CHECK(document.toolpaths.empty());

    bool profile_available = false;
    bool pocket_available = false;
    for (auto* button : window.findChildren<QToolButton*>()) {
        if (button->text() == "Perfil") profile_available = button->isEnabled();
        if (button->text() == "Bolso" || button->text() == "Desbaste") pocket_available = button->isEnabled();
    }
    CHECK(profile_available);
    CHECK(pocket_available);
}

TEST_CASE("import preparation: selected recess stays a draft until generated and cuts before profiles") {
    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    REQUIRE(panel != nullptr);
    configure_preparation(*panel, true);

    auto& document = window.document();
    const auto outside = preparation_rectangle(20.0, 20.0, 160.0, 100.0);
    const auto recess = preparation_rectangle(40.0, 40.0, 50.0, 35.0);
    document.add_entity(outside);
    document.add_entity(recess);
    document.set_selected(recess, true);

    auto profile = std::make_shared<cnc::cam::ToolpathJob>();
    profile->name = "Corte externo existente";
    profile->tool = preparation_tool();
    profile->entity_ids = {outside->id};
    document.add_toolpath(profile);
    REQUIRE(document.toolpaths.size() == 1);
    CHECK(document.toolpaths.front()->kind == cnc::cam::ToolpathType::Profile);

    REQUIRE(QMetaObject::invokeMethod(panel, "assignRecessRequested", Qt::DirectConnection));
    REQUIRE(document.toolpaths.size() == 2);
    const auto found = std::find_if(document.toolpaths.begin(), document.toolpaths.end(), [](const auto& job) {
        return job->kind == cnc::cam::ToolpathType::Pocket;
    });
    REQUIRE(found != document.toolpaths.end());
    const auto pocket = *found;
    CHECK_FALSE(pocket->calculated());
    CHECK(pocket->entity_ids == std::vector<std::string>{recess->id});
    CHECK(pocket->tool == preparation_tool());
    CHECK(recess->layer == "REBAIXO");
    CHECK(outside->layer != "REBAIXO");
    REQUIRE(document.preparation.contains("roles"));
    CHECK(document.preparation.at("roles").at(recess->id) == "recess");
    REQUIRE(document.preparation.contains("recess_jobs"));
    const auto& recess_jobs = document.preparation.at("recess_jobs");
    CHECK(std::find(recess_jobs.begin(), recess_jobs.end(), pocket->id) != recess_jobs.end());
    const auto* settings = std::get_if<cnc::cam::PocketSettings>(&pocket->settings);
    REQUIRE(settings != nullptr);
    CHECK(settings->start_depth == doctest::Approx(0.0));
    CHECK(settings->cut_depth == doctest::Approx(4.5));
    CHECK(settings->cut_depth < document.material.thickness);

    REQUIRE(QMetaObject::invokeMethod(panel, "createRecessRequested", Qt::DirectConnection));
    REQUIRE(document.toolpaths.size() == 2);
    CHECK(document.toolpaths.front()->id == pocket->id);
    CHECK(document.toolpaths.back()->id == profile->id);
    REQUIRE(pocket->calculated());
    CHECK(pocket->error.empty());
    double deepest_cut = std::numeric_limits<double>::max();
    for (const auto& result : pocket->results)
        for (const auto& move : result.moves)
            if (move.cutting()) deepest_cut = std::min(deepest_cut, move.z);
    CHECK(deepest_cut == doctest::Approx(-4.5));
    CHECK(deepest_cut > -document.material.thickness);

    cnc::cad::Document reopened;
    reopened.from_json(document.to_json());
    CHECK(reopened.preparation == document.preparation);
    CHECK(reopened.material.thickness == doctest::Approx(18.0));
    REQUIRE(reopened.toolpaths.size() == 2);
    CHECK(reopened.toolpaths.front()->id == pocket->id);
    CHECK(reopened.toolpaths.front()->tool == preparation_tool());
    const auto* reopened_settings = std::get_if<cnc::cam::PocketSettings>(&reopened.toolpaths.front()->settings);
    REQUIRE(reopened_settings != nullptr);
    CHECK(reopened_settings->cut_depth == doctest::Approx(4.5));
    REQUIRE(reopened.find_entity(recess->id) != nullptr);
    CHECK(reopened.find_entity(recess->id)->layer == "REBAIXO");
    CHECK(reopened.preparation.at("roles").at(recess->id) == "recess");
}

TEST_CASE("import preparation: renaming the recess layer preserves operation identity and geometry") {
    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    REQUIRE(panel != nullptr);
    configure_preparation(*panel, true);
    auto& document = window.document();
    const auto recess = preparation_rectangle(30.0, 40.0, 60.0, 45.0);
    document.add_entity(recess);
    document.set_selected(recess, true);
    REQUIRE(QMetaObject::invokeMethod(panel, "assignRecessRequested", Qt::DirectConnection));
    REQUIRE(document.toolpaths.size() == 1);
    const auto pocket_id = document.toolpaths.front()->id;
    const auto preparation_before = document.preparation;
    bool edited = false;
    QTimer::singleShot(0, &window, [&] {
        cnc::ui::LayerManagerDialog* dialog = nullptr;
        for (auto* candidate : window.findChildren<QDialog*>())
            if (auto* manager = dynamic_cast<cnc::ui::LayerManagerDialog*>(candidate)) dialog = manager;
        if (!dialog) {
            FAIL_CHECK("The layer manager dialog did not open");
            for (auto* candidate : window.findChildren<QDialog*>()) candidate->reject();
            return;
        }
        auto* list = dialog->findChild<QListWidget*>("LayerManagerList");
        auto* name = dialog->findChild<QLineEdit*>("LayerManagerCurrentName");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!list || !name || !buttons) {
            FAIL_CHECK("The layer manager did not expose its rename controls");
            dialog->reject();
            return;
        }
        const auto matches = list->findItems("REBAIXO", Qt::MatchExactly);
        if (matches.isEmpty()) {
            FAIL_CHECK("The assigned recess layer was missing");
            dialog->reject();
            return;
        }
        list->setCurrentItem(matches.front());
        name->setText("Rebaixo cliente");
        edited = true;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    REQUIRE(QMetaObject::invokeMethod(panel, "manageLayersRequested", Qt::DirectConnection));
    CHECK(edited);
    CHECK(recess->layer == "Rebaixo cliente");
    CHECK(document.get_layer("Rebaixo cliente") != nullptr);
    CHECK(document.get_layer("REBAIXO") == nullptr);
    CHECK(document.preparation == preparation_before);
    CHECK(document.preparation.at("roles").at(recess->id) == "recess");
    REQUIRE(document.toolpaths.size() == 1);
    CHECK(document.toolpaths.front()->id == pocket_id);
    CHECK(document.toolpaths.front()->entity_ids == std::vector<std::string>{recess->id});
    CHECK(recess->x == doctest::Approx(30.0));
    CHECK(recess->width == doctest::Approx(60.0));

    REQUIRE(QMetaObject::invokeMethod(panel, "createRecessRequested", Qt::DirectConnection));
    REQUIRE(document.toolpaths.size() == 1);
    CHECK(document.toolpaths.front()->calculated());
    CHECK(document.toolpaths.front()->id == pocket_id);
}

TEST_CASE("import preparation: automatic MDF cycle calculates recesses without cutting their contours through") {
    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    REQUIRE(panel != nullptr);
    configure_preparation(*panel, true);
    auto* thickness = panel->findChild<QDoubleSpinBox*>("ImportThickness");
    REQUIRE(thickness != nullptr);
    thickness->setValue(15.0);

    auto& document = window.document();
    const auto outside = preparation_rectangle(20.0, 20.0, 160.0, 100.0);
    const auto recess = preparation_rectangle(40.0, 40.0, 50.0, 35.0);
    document.add_entity(outside);
    document.add_entity(recess);
    document.preparation["roles"][outside->id] = "external";
    document.set_selected(recess, true);
    REQUIRE(QMetaObject::invokeMethod(panel, "assignRecessRequested", Qt::DirectConnection));
    REQUIRE(document.toolpaths.size() == 1);
    const auto pocket = document.toolpaths.front();
    REQUIRE(pocket->kind == cnc::cam::ToolpathType::Pocket);
    CHECK_FALSE(pocket->calculated());
    CHECK(document.material.thickness == doctest::Approx(15.0));
    document.select_all();

    window.show();
    QApplication::processEvents();
    QToolButton* automatic_navigation = nullptr;
    for (auto* button : window.findChildren<QToolButton*>())
        if (button->toolTip() == "Programação automática por material") automatic_navigation = button;
    REQUIRE(automatic_navigation != nullptr);
    automatic_navigation->click();
    QApplication::processEvents();
    auto* preset_combo = window.findChild<QComboBox*>("AutomaticCutPreset");
    auto* generate = window.findChild<QPushButton*>("AutomaticCreateCut");
    REQUIRE(preset_combo != nullptr);
    REQUIRE(generate != nullptr);
    int preset_index = -1;
    const auto& presets = cnc::cam::house_presets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (QString::fromStdString(presets[i].material).compare("MDF", Qt::CaseInsensitive) == 0 &&
            presets[i].thickness == 15.0) preset_index = static_cast<int>(i);
    REQUIRE(preset_index >= 0);
    const int combo_index = preset_combo->findData(preset_index);
    REQUIRE(combo_index >= 0);
    preset_combo->setCurrentIndex(combo_index);
    REQUIRE(generate->isEnabled());
    generate->click();

    REQUIRE(document.toolpaths.size() == 2);
    CHECK(document.toolpaths.front()->id == pocket->id);
    CHECK(pocket->calculated());
    CHECK(pocket->tool == preparation_tool());
    const auto profile = document.toolpaths.back();
    CHECK(profile->kind == cnc::cam::ToolpathType::Profile);
    CHECK(profile->entity_ids == std::vector<std::string>{outside->id});
    CHECK(std::find(profile->entity_ids.begin(), profile->entity_ids.end(), recess->id) == profile->entity_ids.end());
    REQUIRE(profile->calculated());
    CHECK(profile->error.empty());
    double deepest_cut = std::numeric_limits<double>::max();
    bool through_cut_found = false;
    for (const auto& result : profile->results) {
        for (const auto& move : result.moves) {
            if (!move.cutting()) continue;
            deepest_cut = std::min(deepest_cut, move.z);
            if (move.z <= -15.0) {
                through_cut_found = true;
                const bool inside_recess = move.x >= 40.0 && move.x <= 90.0 &&
                                           move.y >= 40.0 && move.y <= 75.0;
                CHECK_FALSE(inside_recess);
            }
        }
    }
    CHECK(through_cut_found);
    CHECK(deepest_cut == doctest::Approx(-16.0));
    CHECK(document.preparation.at("roles").at(recess->id) == "recess");
}

TEST_CASE("import preparation: quick preset chips and zero / center alignment") {
    cnc::app::MainWindow window;
    auto* panel = preparation_panel(window);
    REQUIRE(panel != nullptr);

    QPushButton* mdf15_chip = nullptr;
    for (auto* btn : panel->findChildren<QPushButton*>()) {
        if (btn->text() == "MDF 15 mm") mdf15_chip = btn;
    }
    REQUIRE(mdf15_chip != nullptr);
    mdf15_chip->click();

    auto* thickness = panel->findChild<QDoubleSpinBox*>("ImportThickness");
    REQUIRE(thickness != nullptr);
    CHECK(thickness->value() == doctest::Approx(15.0));

    auto* combo = panel->findChild<QComboBox*>("ImportPresetCombo");
    REQUIRE(combo != nullptr);
    CHECK(combo->currentData().toInt() >= 0);

    auto& doc = window.document();
    doc.material.width = 1000.0;
    doc.material.height = 800.0;
    const auto rect = preparation_rectangle(500.0, 300.0, 100.0, 80.0);
    doc.add_entity(rect);

    auto* align_zero = panel->findChild<QPushButton*>("ImportAlignZero");
    REQUIRE(align_zero != nullptr);
    align_zero->click();

    auto bbox_zero = doc.bounding_box();
    REQUIRE(bbox_zero.has_value());
    CHECK(bbox_zero->x0 == doctest::Approx(10.0));
    CHECK(bbox_zero->y0 == doctest::Approx(10.0));

    auto* center_btn = panel->findChild<QPushButton*>("ImportCenterMaterial");
    REQUIRE(center_btn != nullptr);
    center_btn->click();

    auto bbox_center = doc.bounding_box();
    REQUIRE(bbox_center.has_value());
    const double cx = (bbox_center->x0 + bbox_center->x1) / 2.0;
    const double cy = (bbox_center->y0 + bbox_center->y1) / 2.0;
    CHECK(cx == doctest::Approx(500.0));
    CHECK(cy == doctest::Approx(400.0));
}

TEST_CASE("geometry: join_open_vectors_by_layer connects open lines into closed polyline on import") {
    auto l1 = std::make_shared<cnc::cad::Line>();
    l1->x1 = 0; l1->y1 = 0; l1->x2 = 100; l1->y2 = 0; l1->layer = "CORTE";
    auto l2 = std::make_shared<cnc::cad::Line>();
    l2->x1 = 100; l2->y1 = 0; l2->x2 = 100; l2->y2 = 50; l2->layer = "CORTE";
    auto l3 = std::make_shared<cnc::cad::Line>();
    l3->x1 = 100; l3->y1 = 50; l3->x2 = 0; l3->y2 = 50; l3->layer = "CORTE";
    auto l4 = std::make_shared<cnc::cad::Line>();
    l4->x1 = 0; l4->y1 = 50; l4->x2 = 0; l4->y2 = 0; l4->layer = "CORTE";

    std::vector<cnc::cad::EntityPtr> entities = {l1, l2, l3, l4};
    auto joined = cnc::cad::join_open_vectors_by_layer(entities, 0.1);
    REQUIRE(joined.size() == 1);
    CHECK(joined.front()->closed());
    CHECK(joined.front()->layer == "CORTE");
}
