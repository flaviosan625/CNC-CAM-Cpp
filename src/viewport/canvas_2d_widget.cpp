// Porte de src/viewport/canvas_2d.py: container com reguas e botao "ajustar a tela".
#include <algorithm>

#include <QEvent>
#include <QGridLayout>

#include "ui/icons.hpp"
#include "ui/theme.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

Canvas2DWidget::Canvas2DWidget(double mat_w, double mat_h, QWidget* parent) : QWidget(parent) {
    canvas = new Canvas2D();
    ruler_h = new RulerH(canvas);
    ruler_v = new RulerV(canvas, mat_h);
    connect(ruler_h, &RulerH::construction_line_requested, this, &Canvas2DWidget::construction_line_requested);
    connect(ruler_v, &RulerV::construction_line_requested, this, &Canvas2DWidget::construction_line_requested);

    auto* corner = new QWidget();
    corner->setFixedSize(kRulerSize, kRulerSize);
    corner->setStyleSheet(QString("background-color: %1;").arg(ui::color("panel")));

    auto* g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(0);
    g->addWidget(corner, 0, 0);
    g->addWidget(ruler_h, 0, 1);
    g->addWidget(ruler_v, 1, 0);
    g->addWidget(canvas, 1, 1);

    connect(canvas, &Canvas2D::view_changed, ruler_h, [this] { ruler_h->update(); });
    connect(canvas, &Canvas2D::view_changed, ruler_v, [this] { ruler_v->update(); });
    connect(canvas, &Canvas2D::mouse_moved, this, &Canvas2DWidget::on_mouse);
    connect(canvas, &Canvas2D::mouse_moved, this, &Canvas2DWidget::mouse_moved);
    connect(canvas, &Canvas2D::selection_changed, this, &Canvas2DWidget::selection_changed);
    connect(canvas, &Canvas2D::files_dropped, this, &Canvas2DWidget::files_dropped);
    connect(canvas, &Canvas2D::entity_double_clicked, this, &Canvas2DWidget::entity_double_clicked);
    connect(canvas, &Canvas2D::context_menu_requested, this, &Canvas2DWidget::context_menu_requested);
    canvas->setup_material(mat_w, mat_h);

    fit_button = build_fit_button();
    canvas->installEventFilter(this);
    position_button();
}

// Botao "ajustar a tela" sobreposto ao desenho. Fica dentro do canvas, e nao na barra lateral,
// porque e o comando que se usa no meio do trabalho - quando a peca sumiu da vista, procurar o
// botao longe do olho custa mais do que o proprio comando.
QToolButton* Canvas2DWidget::build_fit_button() {
    auto* botao = new QToolButton(canvas);
    botao->setIcon(ui::get_icon("zoom_fit"));
    botao->setIconSize(QSize(18, 18));
    botao->setFixedSize(32, 32);
    botao->setCursor(Qt::ArrowCursor);
    botao->setToolTip("Ajustar à tela");
    botao->setAutoRaise(true);
    botao->setStyleSheet(QString("QToolButton { background-color: %1; border: 1px solid rgba(0,0,0,70); "
                                 "border-radius: 6px; }"
                                 "QToolButton:hover { border-color: rgba(0,0,0,140); }")
                             .arg(ui::color("panel")));
    connect(botao, &QToolButton::clicked, canvas, &Canvas2D::fit_view);
    return botao;
}

void Canvas2DWidget::position_button() {
    const int margem = 12;
    fit_button->move(std::max(margem, canvas->width() - fit_button->width() - margem),
                     std::max(margem, canvas->height() - fit_button->height() - margem));
    fit_button->raise();
}

bool Canvas2DWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == canvas && event->type() == QEvent::Resize) position_button();
    return QWidget::eventFilter(obj, event);
}

void Canvas2DWidget::on_mouse(double wx, double wy) {
    const QPoint vp = canvas->mapFromScene(canvas->world_to_scene(wx, wy));
    ruler_h->set_cursor(vp.x());
    ruler_v->set_cursor(vp.y());
}

void Canvas2DWidget::set_material(double w, double h) {
    ruler_v->set_material_height(h);
    canvas->setup_material(w, h);
}

void Canvas2DWidget::set_document(cad::Document* document) {
    if (document) ruler_v->set_material_height(document->material.height);
    canvas->set_document(document);
}

}  // namespace cnc::vp
