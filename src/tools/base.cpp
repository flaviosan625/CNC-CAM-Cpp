// Porte de src/tools/base.py.
#include "tools/base.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace cnc::tools {

std::vector<cad::EntityPtr> clone_entities(const std::vector<cad::EntityPtr>& entities) {
    std::vector<cad::EntityPtr> copies;
    for (const cad::EntityPtr& e : entities) copies.push_back(e->clone());
    std::map<std::string, std::string> groups;
    for (const cad::EntityPtr& e : copies) {
        if (e->group && !e->group->empty() && !groups.count(*e->group)) groups[*e->group] = new_uuid_hex();
    }
    for (const cad::EntityPtr& e : copies) {
        if (e->group && !e->group->empty()) e->group = groups[*e->group];
    }
    return copies;
}

// ----------------------------------------------------------------------
// Comandos
// ----------------------------------------------------------------------

void AddCommand::execute() {
    for (const cad::EntityPtr& e : ents_) doc_.add_entity(e);
}

void AddCommand::undo() {
    for (const cad::EntityPtr& e : ents_) doc_.remove_entity(e->id);
}

DeleteCommand::DeleteCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities)
    : doc_(doc), ents_(std::move(entities)) {
    for (const cad::EntityPtr& e : ents_) {
        auto it = std::find(doc_.entities.begin(), doc_.entities.end(), e);
        if (it != doc_.entities.end()) idx_.push_back(static_cast<size_t>(it - doc_.entities.begin()));
    }
}

void DeleteCommand::execute() {
    for (const cad::EntityPtr& e : ents_) doc_.remove_entity(e->id);
}

void DeleteCommand::undo() {
    // zip(self._idx, self._ents) sorted por indice: so as entidades que existiam no documento tem indice
    std::vector<std::pair<size_t, cad::EntityPtr>> pairs;
    for (size_t i = 0; i < idx_.size() && i < ents_.size(); ++i) pairs.emplace_back(idx_[i], ents_[i]);
    std::stable_sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [i, e] : pairs) {
        doc_.entities.insert(doc_.entities.begin() + static_cast<long>(std::min(i, doc_.entities.size())), e);
    }
    doc_.modified = true;
}

std::string DeleteCommand::description() const { return std::format("Excluir {} objeto(s)", ents_.size()); }

ReplaceCommand::ReplaceCommand(cad::Document& doc, std::vector<cad::EntityPtr> old_ents,
                               std::vector<cad::EntityPtr> new_ents, std::string label)
    : doc_(doc), old_(std::move(old_ents)), new_(std::move(new_ents)), label_(std::move(label)) {
    for (const cad::EntityPtr& e : old_) {
        auto it = std::find(doc_.entities.begin(), doc_.entities.end(), e);
        indices_.push_back(static_cast<size_t>(it - doc_.entities.begin()));
    }
    selection_before_ = doc_.selection_order();
    std::set<std::string> old_ids;
    for (const cad::EntityPtr& e : old_) old_ids.insert(e->id);
    std::vector<std::string> new_ids;
    for (const cad::EntityPtr& e : new_) {
        if (e->selected) new_ids.push_back(e->id);
    }
    bool inserted = false;
    for (const std::string& eid : selection_before_) {
        if (old_ids.count(eid)) {
            if (!inserted) {
                selection_after_.insert(selection_after_.end(), new_ids.begin(), new_ids.end());
                inserted = true;
            }
        } else {
            selection_after_.push_back(eid);
        }
    }
    if (!inserted) selection_after_.insert(selection_after_.end(), new_ids.begin(), new_ids.end());
}

void ReplaceCommand::execute() {
    for (const cad::EntityPtr& e : old_) doc_.remove_entity(e->id);
    for (const cad::EntityPtr& e : new_) doc_.add_entity(e);
    doc_.set_selection_order(selection_after_);
}

void ReplaceCommand::undo() {
    for (const cad::EntityPtr& e : new_) doc_.remove_entity(e->id);
    std::vector<std::pair<size_t, cad::EntityPtr>> pairs;
    for (size_t i = 0; i < indices_.size(); ++i) pairs.emplace_back(indices_[i], old_[i]);
    std::stable_sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, e] : pairs) {
        doc_.entities.insert(doc_.entities.begin() + static_cast<long>(std::min(index, doc_.entities.size())), e);
    }
    doc_.set_selection_order(selection_before_);
    doc_.modified = true;
}

TransformCommand::TransformCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities, ApplyFn apply_fn,
                                   std::string label)
    : doc_(doc), ents_(std::move(entities)), apply_(std::move(apply_fn)), label_(std::move(label)) {
    for (const cad::EntityPtr& e : ents_) before_.push_back(e->copy());
}

void TransformCommand::execute() {
    if (!has_after_) {
        for (const cad::EntityPtr& e : ents_) {
            const auto version = e->version;
            apply_(*e);
            // Property editors can assign fields directly rather than calling a
            // geometry method. Invalidate drawing/picking/snap caches for them.
            if (e->version == version) e->touch();
        }
        for (const cad::EntityPtr& e : ents_) after_.push_back(e->copy());
        has_after_ = true;
    } else {
        restore(after_);
    }
    doc_.modified = true;
}

void TransformCommand::undo() {
    restore(before_);
    doc_.modified = true;
}

void TransformCommand::restore(const std::vector<cad::EntityPtr>& states) {
    for (size_t i = 0; i < ents_.size() && i < states.size(); ++i) ents_[i]->restore_from(*states[i]);
}

TranslateCommand::TranslateCommand(cad::Document& doc, std::vector<cad::EntityPtr> entities,
                                   double dx, double dy, std::string label)
    : doc_(doc), ents_(std::move(entities)), dx_(dx), dy_(dy), label_(std::move(label)) {
    before_.reserve(ents_.size());
    for (const cad::EntityPtr& entity : ents_) before_.push_back(entity->copy());
}

void TranslateCommand::execute() {
    for (const cad::EntityPtr& entity : ents_) {
        const uint64_t version = entity->version;
        entity->translate(dx_, dy_);
        if (entity->version == version) entity->touch();
    }
    doc_.modified = true;
}

void TranslateCommand::undo() {
    for (size_t i = 0; i < ents_.size() && i < before_.size(); ++i) ents_[i]->restore_from(*before_[i]);
    doc_.modified = true;
}

// ----------------------------------------------------------------------
// Contexto
// ----------------------------------------------------------------------

void ToolContext::add(const std::vector<cad::EntityPtr>& entities, const std::string& label, bool preserve_layers) {
    if (entities.empty()) return;
    for (const cad::EntityPtr& e : entities) {
        if (!preserve_layers || e->layer.empty()) {
            e->layer = current_layer;
        } else if (document && !document->get_layer(e->layer)) {
            document->add_layer(e->layer);
        }
    }
    std::map<std::string, std::string> groups;
    for (const cad::EntityPtr& e : entities) {
        if (e->group && !e->group->empty() && !groups.count(*e->group)) groups[*e->group] = new_uuid_hex();
    }
    for (const cad::EntityPtr& e : entities) {
        if (e->group && !e->group->empty()) e->group = groups[*e->group];
    }
    undo_stack->push(std::make_shared<AddCommand>(*document, entities, label));
    refresh();
}

void ToolContext::remove(const std::vector<cad::EntityPtr>& entities) {
    if (entities.empty()) return;
    undo_stack->push(std::make_shared<DeleteCommand>(*document, entities));
    refresh();
}

void ToolContext::replace(const std::vector<cad::EntityPtr>& old_ents, std::vector<cad::EntityPtr> new_ents,
                          const std::string& label) {
    if (old_ents.empty()) return;
    for (const cad::EntityPtr& e : new_ents) e->selected = true;
    undo_stack->push(std::make_shared<ReplaceCommand>(*document, old_ents, std::move(new_ents), label));
    refresh();
}

void ToolContext::transform(const std::vector<cad::EntityPtr>& entities, TransformCommand::ApplyFn fn,
                            const std::string& label, bool refresh) {
    if (entities.empty()) {
        status("Nenhum objeto selecionado");
        return;
    }
    undo_stack->push(std::make_shared<TransformCommand>(*document, entities, std::move(fn), label));
    if (refresh) this->refresh();
}

void ToolContext::translate(const std::vector<cad::EntityPtr>& entities, double dx, double dy,
                            const std::string& label, bool refresh) {
    if (entities.empty()) {
        status("Nenhum objeto selecionado");
        return;
    }
    undo_stack->push(std::make_shared<TranslateCommand>(*document, entities, dx, dy, label));
    if (refresh) this->refresh();
}

// ----------------------------------------------------------------------
// Ferramenta base
// ----------------------------------------------------------------------

void BaseTool::deactivate() {
    restore_live_transform();
    ctx->clear_preview();
}

void BaseTool::begin_live_transform(const std::vector<cad::EntityPtr>& entities) {
    transform_before_.clear();
    for (const cad::EntityPtr& e : entities) transform_before_.emplace_back(e, e->copy());
}

void BaseTool::restore_live_transform(bool refresh, bool keep_snapshot) {
    for (const auto& [entity, snapshot] : transform_before_) {
        entity->restore_from(*snapshot);
        entity->selected = snapshot->selected;
    }
    if (refresh && !transform_before_.empty()) ctx->refresh();
    if (!keep_snapshot) transform_before_.clear();
}

bool BaseTool::on_key(int key) {
    if (key == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

void BaseTool::cancel() {
    ctx->clear_preview();
    ctx->status("Cancelado");
    ctx->finish();
}

}  // namespace cnc::tools
