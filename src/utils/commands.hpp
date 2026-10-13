// Sistema de Undo/Redo via Command Pattern (porte de src/utils/commands.py).
//
// Cada acao do usuario cria um Command que sabe se executar e se desfazer. O UndoStack
// gerencia a pilha de comandos.
#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cnc {

// Base abstrata para qualquer acao reversivel.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    // Descricao curta para o menu Editar.
    virtual std::string description() const { return "Command"; }
};

using CommandPtr = std::shared_ptr<Command>;

// Pilha de undo/redo com limite de profundidade.
class UndoStack {
public:
    explicit UndoStack(size_t max_depth = 200) : max_depth_(max_depth) {}

    // Executa um comando e o adiciona a pilha.
    void push(CommandPtr command);
    // Desfaz o ultimo comando. Devolve a descricao, ou nada se a pilha estiver vazia.
    std::optional<std::string> undo();
    // Refaz o ultimo comando desfeito.
    std::optional<std::string> redo();

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    // So o nome da acao, sem o verbo - o menu escolhe o seu.
    std::string undo_description() const { return undo_.empty() ? "" : undo_.back()->description(); }
    std::string redo_description() const { return redo_.empty() ? "" : redo_.back()->description(); }
    std::string undo_text() const;
    std::string redo_text() const;
    void clear();

private:
    std::deque<CommandPtr> undo_;
    std::vector<CommandPtr> redo_;
    size_t max_depth_;
};

}  // namespace cnc
