#include <doctest.h>

#include <cmath>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include "app/main_window.hpp"
#include "ui/machining_guide.hpp"

TEST_CASE("toolpaths: todas as fichas mostram profundidade, ferramenta e guia contextual") {
    cnc::app::MainWindow window;
    window.show();
    QApplication::processEvents();

    QMenu* operations = nullptr;
    for (QAction* top_action : window.menuBar()->actions()) {
        if (top_action->text() == "Percursos de ferramenta") {
            operations = top_action->menu();
            break;
        }
    }
    REQUIRE(operations != nullptr);

    const QStringList kinds = {
        "Percurso de perfil", "Percurso de bolso", "Percurso de furação", "Entalhe V",
        "Canais (Fluting)", "Textura", "Rasgo Keyhole", "Inlay", "Faca de arraste",
    };
    QStringList profile_order;
    for (const QString& kind : kinds) {
        QAction* open_action = nullptr;
        for (QAction* candidate : operations->actions())
            if (candidate->text() == kind) open_action = candidate;
        REQUIRE(open_action != nullptr);

        bool dialog_seen = false;
        bool depth_seen = false;
        bool tool_seen = false;
        bool guide_image_loaded = false;
        bool workspace_interactive = false;
        QTimer::singleShot(0, &window, [&] {
            QDialog* dialog = nullptr;
            for (auto* candidate : window.findChildren<QDialog*>()) {
                if (candidate->isVisible() && candidate->windowTitle().startsWith("Percurso de ")) {
                    dialog = candidate;
                    break;
                }
            }
            if (!dialog) return;
            dialog_seen = dialog->windowTitle().startsWith("Percurso de ");
            workspace_interactive = window.isEnabled();
            CHECK(dialog->width() <= 420);
            const int app_right = window.mapToGlobal(QPoint(window.width() - 1, 0)).x();
            CHECK(std::abs(dialog->geometry().right() - app_right) <= 3);
            const QString capture_dir = QString::fromUtf8(qgetenv("CNC_GUIDE_CAPTURE_DIR"));
            if (kind == "Percurso de perfil" && !capture_dir.isEmpty())
                dialog->screen()->grabWindow(0).save(capture_dir + "/percurso-right-edge.png");
            auto* scroll = dialog->findChild<QScrollArea*>();
            if (scroll && scroll->widget() && scroll->widget()->layout()) {
                auto* layout = qobject_cast<QVBoxLayout*>(scroll->widget()->layout());
                if (layout) {
                    for (int i = 0; i < layout->count(); ++i) {
                        auto* group = qobject_cast<QGroupBox*>(layout->itemAt(i)->widget());
                        if (!group || !group->isVisible()) continue;
                        if (group->title() == "Profundidades de corte") depth_seen = true;
                        if (group->title() == "Ferramenta") tool_seen = true;
                        if (kind == "Percurso de perfil") profile_order.push_back(group->title());
                    }
                }
            }
            if (kind == "Percurso de perfil") {
                auto* tabs = dialog->findChild<QTabBar*>("ProfileOptionTabs");
                REQUIRE(tabs != nullptr);
                CHECK(tabs->count() == 4);
                CHECK(tabs->tabText(0) == "Pontes");
                CHECK(tabs->tabText(1) == "Entradas");
                CHECK(tabs->tabText(2) == "Rampas");
                CHECK(tabs->tabText(3) == "Ordem");
                auto* actions = dialog->findChild<QWidget*>("ProfileBridgeActions");
                REQUIRE(actions != nullptr);
                CHECK(actions->isVisible());
                const auto bridge_buttons = actions->findChildren<QPushButton*>();
                CHECK(bridge_buttons.size() == 4);
                for (auto* button : bridge_buttons) CHECK(button->isVisible());
                for (auto* button : bridge_buttons)
                    if (button->text() == "Marcar no desenho") CHECK(button->isEnabled());
            }
            for (QComboBox* topic : dialog->findChildren<QComboBox*>("CamGuideTopic")) {
                if (topic->count() == 0) continue;
                for (auto* image : dialog->findChildren<cnc::ui::MachiningGuide*>("CamGuideImage"))
                    guide_image_loaded = guide_image_loaded || !image->grab().isNull();
            }
            dialog->reject();
        });
        open_action->trigger();
        CHECK(dialog_seen);
        CHECK(depth_seen);
        CHECK(tool_seen);
        CHECK(guide_image_loaded);
        CHECK(workspace_interactive);
    }

    const QStringList profile_sections = {
        "Profundidades de corte", "Ferramenta", "Passagens", "Usinar vetores...",
        "Último passe",
    };
    int previous = -1;
    for (const QString& expected : profile_sections) {
        const int index = profile_order.indexOf(expected);
        REQUIRE(index >= 0);
        CHECK(index > previous);
        previous = index;
    }
}
