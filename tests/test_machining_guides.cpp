#include <doctest.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFrame>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QStackedWidget>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include "app/main_window.hpp"
#include "ui/machining_guide.hpp"
#include "ui/responsive.hpp"
#include "ui/theme.hpp"
#include "viewport/canvas_2d.hpp"

namespace {
const QStringList topics = {"01_usinar_vetores", "02_profundidades", "03_pontes", "04_entradas_saidas",
    "05_rampas", "06_limpar_area", "07_ataque_rampeado", "08_material", "09_ferramentas", "10_estrela"};
QImage render(cnc::ui::MachiningGuide& guide) {
    QImage image(720, 450, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    guide.resize(image.size()); guide.render(&image); return image;
}
template <typename Predicate>
bool wait_for_guide(Predicate&& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 2000) {
        QApplication::processEvents();
        QTest::qWait(5);
    }
    return predicate();
}
}

TEST_CASE("guides: all ten designs render distinct parameter-driven states") {
    QImage board(1440, 2250, QImage::Format_ARGB32_Premultiplied); board.fill(Qt::black);
    QPainter painter(&board);
    for (int i = 0; i < topics.size(); ++i) {
        CAPTURE(i);
        cnc::ui::MachiningGuide guide(topics[i]);
        const auto before = render(guide);
        CHECK_FALSE(before.isNull());
        const auto modes = cnc::ui::MachiningGuide::modeLabels(topics[i]);
        if (modes.size() > 1) {
            guide.set_mode((guide.mode() + 1) % modes.size());
            CHECK(render(guide) != before);
        } else if (i == 1) {
            guide.model()->setValue("depth", 11.0); CHECK(render(guide) != before);
        } else {
            guide.model()->setValue("points", 8); CHECK(render(guide) != before);
        }
        painter.drawImage(QPoint((i % 2) * 720, (i / 2) * 450), before);
    }
    painter.end();
    const QString output = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        CHECK(board.save(output + "/desenhos-renderizados.png"));
    }
}

TEST_CASE("guides: inside outside and on-line are geometrically distinct") {
    cnc::ui::MachiningGuide guide(topics[0]);
    guide.set_mode(0); const QRectF outside = guide.trajectory().boundingRect();
    guide.set_mode(1); const QRectF inside = guide.trajectory().boundingRect();
    guide.set_mode(2); const QRectF on = guide.trajectory().boundingRect();
    CHECK(outside.width() > on.width()); CHECK(on.width() > inside.width());
    CHECK(outside.height() > on.height()); CHECK(on.height() > inside.height());
    guide.set_mode(0); const auto narrow = guide.trajectory();
    guide.model()->setValue("diameter", 12.0);
    CHECK(guide.trajectory().boundingRect().width() > narrow.boundingRect().width());

    QPainterPath circle; circle.addEllipse(QRectF(0, 0, 100, 100));
    guide.model()->setContour(circle);
    const auto selected = guide.nominalPath();
    CHECK(guide.model()->hasSelection());
    CHECK(selected != circle);
    guide.set_mode(1);
    CHECK_FALSE(guide.trajectory().isEmpty());
}

TEST_CASE("guides: topic changes and enlarged views preserve shared parameters") {
    cnc::ui::GuideModel model;
    cnc::ui::MachiningGuide first(topics[4], nullptr, &model), enlarged(topics[4], nullptr, &model);
    first.set_mode(3); CHECK(enlarged.mode() == 3);
    first.set_kind(topics[5]); first.set_mode(1);
    first.set_kind(topics[4]); CHECK(first.mode() == 3);
    const auto old_path = enlarged.trajectory();
    model.setValue("depth", 12.0); CHECK(old_path != enlarged.trajectory());
    CHECK(first.trajectory() == enlarged.trajectory());
    first.set_kind(topics[5]); CHECK(first.mode() == 1);
}

TEST_CASE("guides: continuous zigzag and helical descent and playback lifetime") {
    cnc::ui::MachiningGuide guide(topics[4]);
    guide.set_mode(2); const auto zigzag = guide.trajectory();
    guide.set_mode(3); const auto spiral = guide.trajectory();
    CHECK(zigzag != spiral);
    for (const auto& path : {zigzag, spiral}) {
        int starts = 0;
        for (int i = 0; i < path.elementCount(); ++i)
            if (path.elementAt(i).type == QPainterPath::MoveToElement) ++starts;
        CHECK(starts == 1);
        CHECK(path.pointAtPercent(1).y() > path.pointAtPercent(0).y());
    }
    guide.show(); guide.setProgress(.1); guide.setPlaying(true);
    CHECK(guide.timerActive()); QTest::qWait(150); CHECK(guide.progress() > .1);
    guide.setPlaying(false); const double paused = guide.progress(); QTest::qWait(80); CHECK(guide.progress() == paused);
    guide.setPlaying(true); guide.hide(); CHECK_FALSE(guide.timerActive());
    guide.setProgress(.7); CHECK(guide.progress() == doctest::Approx(.7));
    guide.set_kind(topics[5]); guide.set_mode(2);
    int starts = 0;
    for (int i = 0; i < guide.trajectory().elementCount(); ++i)
        if (guide.trajectory().elementAt(i).type == QPainterPath::MoveToElement) ++starts;
    CHECK(starts == 1);
}

TEST_CASE("guides: bridge dimensions and zero count change the drawing") {
    cnc::ui::MachiningGuide guide(topics[2]);
    const auto initial = render(guide);
    guide.model()->setValue("tabCount", 0); CHECK(render(guide) != initial);
    guide.model()->setValue("tabCount", 4); guide.model()->setValue("tabHeight", 6.0); CHECK(render(guide) != initial);
    guide.model()->setValue("tabLength", 18.0); CHECK(render(guide) != initial);
}

TEST_CASE("guides: operation fields and rendered previews use the same draft") {
    cnc::app::MainWindow window; window.show(); QApplication::processEvents();
    QAction* open = nullptr;
    for (auto* action : window.findChildren<QAction*>()) if (action->text() == "Percurso de perfil") open = action;
    REQUIRE(open != nullptr);
    bool inspected = false;
    QTimer::singleShot(0, &window, [&] {
        QDialog* dialog = nullptr;
        for (auto* candidate : window.findChildren<QDialog*>()) {
            if (candidate->isVisible() && candidate->windowTitle().startsWith("Percurso de ")) {
                dialog = candidate;
                break;
            }
        }
        REQUIRE(dialog != nullptr);
        auto* profile_tabs = dialog->findChild<QTabBar*>("ProfileOptionTabs");
        REQUIRE(profile_tabs != nullptr);
        CHECK(profile_tabs->count() == 4);
        CHECK(profile_tabs->tabText(0) == "Pontes");
        CHECK(profile_tabs->tabText(1) == "Entradas");
        CHECK(profile_tabs->tabText(2) == "Rampas");
        CHECK(profile_tabs->tabText(3) == "Ordem");
        const auto group_for = [dialog](const QString& title) -> QGroupBox* {
            for (auto* group : dialog->findChildren<QGroupBox*>("CamConfigGroup"))
                if (group->property("sectionTitle").toString() == title) return group;
            return nullptr;
        };
        auto* depth_group = group_for("Profundidades de corte");
        auto* bridges_group = group_for("Pontes");
        auto* ramps_group = group_for("Rampas");
        REQUIRE(depth_group != nullptr);
        REQUIRE(bridges_group != nullptr);
        REQUIRE(ramps_group != nullptr);
        CHECK(depth_group->isVisible());
        CHECK(bridges_group->isVisible());
        CHECK_FALSE(ramps_group->isVisible());
        profile_tabs->setCurrentIndex(2);
        QApplication::processEvents();
        CHECK(depth_group->isVisible());
        CHECK_FALSE(bridges_group->isVisible());
        CHECK(ramps_group->isVisible());
        auto* ramp_guide = ramps_group->findChild<cnc::ui::MachiningGuide*>();
        auto* ramp_type = dialog->findChild<QComboBox*>("CamParameter_ramp");
        REQUIRE(ramp_guide != nullptr);
        REQUIRE(ramp_type != nullptr);
        CHECK(ramp_guide->playing());
        ramp_type->setCurrentIndex(1);
        const auto smooth_route = ramp_guide->trajectory();
        ramp_type->setCurrentIndex(2);
        CHECK(ramp_guide->mode() == 2);
        CHECK(ramp_guide->trajectory() != smooth_route);
        CHECK(dialog->findChildren<QPushButton*>("GuidePlay").isEmpty());
        CHECK(dialog->findChildren<QPushButton*>("EnlargeGuide").isEmpty());
        auto* side = dialog->findChild<QComboBox*>("CamParameter_side");
        auto* depth = dialog->findChild<QDoubleSpinBox*>("CamParameter_depth");
        REQUIRE(side != nullptr); REQUIRE(depth != nullptr);
        auto guides = dialog->findChildren<cnc::ui::MachiningGuide*>(); REQUIRE_FALSE(guides.empty());
        side->setCurrentIndex(1); depth->setValue(11.0);
        for (auto* guide : guides) {
            CHECK(guide->model()->integer("side") == 1);
            CHECK(guide->model()->number("depth") == 11.0);
        }
        auto* model = guides.first()->model(); model->setValue("side", 2); CHECK(side->currentData().toInt() == 2);
        auto* lead_angle = dialog->findChild<QDoubleSpinBox*>("CamParameter_leadAngle");
        auto* ramp_angle = dialog->findChild<QDoubleSpinBox*>("CamParameter_rampAngle");
        REQUIRE(lead_angle != nullptr); REQUIRE(ramp_angle != nullptr);
        lead_angle->setValue(38); ramp_angle->setValue(7);
        CHECK(lead_angle->value() == 38); CHECK(model->number("rampAngle") == 7);
        dialog->resize(440, 540); QApplication::processEvents();
        CHECK(dialog->width() <= 480);
        std::string width_details;
        for (auto* group : dialog->findChildren<QGroupBox*>("CamConfigGroup")) {
            if (!group->isVisible()) continue;
            width_details += group->property("sectionTitle").toString().toStdString() + "=" +
                std::to_string(group->minimumSizeHint().width()) + " ";
        }
        width_details += "tabs=" + std::to_string(profile_tabs->minimumSizeHint().width());
        INFO(width_details);
        for (auto* scroll : dialog->findChildren<QScrollArea*>())
            if (scroll->parentWidget() == dialog) CHECK(scroll->widget()->width() <= scroll->viewport()->width());
        const QString output = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
        if (!output.isEmpty()) {
            dialog->grab().save(output + "/ficha-responsiva.png");
            for (auto* scroll : dialog->findChildren<QScrollArea*>())
                if (scroll->parentWidget() == dialog)
                    scroll->verticalScrollBar()->setValue(std::max(0, profile_tabs->y() - 10));
            QApplication::processEvents();
            dialog->grab().save(output + "/ficha-4-abas.png");
        }
        inspected = true; dialog->reject();
    });
    open->trigger(); CHECK(inspected);
}

TEST_CASE("responsive: the main side panel does not force horizontal overflow") {
    cnc::app::MainWindow window; window.resize(1024, 700); window.show(); QApplication::processEvents();
    auto* scroll = window.findChild<QScrollArea*>("RightPanelScroll"); REQUIRE(scroll != nullptr);
    auto* stack = qobject_cast<QStackedWidget*>(scroll->widget()); REQUIRE(stack != nullptr);
    for (int width : {1024, 760}) {
        window.resize(width, 700);
        for (int page = 0; page < stack->count(); ++page) {
            CAPTURE(width); CAPTURE(page);
            stack->setCurrentIndex(page); QApplication::processEvents();
            CHECK(scroll->widget()->width() <= scroll->viewport()->width());
            CHECK(scroll->horizontalScrollBar()->maximum() == 0);
            const QString output = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
            if (!output.isEmpty() && width == 1024 && page == 1)
                window.grab().save(output + "/painel-percursos.png");
        }
    }
    window.resize(1280, 800); QApplication::processEvents();
    auto* tabs = window.findChild<QTabBar*>("ViewTabs"); REQUIRE(tabs != nullptr);
    auto* main_view = window.findChild<QStackedWidget*>("MainViewStack"); REQUIRE(main_view != nullptr);
    auto* dock = window.findChild<QDockWidget*>("ToolpathPanel"); REQUIRE(dock != nullptr);
    CHECK(tabs->count() >= 3);
    CHECK(dock->allowedAreas() == Qt::RightDockWidgetArea);
    CHECK(dock->features() == QDockWidget::NoDockWidgetFeatures);
    tabs->setCurrentIndex(2); QApplication::processEvents();
    CHECK(main_view->currentIndex() == 2);
    CHECK(scroll->isVisible());
    CHECK(window.findChild<QFrame*>("OSPreviewPanel") != nullptr);
    const QString output = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
    if (!output.isEmpty()) window.grab().save(output + "/os-setup-wizard.png");

}

TEST_CASE("guides: main panel uses the new renderer and follows selected geometry") {
    cnc::app::MainWindow window;
    window.show(); QApplication::processEvents();
    auto* preview = window.findChild<cnc::ui::MachiningGuide*>("SelectionGuide");
    auto* thumbnail = window.findChild<cnc::ui::MachiningGuide*>("MaterialGuideThumbnail");
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(preview != nullptr); REQUIRE(thumbnail != nullptr); REQUIRE(canvas != nullptr);
    auto* scroll = window.findChild<QScrollArea*>("RightPanelScroll"); REQUIRE(scroll != nullptr);
    auto* stack = qobject_cast<QStackedWidget*>(scroll->widget()); REQUIRE(stack != nullptr);
    REQUIRE(window.findChild<QPushButton*>("OpenGuideGallery") != nullptr);
    CHECK(preview->kind() == "08_material");
    CHECK(thumbnail->property("compact").toBool());
    stack->setCurrentIndex(0); QApplication::processEvents();
    CHECK(preview->isVisible());
    CHECK(preview->model()->number("width") == 1220.0);
    auto* document = canvas->document(); REQUIRE(document != nullptr);
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->width = 160; rectangle->height = 80;
    document->add_entity(rectangle); document->set_selected(rectangle, true);
    canvas->refresh_selection_handles();
    REQUIRE(wait_for_guide([&] { return preview->model()->contour().boundingRect().width() == 160; }));
    CHECK(preview->kind() == "01_usinar_vetores");
    CHECK(preview->model()->hasSelection());
    CHECK(preview->model()->contour().boundingRect().width() == 160);
    CHECK(preview->mode() == 2);
    int rebuilds = 0;
    QObject::connect(preview->model(), &cnc::ui::GuideModel::changed, &window, [&] { ++rebuilds; });
    rectangle->width = 200; rectangle->touch(); canvas->refresh_selection_handles();
    rectangle->width = 240; rectangle->touch(); canvas->refresh_selection_handles();
    CHECK(rebuilds == 0);
    REQUIRE(wait_for_guide([&] { return preview->model()->contour().boundingRect().width() == 240; }));
    CHECK(rebuilds == 1);
    CHECK(preview->model()->contour().boundingRect().width() == 240);
    // Hidden guides stay unchanged until their page opens, then show the latest
    // document state immediately without work on every selection notification.
    stack->setCurrentIndex(1); QApplication::processEvents();
    CHECK(thumbnail->model()->number("width") == 1220.0);
    rectangle->width = 320; rectangle->touch(); canvas->refresh_selection_handles();
    QTest::qWait(50);
    CHECK(rebuilds == 1);
    stack->setCurrentIndex(0); QApplication::processEvents();
    CHECK(preview->model()->contour().boundingRect().width() == 320);
    canvas->refresh_entities();
    auto* tool = canvas->tool(); REQUIRE(tool != nullptr);
    const int rebuilds_before_drag = rebuilds;
    const double initial_left = preview->model()->contour().boundingRect().left();
    REQUIRE(tool->on_press(160, 0, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(tool->active_mode() == "move");
    REQUIRE(tool->on_move(180, 10, Qt::ShiftModifier));
    QTest::qWait(50);
    REQUIRE(tool->on_move(190, 20, Qt::ShiftModifier));
    QTest::qWait(50);
    CHECK(rebuilds == rebuilds_before_drag);
    CHECK(preview->model()->contour().boundingRect().left() == initial_left);
    REQUIRE(tool->on_release(190, 20, Qt::LeftButton, Qt::ShiftModifier));
    REQUIRE(wait_for_guide([&] {
        return preview->model()->contour().boundingRect().left() == initial_left + 30;
    }));
    CHECK(rebuilds == rebuilds_before_drag + 1);
    CHECK(preview->model()->contour().boundingRect().left() == doctest::Approx(initial_left + 30));
    // Cancel also resumes the deferred guide and restores its original contour.
    const QPainterPath before_cancel = preview->model()->contour();
    REQUIRE(tool->on_press(190, 20, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(tool->on_move(220, 30, Qt::ShiftModifier));
    QTest::qWait(50);
    CHECK(preview->model()->contour() == before_cancel);
    tool->deactivate();
    QTest::qWait(50);
    CHECK(preview->model()->contour() == before_cancel);
    document->clear_selection(); canvas->refresh_selection_handles();
    REQUIRE(wait_for_guide([&] { return !preview->model()->hasSelection(); }));
    CHECK(preview->kind() == "08_material");
    CHECK_FALSE(preview->model()->hasSelection());
    stack->setCurrentIndex(1); QApplication::processEvents();
    const auto before = thumbnail->grab().toImage();
    document->material.thickness = 36; canvas->refresh_selection_handles();
    REQUIRE(wait_for_guide([&] { return thumbnail->model()->number("thickness") == 36; }));
    CHECK(thumbnail->model()->number("thickness") == 36);
    CHECK(thumbnail->grab().toImage() != before);
    CHECK(thumbnail->width() == 86); CHECK(thumbnail->height() == 68);
}

TEST_CASE("responsive: dialogs and icon flows fit short narrow screens") {
    cnc::ui::ResponsiveDialog dialog;
    auto* form = new QFormLayout(&dialog);
    for (int i = 0; i < 30; ++i) form->addRow("Parâmetro " + QString::number(i), new QDoubleSpinBox(&dialog));
    dialog.resize(500, 400); dialog.show(); QApplication::processEvents();
    CHECK(dialog.findChild<QScrollArea*>("ResponsiveDialogScroll") != nullptr);
    CHECK(dialog.height() <= dialog.screen()->availableGeometry().height());
    CHECK(dialog.width() <= 420);
    CHECK(dialog.geometry().right() >= dialog.screen()->availableGeometry().right() - 10);
    QWidget panel; auto* flow = new cnc::ui::FlowLayout;
    panel.setLayout(flow);
    for (int i = 0; i < 10; ++i) { auto* label = new QLabel("X", &panel); label->setFixedSize(38, 45); flow->addWidget(label); }
    flow->setGeometry(QRect(0, 0, 140, 400));
    for (int i = 0; i < flow->count(); ++i) CHECK(flow->itemAt(i)->geometry().right() < 140);
    CHECK(flow->heightForWidth(140) > flow->heightForWidth(280));
}

TEST_CASE("os_drawing: high-end technical drawing matches card 08 and real vectors") {
    cnc::cad::MaterialSetup material;
    material.width = 600.0;
    material.height = 400.0;
    material.thickness = 18.0;
    material.clearance_z = 10.0;
    material.zero_x = "left";
    material.zero_y = "bottom";
    material.zero_z = "top";

    cnc::cam::Tool tool;
    tool.diameter = 6.0;

    cnc::cam::ToolpathJob job;
    job.tool = tool;

    cnc::cam::Toolpath path;
    path.moves = {
        {cnc::cam::MoveKind::Rapid, 50.0, 50.0, 5.0, 0.0},
        {cnc::cam::MoveKind::Plunge, 50.0, 50.0, -18.0, 0.0},
        {cnc::cam::MoveKind::Cut, 450.0, 50.0, -18.0, 0.0},
        {cnc::cam::MoveKind::Cut, 450.0, 300.0, -18.0, 0.0},
        {cnc::cam::MoveKind::Cut, 50.0, 300.0, -18.0, 0.0},
        {cnc::cam::MoveKind::Cut, 50.0, 50.0, -18.0, 0.0},
        {cnc::cam::MoveKind::Rapid, 50.0, 50.0, 5.0, 0.0},
        {cnc::cam::MoveKind::Rapid, 150.0, 120.0, 5.0, 0.0},
        {cnc::cam::MoveKind::Plunge, 150.0, 120.0, -10.0, 0.0},
        {cnc::cam::MoveKind::Cut, 350.0, 120.0, -10.0, 0.0},
        {cnc::cam::MoveKind::Cut, 350.0, 230.0, -10.0, 0.0},
        {cnc::cam::MoveKind::Cut, 150.0, 230.0, -10.0, 0.0},
        {cnc::cam::MoveKind::Cut, 150.0, 120.0, -10.0, 0.0},
        {cnc::cam::MoveKind::Rapid, 150.0, 120.0, 5.0, 0.0}
    };

    cnc::cam::SetupSheetOperation op;
    op.path = &path;
    op.job = &job;
    std::vector<cnc::cam::SetupSheetOperation> ops = {op};

    cnc::app::JobPreviewOptions options;
    QImage img = cnc::app::make_job_layout_preview(ops, &material, options);
    CHECK_FALSE(img.isNull());
    // The drawing is supersampled so the dimensions stay sharp on the sheet.
    CHECK(img.width() == qRound(options.width * options.supersample));
    // The canvas grows to fit the projected stock, within the given bounds.
    CHECK(img.height() >= qRound(options.min_height * options.supersample));
    CHECK(img.height() <= qRound(options.max_height * options.supersample));

    // A per-operation sheet isolates one toolpath and may grow taller still.
    const std::vector<QImage> per_operation = cnc::app::make_operation_previews(ops, &material);
    REQUIRE(per_operation.size() == 1);
    CHECK(per_operation[0].width() == img.width());

    const QString artifact_dir = "C:/Users/flavi/.gemini/antigravity/brain/f7b2d311-6692-4dee-acfa-d51ba77741b0";
    img.save(artifact_dir + "/ordem_de_servico_desenho_novo.png");
}

TEST_CASE("desenho 3D ampliado e visivel sem dead space") {
    cnc::cad::MaterialSetup material;
    material.width = 1220.0;
    material.height = 2440.0;
    material.thickness = 18.0;
    material.material_profile = "mdf";
    material.zero_z = "top";

    cnc::cam::Tool tool;
    tool.diameter = 4.0;
    tool.pass_depth = 4.0;

    cnc::cam::ToolpathJob job;
    job.tool = tool;

    cnc::cam::Toolpath rect_path;
    rect_path.name = "Perfil Retangular";
    rect_path.moves = {
        {cnc::cam::MoveKind::Rapid, 156.4, 237.1, 5.0, 0.0},
        {cnc::cam::MoveKind::Plunge, 156.4, 237.1, -11.0, 0.0},
        {cnc::cam::MoveKind::Cut, 650.0, 237.1, -11.0, 0.0},
        {cnc::cam::MoveKind::Cut, 650.0, 1050.0, -11.0, 0.0},
        {cnc::cam::MoveKind::Cut, 156.4, 1050.0, -11.0, 0.0},
        {cnc::cam::MoveKind::Cut, 156.4, 237.1, -11.0, 0.0},
        {cnc::cam::MoveKind::Rapid, 156.4, 237.1, 5.0, 0.0}
    };

    cnc::cam::Toolpath circle_path;
    circle_path.name = "Perfil Circular";
    const double cx = 610.0;
    const double cy = 1750.0;
    const double rx = 453.6;
    const double ry = 453.0;
    circle_path.moves.push_back({cnc::cam::MoveKind::Rapid, cx + rx, cy, 5.0, 0.0});
    circle_path.moves.push_back({cnc::cam::MoveKind::Plunge, cx + rx, cy, -11.0, 0.0});
    for (int deg = 5; deg <= 360; deg += 5) {
        const double rad = deg * 3.141592653589793 / 180.0;
        circle_path.moves.push_back({cnc::cam::MoveKind::Cut, cx + rx * std::cos(rad), cy + ry * std::sin(rad), -11.0, 0.0});
    }
    circle_path.moves.push_back({cnc::cam::MoveKind::Rapid, cx + rx, cy, 5.0, 0.0});

    std::vector<cnc::cam::SetupSheetOperation> ops = {
        {&rect_path, &job},
        {&circle_path, &job}
    };

    cnc::app::JobPreviewOptions options;
    QImage img = cnc::app::make_job_layout_preview(ops, &material, options);
    CHECK_FALSE(img.isNull());

    const QString artifact_dir = "C:/Users/flavi/.gemini/antigravity/brain/f7b2d311-6692-4dee-acfa-d51ba77741b0";
    img.save(artifact_dir + "/desenho_3d_ampliado_preview.png");

    // Also render a realistic mockup of the Central OS dialog showing the enlarged 3D preview
    // occupying the right panel properly (scaled with AspectPixmapLabel).
    QImage dialog_mock(1024, 725, QImage::Format_ARGB32_Premultiplied);
    dialog_mock.fill(QColor("#0f1722"));
    QPainter painter(&dialog_mock);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Title bar
    painter.setPen(QColor("#8cc8ff"));
    painter.setFont(QFont("Segoe UI", 14, QFont::Bold));
    painter.drawText(16, 26, "CENTRAL OS · SETUP EM 5 ETAPAS");
    painter.setPen(QColor("#7e92a8"));
    painter.setFont(QFont("Segoe UI", 8.5));
    painter.drawText(16, 42, "Confira material, origem e ferramentas; o desenho acompanha os percursos calculados.");
    painter.drawText(QRectF(700, 14, 308, 20), Qt::AlignRight | Qt::AlignVCenter, "1 operação(ões) calculada(s)");

    // Step tabs
    const std::array<QString, 5> steps = {"1 · Material", "2 · Origem", "3 · Ferramentas", "4 · Percurso", "5 · Identificação / PDF"};
    int tab_x = 16;
    for (size_t i = 0; i < steps.size(); ++i) {
        QRectF tab_r(tab_x, 52, 195, 28);
        painter.setBrush(i == 0 ? QColor("#1d4567") : QColor("#1a2939"));
        painter.setPen(QPen(i == 0 ? QColor("#168be0") : QColor("#344a62"), 1.0));
        painter.drawRect(tab_r);
        painter.setPen(i == 0 ? QColor("#f0f7ff") : QColor("#a9bfd6"));
        painter.setFont(QFont("Segoe UI", 8.5, i == 0 ? QFont::Bold : QFont::Normal));
        painter.drawText(tab_r, Qt::AlignCenter, steps[i]);
        tab_x += 198;
    }

    // Left pane (Step 1 info)
    const QRectF left_pane(16, 88, 380, 570);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Qt::NoPen);
    painter.setFont(QFont("Segoe UI", 9.5, QFont::Bold));
    painter.setPen(QColor("#cbd5e1"));
    painter.drawText(24, 110, "1 · Material da chapa");
    painter.setFont(QFont("Segoe UI", 8.5));
    const std::array<std::pair<QString, QString>, 4> mat_info = {{
        {"Material", "mdf"},
        {"Dimensões X × Y × Z", "1220 × 2440 × 18 mm"},
        {"Área", "2.977 m²"},
        {"Margem de segurança", "0 mm"}
    }};
    int info_y = 135;
    for (const auto& item : mat_info) {
        painter.setPen(QColor("#7e92a8"));
        painter.drawText(24, info_y, item.first);
        painter.setPen(QColor("#f1f5f9"));
        painter.drawText(170, info_y, item.second);
        info_y += 24;
    }
    painter.setPen(QColor("#7e92a8"));
    painter.drawText(24, info_y + 10, "Use a mesma espessura cadastrada para a chapa carregada na máquina.");
    QRectF edit_btn(24, info_y + 35, 360, 32);
    painter.setBrush(QColor("#1e293b"));
    painter.setPen(QPen(QColor("#475569"), 1.0));
    painter.drawRoundedRect(edit_btn, 4, 4);
    painter.setPen(QColor("#f8fafc"));
    painter.drawText(edit_btn, Qt::AlignCenter, "Editar material e dimensões...");

    // Right pane: OSPreviewPanel
    const QRectF right_pane(406, 88, 602, 570);
    painter.setBrush(QColor("#14202d"));
    painter.setPen(QPen(QColor("#304861"), 1.0));
    painter.drawRoundedRect(right_pane, 8, 8);

    painter.setPen(QColor("#e2e8f0"));
    painter.setFont(QFont("Segoe UI", 9.5, QFont::Bold));
    painter.drawText(418, 110, "DESENHO 3D · PERCURSO REAL");

    // image_frame inside right pane - with AspectPixmapLabel expanding to fill the frame
    const QRectF image_frame(416, 120, 582, 380);
    painter.setBrush(QColor("#0b1521"));
    painter.setPen(QPen(QColor("#344a62"), 1.0));
    painter.drawRoundedRect(image_frame, 6, 6);

    // Scale the 3D drawing into image_frame with Qt::KeepAspectRatio
    const QRectF preview_rect(image_frame.adjusted(4, 4, -4, -4));
    const double scale = std::min(preview_rect.width() / img.width(), preview_rect.height() / img.height());
    const double draw_w = img.width() * scale;
    const double draw_h = img.height() * scale;
    const QRectF target_r(preview_rect.left() + (preview_rect.width() - draw_w) / 2.0,
                          preview_rect.top() + (preview_rect.height() - draw_h) / 2.0,
                          draw_w, draw_h);
    painter.drawImage(target_r, img);

    // Dimension card below image
    const QRectF dim_card(416, 510, 582, 38);
    painter.setBrush(QColor("#172433"));
    painter.setPen(QPen(QColor("#344a62"), 1.0));
    painter.drawRoundedRect(dim_card, 5, 5);
    painter.setFont(QFont("Segoe UI", 8.5, QFont::Bold));
    painter.setPen(QColor("#f8fafc"));
    painter.drawText(426, 526, "mdf · 1220 × 2440 × 18 mm · 2.977 m²");
    painter.setFont(QFont("Segoe UI", 7.8));
    painter.setPen(QColor("#8ea4ba"));
    painter.drawText(426, 540, "Origem X 0 · Y 0 · Z na face superior · Seguro 5 mm");

    // Metrics cards
    const std::array<std::tuple<QString, QString, QColor>, 3> metrics_data = {{
        {"TEMPO", "3 min 15 s", QColor("#f5a623")},
        {"CORTE", "6.26 m", QColor("#9a6bff")},
        {"Z MÁX.", "11.00 mm", QColor("#31c48d")}
    }};
    double card_x = 416.0;
    const double card_w = (582.0 - 12.0) / 3.0;
    for (const auto& [lbl, val, acc] : metrics_data) {
        QRectF c_rect(card_x, 556, card_w, 44);
        painter.setBrush(QColor("#202f41"));
        painter.setPen(QPen(QColor("#344a62"), 1.0));
        painter.drawRoundedRect(c_rect, 6, 6);
        painter.setPen(QPen(acc, 3.0));
        painter.drawLine(c_rect.left() + 6, c_rect.top(), c_rect.right() - 6, c_rect.top());
        painter.setFont(QFont("Segoe UI", 7.5, QFont::Bold));
        painter.setPen(QColor("#9db0c4"));
        painter.drawText(QRectF(c_rect.left() + 8, c_rect.top() + 4, card_w - 16, 14), lbl);
        painter.setFont(QFont("Segoe UI", 11.0, QFont::Bold));
        painter.setPen(QColor("#f0f7ff"));
        painter.drawText(QRectF(c_rect.left() + 8, c_rect.top() + 18, card_w - 16, 22), val);
        card_x += card_w + 6.0;
    }

    // Footer buttons
    painter.setFont(QFont("Segoe UI", 8.5));
    painter.setPen(QColor("#7e92a8"));
    painter.drawText(16, 695, "Confira cotas e fixação antes de usinar.");
    QRectF prev_b(680, 680, 85, 30);
    QRectF next_b(775, 680, 85, 30);
    QRectF pdf_b(870, 675, 138, 38);
    painter.setBrush(QColor("#1e293b"));
    painter.setPen(QPen(QColor("#475569"), 1.0));
    painter.drawRoundedRect(prev_b, 4, 4);
    painter.drawRoundedRect(next_b, 4, 4);
    painter.setPen(QColor("#f8fafc"));
    painter.drawText(prev_b, Qt::AlignCenter, "Etapa anterior");
    painter.drawText(next_b, Qt::AlignCenter, "Próxima etapa");
    painter.setBrush(QColor("#0284c7"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(pdf_b, 5, 5);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 9.0, QFont::Bold));
    painter.drawText(pdf_b, Qt::AlignCenter, "📄 GERAR PDF DA OS");

    dialog_mock.save(artifact_dir + "/central_os_tela_ampliada_preview.png");
}
