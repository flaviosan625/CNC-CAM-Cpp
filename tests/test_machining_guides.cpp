#include <doctest.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QStackedWidget>
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
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        REQUIRE(dialog != nullptr);
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
        for (auto* scroll : dialog->findChildren<QScrollArea*>())
            if (scroll->parentWidget() == dialog) CHECK(scroll->widget()->width() <= scroll->viewport()->width());
        const QString output = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
        if (!output.isEmpty()) dialog->grab().save(output + "/ficha-responsiva.png");
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
}

TEST_CASE("guides: main panel uses the new renderer and follows selected geometry") {
    cnc::app::MainWindow window;
    auto* preview = window.findChild<cnc::ui::MachiningGuide*>("SelectionGuide");
    auto* thumbnail = window.findChild<cnc::ui::MachiningGuide*>("MaterialGuideThumbnail");
    auto* canvas = window.findChild<cnc::vp::Canvas2D*>();
    REQUIRE(preview != nullptr); REQUIRE(thumbnail != nullptr); REQUIRE(canvas != nullptr);
    REQUIRE(window.findChild<QPushButton*>("OpenGuideGallery") != nullptr);
    CHECK(preview->kind() == "08_material");
    CHECK(thumbnail->property("compact").toBool());
    CHECK(thumbnail->model()->number("width") == 1220.0);
    auto* document = canvas->document(); REQUIRE(document != nullptr);
    auto rectangle = std::make_shared<cnc::cad::Rectangle>();
    rectangle->width = 160; rectangle->height = 80;
    document->add_entity(rectangle); document->set_selected(rectangle, true);
    canvas->refresh_selection_handles();
    CHECK(preview->kind() == "01_usinar_vetores");
    CHECK(preview->model()->hasSelection());
    CHECK(preview->model()->contour().boundingRect().width() == 160);
    CHECK(preview->mode() == 2);
    rectangle->width = 240; canvas->refresh_selection_handles();
    CHECK(preview->model()->contour().boundingRect().width() == 240);
    document->clear_selection(); canvas->refresh_selection_handles();
    CHECK(preview->kind() == "08_material");
    CHECK_FALSE(preview->model()->hasSelection());
    const auto before = thumbnail->grab().toImage();
    document->material.thickness = 36; canvas->refresh_selection_handles();
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
    QWidget panel; auto* flow = new cnc::ui::FlowLayout;
    panel.setLayout(flow);
    for (int i = 0; i < 10; ++i) { auto* label = new QLabel("X", &panel); label->setFixedSize(38, 45); flow->addWidget(label); }
    flow->setGeometry(QRect(0, 0, 140, 400));
    for (int i = 0; i < flow->count(); ++i) CHECK(flow->itemAt(i)->geometry().right() < 140);
    CHECK(flow->heightForWidth(140) > flow->heightForWidth(280));
}
