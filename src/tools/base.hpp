// Infraestrutura do sistema de ferramentas (porte de src/tools/base.py).
//
// Cada ferramenta recebe eventos de mouse do canvas em coordenadas de mundo e cria/modifica entidades
// do documento atraves de comandos (undo/redo).
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Qt>
#include <QCursor>

#include "cad/document.hpp"
#include "cad/entities.hpp"
#include "utils/commands.hpp"
#include "viewport/rotate_handles.hpp"
#include "viewport/snapping.hpp"

namespace cnc::tools {

// Copia um conjunto mantendo grupos independentes do original.
std::vector<cad::EntityPtr> clone_entities(const std::vector<cad::EntityPtr>& entities);

// ======================================================================
// Comandos genericos
// ======================================================================

class AddCommand : public Command {
public:
    AddCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities, std::string label = "Adicionar")
        : doc_(doc), ents_(std::move(entities)), label_(std::move(label)) {}
    void execute() override;
    void undo() override;
    std::string description() const override { return label_; }

private:
    cad::Document& doc_;
    std::vector<cad::EntityPtr> ents_;
    std::string label_;
};

class DeleteCommand : public Command {
public:
    DeleteCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities);
    void execute() override;
    void undo() override;
    std::string description() const override;

private:
    cad::Document& doc_;
    std::vector<cad::EntityPtr> ents_;
    std::vector<size_t> idx_;
};

// Substitui um conjunto de entidades por outro (booleanas, offset, etc).
class ReplaceCommand : public Command {
public:
    ReplaceCommand(cad::Document& doc, std::vector<cad::EntityPtr> old_ents, std::vector<cad::EntityPtr> new_ents,
                   std::string label = "Modificar");
    void execute() override;
    void undo() override;
    std::string description() const override { return label_; }

private:
    cad::Document& doc_;
    std::vector<cad::EntityPtr> old_, new_;
    std::string label_;
    std::vector<size_t> indices_;
    std::vector<std::string> selection_before_, selection_after_;
};

// Undo por snapshot - serve para qualquer transformacao no proprio objeto.
class TransformCommand : public Command {
public:
    using ApplyFn = std::function<void(cad::Entity&)>;
    TransformCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities, ApplyFn apply_fn,
                     std::string label = "Transformar");
    void execute() override;
    void undo() override;
    std::string description() const override { return label_; }

private:
    void restore(const std::vector<cad::EntityPtr>& states);

    cad::Document& doc_;
    std::vector<cad::EntityPtr> ents_;
    std::vector<cad::EntityPtr> before_;
    ApplyFn apply_;
    std::string label_;
    std::vector<cad::EntityPtr> after_;
    bool has_after_ = false;
};

// ======================================================================
// Contexto das ferramentas
// ======================================================================

// O que as ferramentas pedem ao canvas (CanvasLike do Python). O que o Python consulta com
// getattr/hasattr tem implementacao padrao vazia aqui: um canvas de teste so precisa do essencial.
class ICanvas {
public:
    virtual ~ICanvas() = default;
    virtual void refresh_entities() = 0;
    virtual void set_preview(const Points& pts, bool closed = false, bool nodes = false) = 0;
    virtual void show_dimension_preview(const std::shared_ptr<cad::Dimension>& dimension) = 0;
    virtual void clear_preview() = 0;
    // Unidades de mundo por pixel (para tolerancia de clique).
    virtual double pick_tolerance() const = 0;

    // -- opcionais --
    // Quantos pixels de tela vale 1 mm no zoom atual.
    virtual double px_per_mm() const { return 1.0; }
    virtual vp::SelectionHandles* selection_handles() { return nullptr; }
    virtual const vp::SnapEngine* snap_engine() const { return nullptr; }
    virtual void set_selection_preview(const Points& pts, bool crossing) {
        (void)pts; (void)crossing;
    }
    virtual void show_snap_marker(const std::optional<vp::SnapResult>& result) { (void)result; }
    virtual void show_measurement(const Pt& start, const Pt& end, const std::string& text) {
        (void)start; (void)end; (void)text;
    }
    virtual void clear_measurement() {}
    virtual void set_cursor(const QCursor& cursor) { (void)cursor; }
};

// Acesso compartilhado que as ferramentas usam.
class ToolContext {
public:
    ToolContext(cad::Document& document, UndoStack& undo_stack) : document(&document), undo_stack(&undo_stack) {}

    cad::Document* document;
    UndoStack* undo_stack;
    ICanvas* canvas = nullptr;
    std::function<void(const std::string&)> status_fn = [](const std::string&) {};
    std::function<void(const std::string&)> prompt_fn = [](const std::string&) {};
    std::function<void()> finished_fn = [] {};
    // Pergunta "sim ou nao" ao usuario. O padrao aceita, para que teste e execucao sem janela nao
    // fiquem presos esperando clique.
    std::function<bool(const std::string&, const std::string&)> confirm_fn = [](const std::string&, const std::string&) {
        return true;
    };
    std::string current_layer = "Camada 1";

    // -- helpers --
    void add(const std::vector<cad::EntityPtr>& entities, const std::string& label = "Adicionar",
             bool preserve_layers = false);
    void add(const cad::EntityPtr& entity, const std::string& label = "Adicionar", bool preserve_layers = false) {
        add(std::vector<cad::EntityPtr>{entity}, label, preserve_layers);
    }
    void remove(const std::vector<cad::EntityPtr>& entities);
    void replace(const std::vector<cad::EntityPtr>& old_ents, std::vector<cad::EntityPtr> new_ents,
                 const std::string& label);
    void transform(const std::vector<cad::EntityPtr>& entities, TransformCommand::ApplyFn fn, const std::string& label);
    std::vector<cad::EntityPtr> selection() const { return document->selected_entities(); }
    void refresh() {
        if (canvas) canvas->refresh_entities();
    }
    void status(const std::string& msg) const { status_fn(msg); }
    void prompt(const std::string& msg) const { prompt_fn(msg); }
    void preview(const Points& pts, bool closed = false, bool nodes = false) {
        if (canvas) canvas->set_preview(pts, closed, nodes);
    }
    void preview_dimension(const std::shared_ptr<cad::Dimension>& dimension) {
        if (canvas) {
            dimension->layer = current_layer;
            canvas->show_dimension_preview(dimension);
        }
    }
    void clear_preview() {
        if (canvas) canvas->clear_preview();
    }
    // Sinaliza que a ferramenta concluiu (volta para selecao).
    void finish() const { finished_fn(); }
};

// ======================================================================
// Ferramenta base
// ======================================================================

// Base para todas as ferramentas interativas. Os eventos devolvem true se consumiram o evento.
class BaseTool {
public:
    explicit BaseTool(ToolContext& ctx) : ctx(&ctx) {}
    virtual ~BaseTool() = default;

    ToolContext* ctx;

    virtual std::string name() const { return "tool"; }
    virtual std::string label() const { return "Ferramenta"; }
    virtual QCursor cursor() const { return QCursor(Qt::CrossCursor); }
    virtual bool instant() const { return false; }          // executa imediatamente ao ser escolhida
    virtual bool needs_selection() const { return false; }  // exige selecao previa
    virtual bool cursor_snap() const { return true; }       // o ima corrige o cursor antes de chegar aqui

    // -- ciclo de vida --
    virtual void activate() { ctx->prompt(hint()); }
    virtual void deactivate();
    void begin_live_transform(const std::vector<cad::EntityPtr>& entities);
    void restore_live_transform(bool refresh = true, bool keep_snapshot = false);
    virtual std::string hint() const { return label(); }
    // Ferramentas "instant" implementam aqui a acao direta.
    virtual void run() {}

    // -- eventos --
    virtual bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
        (void)wx; (void)wy; (void)button; (void)mods;
        return false;
    }
    virtual bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) {
        (void)wx; (void)wy; (void)mods;
        return false;
    }
    virtual bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
        (void)wx; (void)wy; (void)button; (void)mods;
        return false;
    }
    virtual bool on_double_click(double wx, double wy) {
        (void)wx; (void)wy;
        return false;
    }
    virtual bool on_key(int key);
    virtual void cancel();

    // Ganchos que o canvas consulta (no Python: getattr com padrao).
    // Entidades que estao sendo arrastadas - nao faz sentido grudar em si mesmas.
    virtual std::vector<cad::EntityPtr> snap_targets() const { return {}; }
    // "handle-rotate" / "handle-scale" enquanto uma alca esta sendo arrastada.
    virtual std::string active_mode() const { return ""; }
    virtual std::optional<vp::HandleKind> active_handle() const { return std::nullopt; }

private:
    std::vector<std::pair<cad::EntityPtr, cad::EntityPtr>> transform_before_;
};

}  // namespace cnc::tools
