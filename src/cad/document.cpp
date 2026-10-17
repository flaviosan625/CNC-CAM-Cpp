// Porte de src/cad/document.py.
#include "cad/document.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "cad/entity_json.hpp"

namespace cnc::cad {

// ----------------------------------------------------------------------
// OrderedStrMap
// ----------------------------------------------------------------------

void OrderedStrMap::set(const std::string& key, const std::string& value) {
    for (Item& it : items_) {
        if (it.first == key) {
            it.second = value;
            return;
        }
    }
    items_.emplace_back(key, value);
}

const std::string* OrderedStrMap::get(const std::string& key) const {
    for (const Item& it : items_) {
        if (it.first == key) return &it.second;
    }
    return nullptr;
}

bool OrderedStrMap::erase(const std::string& key) {
    auto it = std::find_if(items_.begin(), items_.end(), [&](const Item& i) { return i.first == key; });
    if (it == items_.end()) return false;
    items_.erase(it);
    return true;
}

// ----------------------------------------------------------------------
// Document
// ----------------------------------------------------------------------

Document::Document() { layers.push_back(Layer{}); }

std::string Document::title() const {
    return file_path ? path_to_utf8(file_path->stem()) : "Sem título";
}

void Document::add_entity(const EntityPtr& entity) {
    entities.push_back(entity);
    if (entity->selected &&
        std::find(selection_order_.begin(), selection_order_.end(), entity->id) == selection_order_.end()) {
        selection_order_.push_back(entity->id);
    }
    modified = true;
}

EntityPtr Document::remove_entity(const std::string& entity_id) {
    for (size_t i = 0; i < entities.size(); ++i) {
        if (entities[i]->id == entity_id) {
            modified = true;
            EntityPtr e = entities[i];
            entities.erase(entities.begin() + static_cast<long>(i));
            return e;
        }
    }
    return nullptr;
}

EntityPtr Document::find_entity(const std::string& entity_id) const {
    for (const EntityPtr& e : entities) {
        if (e->id == entity_id) return e;
    }
    return nullptr;
}

std::vector<EntityPtr> Document::entities_on_layer(const std::string& name) const {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : entities) {
        if (e->layer == name) out.push_back(e);
    }
    return out;
}

std::vector<EntityPtr> Document::selected_entities() const {
    std::map<std::string, int> positions;
    for (size_t i = 0; i < selection_order_.size(); ++i) positions[selection_order_[i]] = static_cast<int>(i);
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : entities) {
        if (e->selected && is_editable(*e)) out.push_back(e);
    }
    auto pos = [&](const EntityPtr& e) {
        auto it = positions.find(e->id);
        return it == positions.end() ? -1 : it->second;
    };
    std::stable_sort(out.begin(), out.end(), [&](const EntityPtr& a, const EntityPtr& b) { return pos(a) < pos(b); });
    return out;
}

bool Document::is_editable(const Entity& entity) const {
    const Layer* layer = get_layer(entity.layer);
    return layer == nullptr || (layer->visible && !layer->locked);
}

void Document::set_selected(const EntityPtr& entity, bool selected) {
    entity->selected = selected;
    selection_order_.erase(std::remove(selection_order_.begin(), selection_order_.end(), entity->id),
                           selection_order_.end());
    if (selected) selection_order_.push_back(entity->id);
}

void Document::clear_selection() {
    selection_order_.clear();
    for (const EntityPtr& e : entities) e->selected = false;
}

void Document::select_all() {
    clear_selection();
    for (const EntityPtr& e : entities) {
        if (is_editable(*e)) set_selected(e, true);
    }
}

Layer* Document::get_layer(const std::string& name) {
    for (Layer& l : layers) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

const Layer* Document::get_layer(const std::string& name) const {
    for (const Layer& l : layers) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

Layer& Document::add_layer(const std::string& name) {
    Layer lay;
    lay.name = name;
    layers.push_back(lay);
    modified = true;
    return layers.back();
}

bool Document::remove_layer(const std::string& name) {
    if (layers.size() <= 1) return false;
    if (!get_layer(name)) return false;
    std::string fallback;
    for (const Layer& l : layers) {
        if (l.name != name) {
            fallback = l.name;
            break;
        }
    }
    for (const EntityPtr& e : entities_on_layer(name)) e->layer = fallback;
    layers.erase(std::remove_if(layers.begin(), layers.end(), [&](const Layer& l) { return l.name == name; }),
                 layers.end());
    modified = true;
    return true;
}

void Document::ensure_import_layers() {
    if (!get_layer("Cotas")) {
        auto& lay = add_layer("Cotas");
        lay.color = {0.31, 0.76, 0.97}; // soft cyan #4fc3f7
    }
    if (!get_layer("Linha de centro")) {
        auto& lay = add_layer("Linha de centro");
        lay.color = {1.0, 0.62, 0.26}; // orange #ff9f43
    }
}

// -- percursos --

std::shared_ptr<cam::ToolpathJob> Document::add_toolpath(std::shared_ptr<cam::ToolpathJob> job) {
    toolpaths.push_back(job);
    modified = true;
    return job;
}

void Document::remove_toolpath(const std::shared_ptr<cam::ToolpathJob>& job) {
    auto it = std::find(toolpaths.begin(), toolpaths.end(), job);
    if (it != toolpaths.end()) {
        toolpaths.erase(it);
        modified = true;
    }
}

void Document::move_toolpath(const std::shared_ptr<cam::ToolpathJob>& job, int delta) {
    auto it = std::find(toolpaths.begin(), toolpaths.end(), job);
    if (it == toolpaths.end()) return;
    const int i = static_cast<int>(it - toolpaths.begin());
    const int j = std::max(0, std::min(static_cast<int>(toolpaths.size()) - 1, i + delta));
    if (i != j) {
        auto moved = toolpaths[static_cast<size_t>(i)];
        toolpaths.erase(toolpaths.begin() + i);
        toolpaths.insert(toolpaths.begin() + j, moved);
        modified = true;
    }
}

std::vector<std::string> Document::recalculate_toolpaths(const std::shared_ptr<cam::ToolpathJob>& only) {
    std::vector<std::string> errors;
    std::vector<std::shared_ptr<cam::ToolpathJob>> jobs;
    if (only) {
        jobs.push_back(only);
    } else {
        jobs = toolpaths;
    }
    for (const auto& job : jobs) {
        job->calculate(*this);
        if (!job->error.empty()) {
            errors.push_back(job->name + ": " + job->error);
        }
    }
    return errors;
}

std::string Document::unique_toolpath_name(const std::string& base) const {
    auto taken = [&](const std::string& n) {
        return std::any_of(toolpaths.begin(), toolpaths.end(), [&](const auto& j) { return j->name == n; });
    };
    if (!taken(base)) return base;
    int n = 2;
    while (taken(base + " " + std::to_string(n))) ++n;
    return base + " " + std::to_string(n);
}

// -- caixa do desenho --

std::optional<BBox> Document::bounding_box() const {
    if (entities.empty()) return std::nullopt;
    BBox out = entities[0]->bounding_box();
    for (size_t i = 1; i < entities.size(); ++i) {
        const BBox b = entities[i]->bounding_box();
        out.x0 = std::min(out.x0, b.x0);
        out.y0 = std::min(out.y0, b.y0);
        out.x1 = std::max(out.x1, b.x1);
        out.y1 = std::max(out.y1, b.y1);
    }
    return out;
}

void Document::sync_dimensions() {
    std::map<std::string, EntityPtr> sources;
    for (const EntityPtr& e : entities) {
        if (is_a<Circle>(e) || is_a<Arc>(e)) sources[e->id] = e;
    }
    for (const EntityPtr& e : entities) {
        auto* dim = as<Dimension>(e);
        if (!dim || !dim->source_id || dim->source_id->empty()) continue;
        auto it = sources.find(*dim->source_id);
        if (it == sources.end()) continue;
        double cx, cy, radius;
        if (auto* c = as<Circle>(it->second)) {
            cx = c->cx; cy = c->cy; radius = c->radius;
        } else {
            auto* a = as<Arc>(it->second);
            cx = a->cx; cy = a->cy; radius = a->radius;
        }
        const double old_cx = dim->x1, old_cy = dim->y1;
        double vx = dim->x2 - old_cx, vy = dim->y2 - old_cy;
        double length = std::pow(vx * vx + vy * vy, 0.5);
        if (length < 1e-9) {
            vx = 1.0;
            vy = 0.0;
            length = 1.0;
        }
        const double next_x2 = cx + radius * vx / length;
        const double next_y2 = cy + radius * vy / length;
        const double next_x3 = dim->x3 + (cx - old_cx);
        const double next_y3 = dim->y3 + (cy - old_cy);
        if (dim->x1 == cx && dim->y1 == cy && dim->x2 == next_x2 &&
            dim->y2 == next_y2 && dim->x3 == next_x3 && dim->y3 == next_y3) continue;
        dim->x1 = cx;
        dim->y1 = cy;
        dim->x2 = next_x2;
        dim->y2 = next_y2;
        dim->x3 = next_x3;
        dim->y3 = next_y3;
        // Linked dimensions are edited here rather than through transforms.
        // Notify the viewport and snap caches only when their geometry changes.
        dim->touch();
    }
}

// ----------------------------------------------------------------------
// Persistencia (JSON)
// ----------------------------------------------------------------------

void Document::save(const std::optional<std::filesystem::path>& path) {
    const std::optional<std::filesystem::path> target = path ? path : file_path;
    if (!target) throw std::runtime_error("Nenhum caminho de arquivo definido.");
    write_text_file(*target, dumps_pretty(to_json()));
    file_path = target;
    modified = false;
}

void Document::load(const std::filesystem::path& path) {
    from_json(Json::parse(read_text_file(path)));
    file_path = path;
    modified = false;
}

Json Document::to_json() const {
    Json ents = Json::array();
    for (const EntityPtr& e : entities) ents.push_back(entity_to_json(*e));

    Json lays = Json::array();
    for (const Layer& l : layers) {
        Json d = Json::object();
        d["name"] = l.name;
        d["color"] = rgb_to_json(l.color);
        d["visible"] = l.visible;
        d["locked"] = l.locked;
        lays.push_back(std::move(d));
    }

    Json vars = Json::object();
    for (const auto& [k, v] : variables) vars[k] = v;

    Json jobs = Json::array();
    for (const auto& j : toolpaths) jobs.push_back(j->to_json());

    Json out = Json::object();
    out["version"] = 2;
    out["units"] = material.units;
    out["material"] = material.to_json();
    out["dimension_style"] = dimension_style_to_json(dimension_style);
    out["notes"] = notes;
    out["variables"] = std::move(vars);
    out["layers"] = std::move(lays);
    out["entities"] = std::move(ents);
    out["toolpaths"] = std::move(jobs);
    if (!preparation.empty()) out["preparation"] = preparation;
    return out;
}

void Document::from_json(const Json& data) {
    selection_order_.clear();
    preparation = data.contains("preparation") && data.at("preparation").is_object()
                      ? data.at("preparation") : Json::object();
    units = jget<std::string>(data, "units", "mm");
    dimension_style = dimension_style_from_json(data.contains("dimension_style") ? data.at("dimension_style") : Json());
    notes.clear();
    if (auto it = data.find("notes"); it != data.end() && it->is_string()) notes = it->get<std::string>();
    variables.clear();
    if (auto it = data.find("variables"); it != data.end() && it->is_object()) {
        for (auto kv = it->begin(); kv != it->end(); ++kv) {
            variables.set(kv.key(), kv.value().is_string() ? kv.value().get<std::string>() : kv.value().dump());
        }
    }

    const Json mat = data.contains("material") ? data.at("material") : Json::object();
    material = MaterialSetup::from_json(mat, units);
    units = material.units;

    layers.clear();
    if (auto it = data.find("layers"); it != data.end() && it->is_array()) {
        for (const Json& ld : *it) {
            Layer l;
            l.name = ld.at("name").get<std::string>();
            l.color = rgb_from_json(ld.contains("color") ? ld.at("color") : Json(), kDefaultColor);
            l.visible = jget<bool>(ld, "visible", true);
            l.locked = jget<bool>(ld, "locked", false);
            layers.push_back(std::move(l));
        }
    }
    if (layers.empty()) layers.push_back(Layer{});

    entities.clear();
    if (auto it = data.find("entities"); it != data.end() && it->is_array()) {
        for (const Json& ed : *it) {
            if (EntityPtr e = entity_from_json(ed, layers[0].name)) entities.push_back(std::move(e));
        }
    }

    toolpaths.clear();
    if (auto it = data.find("toolpaths"); it != data.end() && it->is_array()) {
        for (const Json& td : *it) {
            try {
                toolpaths.push_back(std::make_shared<cam::ToolpathJob>(cam::ToolpathJob::from_json(td)));
            } catch (const std::exception&) {
                continue;  // percurso invalido: o resto do projeto abre
            }
        }
    }
}

}  // namespace cnc::cad
