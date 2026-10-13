#include "utils/commands.hpp"

namespace cnc {

void UndoStack::push(CommandPtr command) {
    command->execute();
    undo_.push_back(std::move(command));
    if (undo_.size() > max_depth_) undo_.pop_front();
    redo_.clear();
}

std::optional<std::string> UndoStack::undo() {
    if (undo_.empty()) return std::nullopt;
    CommandPtr cmd = undo_.back();
    undo_.pop_back();
    cmd->undo();
    redo_.push_back(cmd);
    return cmd->description();
}

std::optional<std::string> UndoStack::redo() {
    if (redo_.empty()) return std::nullopt;
    CommandPtr cmd = redo_.back();
    redo_.pop_back();
    cmd->execute();
    undo_.push_back(cmd);
    return cmd->description();
}

std::string UndoStack::undo_text() const {
    return undo_.empty() ? "Desfazer" : "Desfazer " + undo_.back()->description();
}

std::string UndoStack::redo_text() const {
    return redo_.empty() ? "Refazer" : "Refazer " + redo_.back()->description();
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

}  // namespace cnc
