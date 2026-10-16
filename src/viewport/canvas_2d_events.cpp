// Porte de src/viewport/canvas_2d.py: eventos de mouse, teclado e arrastar-e-soltar.
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QUrl>
#include <QWheelEvent>

#include "io/importers.hpp"
#include "tools/cursors.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

namespace {

int zoom_direction(const QKeyEvent* event) {
    const auto modifiers = event->modifiers();
    if (!(modifiers & Qt::ControlModifier) || (modifiers & (Qt::AltModifier | Qt::MetaModifier))) return 0;
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) return 1;
    if (event->key() == Qt::Key_Minus) return -1;
    return 0;
}

bool text_input_has_focus() {
    QWidget* focus = QApplication::focusWidget();
    for (QWidget* widget = focus; widget; widget = widget->parentWidget()) {
        if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QTextEdit*>(widget) ||
            qobject_cast<QPlainTextEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget))
            return true;
        if (auto* combo = qobject_cast<QComboBox*>(widget); combo && combo->isEditable()) return true;
    }
    return false;
}

}  // namespace

// -- arrastar e soltar arquivos --

// Caminhos aceitos no que foi arrastado, na ordem em que vieram.
QStringList Canvas2D::dropped_paths(const QMimeData* mime) const {
    QStringList out;
    if (!mime->hasUrls()) return out;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (io::is_supported(std::filesystem::path(path.toStdWString()))) out.push_back(QDir::toNativeSeparators(path));
    }
    return out;
}

void Canvas2D::dragEnterEvent(QDragEnterEvent* event) {
    if (!dropped_paths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void Canvas2D::dragMoveEvent(QDragMoveEvent* event) {
    if (!dropped_paths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void Canvas2D::dropEvent(QDropEvent* event) {
    const QStringList caminhos = dropped_paths(event->mimeData());
    if (caminhos.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    setFocus();
    const Pt w = to_world(event->position());
    emit files_dropped(caminhos, w.x, w.y);
}

// -- mouse --

void Canvas2D::wheelEvent(QWheelEvent* event) {
    const double f = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    scale(f, f);
    emit view_changed();
}

std::set<std::string> Canvas2D::dragging_ids() const {
    std::set<std::string> ids;
    if (!tool_) return ids;
    for (const cad::EntityPtr& e : tool_->snap_targets()) ids.insert(e->id);
    return ids;
}

void Canvas2D::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        last_pan_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton && dynamic_cast<TabMarker*>(itemAt(event->position().toPoint()))) {
        dragging_tab_marker_ = true;
        QGraphicsView::mousePressEvent(event);
        return;
    }
    setFocus();
    const Pt raw = to_world(event->position());
    const Pt w = apply_snap(raw.x, raw.y, event->modifiers(), dragging_ids());
    if (manual_tab_mode && event->button() == Qt::LeftButton) {
        emit manual_tab_clicked(w.x, w.y);
        event->accept();
        return;
    }
    if (manual_drill_mode && event->button() == Qt::LeftButton) {
        emit manual_drill_clicked(w.x, w.y);
        event->accept();
        return;
    }
    if (tool_ && tool_->on_press(w.x, w.y, event->button(), event->modifiers())) {
        if (event->button() == Qt::RightButton) right_consumed_ = true;
        emit selection_changed();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void Canvas2D::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (dragging_tab_marker_) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    if (panning_) {
        const QPointF d = pos - last_pan_;
        last_pan_ = pos;
        QScrollBar* hs = horizontalScrollBar();
        QScrollBar* vs = verticalScrollBar();
        hs->setValue(hs->value() - static_cast<int>(d.x()));
        vs->setValue(vs->value() - static_cast<int>(d.y()));
        emit view_changed();
        return;
    }

    const Pt raw = to_world(pos);
    emit mouse_moved(raw.x, raw.y);
    const Pt w = apply_snap(raw.x, raw.y, event->modifiers(), dragging_ids());
    if (tool_ && tool_->on_move(w.x, w.y, event->modifiers())) {
        update_hover_cursor(raw.x, raw.y);
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
    // QGraphicsView pode restaurar a seta ao processar o movimento. Aplicar o cursor da alca depois
    // dele mantem rotacao/escala visiveis.
    update_hover_cursor(raw.x, raw.y);
}

// Cursor para cada tipo de alca - rotacao (-1) e escala em cada direcao (setas nativas do Qt).
std::optional<int> Canvas2D::cursor_code_for(const std::optional<HandleKind>& kind) const {
    if (!kind) return std::nullopt;
    if (is_rotate(*kind)) return -1;
    switch (*kind) {
        case HandleKind::ScaleL:
        case HandleKind::ScaleR: return static_cast<int>(Qt::SizeHorCursor);
        case HandleKind::ScaleT:
        case HandleKind::ScaleB: return static_cast<int>(Qt::SizeVerCursor);
        case HandleKind::ScaleTL:
        case HandleKind::ScaleBR: return static_cast<int>(Qt::SizeFDiagCursor);
        case HandleKind::ScaleTR:
        case HandleKind::ScaleBL: return static_cast<int>(Qt::SizeBDiagCursor);
        default: return std::nullopt;
    }
}

void Canvas2D::apply_cursor_code(int code) {
    setCursor(code == -1 ? tools::cursors::rotate() : QCursor(static_cast<Qt::CursorShape>(code)));
}

// Troca o cursor conforme a alca sob o mouse - so na selecao. E o jeito do operador saber, antes de
// clicar, se vai girar, escalar ou mover. Em outras ferramentas o cursor e da ferramenta e nao deve
// ser mexido aqui.
void Canvas2D::update_hover_cursor(double wx, double wy) {
    if (!tool_ || tool_->name() != "select" || !handles.visible()) {
        // Fora da selecao, o cursor e da ferramenta: restaurar se foi alterado por hover anterior.
        if (hover_cursor_) {
            setCursor(tool_ ? tool_->cursor() : QCursor(Qt::ArrowCursor));
            hover_cursor_.reset();
        }
        return;
    }
    const std::string mode = tool_->active_mode();
    if (mode == "handle-rotate" || mode == "handle-scale") {
        const std::optional<int> novo = cursor_code_for(tool_->active_handle());
        if (novo != hover_cursor_) {
            hover_cursor_ = novo;
            if (novo) apply_cursor_code(*novo);
        }
        return;
    }
    const std::optional<HandleHit> alca = handles.hit(wx, wy);
    const std::optional<int> novo = cursor_code_for(alca ? std::optional<HandleKind>(alca->kind) : std::nullopt);
    if (novo != hover_cursor_) {
        hover_cursor_ = novo;
        if (novo) {
            apply_cursor_code(*novo);
        } else {
            setCursor(tool_ ? tool_->cursor() : QCursor(Qt::ArrowCursor));
        }
    }
}

void Canvas2D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(tool_ ? tool_->cursor() : QCursor(Qt::ArrowCursor));
        return;
    }
    if (dragging_tab_marker_) {
        QGraphicsView::mouseReleaseEvent(event);
        dragging_tab_marker_ = false;
        return;
    }
    const Pt raw = to_world(event->position());
    const Pt w = apply_snap(raw.x, raw.y, event->modifiers(), dragging_ids());
    clear_snap_marker();
    if (tool_ && tool_->on_release(w.x, w.y, event->button(), event->modifiers())) {
        emit selection_changed();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void Canvas2D::mouseDoubleClickEvent(QMouseEvent* event) {
    const Pt w = to_world(event->position());
    // A ferramenta tem prioridade: dois cliques fecham a polilinha e inserem no na edicao de nos, e
    // isso nao pode virar caixa de edicao.
    if (tool_ && tool_->on_double_click(w.x, w.y)) return;
    const cad::EntityPtr alvo = entity_at(w.x, w.y);
    if (alvo) {
        emit entity_double_clicked(QString::fromStdString(alvo->id));
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void Canvas2D::contextMenuEvent(QContextMenuEvent* event) {
    if (right_consumed_) {
        right_consumed_ = false;
        event->accept();
        return;
    }
    const Pt w = scene_to_world(mapToScene(event->pos()));
    const cad::EntityPtr alvo = entity_at(w.x, w.y);
    if (alvo && !alvo->selected && document_) {
        // Clicar com a direita num vetor solto seleciona ele: o menu fala da selecao, e menu
        // falando de outra coisa e armadilha.
        document_->clear_selection();
        document_->set_selected(alvo, true);
        refresh_entities();
        emit selection_changed();
    }
    emit context_menu_requested(event->globalPos(), w.x, w.y);
    event->accept();
}

// -- teclado --

bool Canvas2D::event(QEvent* ev) {
    // Enquanto o desenho esta ativo, reservar Ctrl+mais/menos antes dos atalhos do menu.
    // Campos de entrada conservam as proprias teclas quando recebem o foco.
    if (ev->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(ev);
        if (zoom_direction(key) != 0 && !text_input_has_focus()) {
            ev->accept();
            return true;
        }
    }
    if (ev->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(ev);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier && tool_ && tool_->name() == "polyline") {
            keyPressEvent(key);
            return true;
        }
    }
    return QGraphicsView::event(ev);
}

void Canvas2D::keyPressEvent(QKeyEvent* event) {
    const int zoom = zoom_direction(event);
    if (zoom != 0 && !text_input_has_focus()) {
        zoom_by(zoom > 0 ? 1.25 : 1.0 / 1.25);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        // Esc tem um significado unico no aplicativo: abandonar a operacao inteira. Nao deixamos
        // cada ferramenta interpretar a tecla como apenas "limpar o passo atual" e continuar ativa.
        if (tool_) tool_->cancel();
        emit cancel_requested();
        event->accept();
        return;
    }
    if (tool_ && tool_->on_key(event->key())) {
        emit selection_changed();
        return;
    }
    if (nudge(event)) return;
    if (event->modifiers() == Qt::NoModifier) {
        std::optional<std::string> action = shortcuts::action_for(event->key(), &shortcut_table);
        // A tecla da edicao de nos alterna: apertar de novo volta para a selecao, que e como se sai
        // de dentro do vetor sem procurar botao.
        if (action == "node_edit" && tool_ && tool_->name() == "node_edit") action = "select";
        if (action) {
            emit shortcut_requested(QString::fromStdString(*action));
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

// Seta do teclado empurra a selecao - nunca rola a pagina. Passos como no Aspire: Ctrl afina, Shift
// engrossa. A seta e consumida mesmo sem selecao, senao a vista sai do lugar justamente quando se
// esta mirando o vetor.
bool Canvas2D::nudge(QKeyEvent* event) {
    double dir_x = 0.0, dir_y = 0.0;
    switch (event->key()) {
        case Qt::Key_Left: dir_x = -1.0; break;
        case Qt::Key_Right: dir_x = 1.0; break;
        case Qt::Key_Up: dir_y = 1.0; break;
        case Qt::Key_Down: dir_y = -1.0; break;
        default: return false;
    }
    if (!document_ || !tool_) return true;
    const auto sel = document_->selected_entities();
    if (sel.empty()) return true;

    const Qt::KeyboardModifiers mods = event->modifiers();
    double passo = kNudgeMm;
    if (mods & Qt::ControlModifier) {
        passo = kNudgeFineMm;
    } else if (mods & Qt::ShiftModifier) {
        passo = kNudgeCoarseMm;
    }
    const double dx = dir_x * passo, dy = dir_y * passo;
    tool_->ctx->translate(sel, dx, dy, "Empurrar");
    emit selection_changed();
    return true;
}

void Canvas2D::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    emit view_changed();
}

}  // namespace cnc::vp
