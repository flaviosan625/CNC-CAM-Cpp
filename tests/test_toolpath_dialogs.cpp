#include <doctest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
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
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog_seen = dialog->windowTitle().startsWith("Percurso de ");
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
    }

    const QStringList profile_sections = {
        "Profundidades de corte", "Ferramenta", "Passagens", "Usinar vetores...",
        "Último passe", "Pontes", "Entradas e saídas", "Rampas", "Ordem",
    };
    int previous = -1;
    for (const QString& expected : profile_sections) {
        const int index = profile_order.indexOf(expected);
        REQUIRE(index >= 0);
        CHECK(index > previous);
        previous = index;
    }
}
