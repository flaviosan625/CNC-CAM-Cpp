// Modelo do documento/projeto - entidades, camadas e configuracao do material
// (porte de src/cad/document.py).
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/job.hpp"

namespace cnc::cad {

// dict[str, str] do Python: mantem a ordem de insercao (e a do arquivo gravado).
class OrderedStrMap {
public:
    using Item = std::pair<std::string, std::string>;

    void set(const std::string& key, const std::string& value);
    const std::string* get(const std::string& key) const;
    bool erase(const std::string& key);
    void clear() { items_.clear(); }
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    bool operator==(const OrderedStrMap&) const = default;

private:
    std::vector<Item> items_;
};

// Projeto CAD/CAM - entidades, camadas e material.
class Document {
public:
    Document();

    std::vector<EntityPtr> entities;
    std::vector<Layer> layers;
    MaterialSetup material;
    DimensionStyle dimension_style;
    std::vector<std::shared_ptr<cam::ToolpathJob>> toolpaths;
    // Recado livre que viaja com o projeto (menu Editar > Notas).
    std::string notes;
    // Variaveis do documento: nome -> valor, para carimbar em textos.
    OrderedStrMap variables;
    std::optional<std::filesystem::path> file_path;
    bool modified = false;
    std::string units = "mm";

    std::string title() const;

    // -- entidades --
    void add_entity(const EntityPtr& entity);
    EntityPtr remove_entity(const std::string& entity_id);
    EntityPtr find_entity(const std::string& entity_id) const;
    std::vector<EntityPtr> entities_on_layer(const std::string& name) const;

    // -- selecao --
    // As selecionadas e editaveis, na ordem em que foram selecionadas.
    std::vector<EntityPtr> selected_entities() const;
    bool is_editable(const Entity& entity) const;
    void set_selected(const EntityPtr& entity, bool selected);
    void clear_selection();
    void select_all();
    // Ordem em que as entidades foram selecionadas (ids). O desfazer a restaura.
    const std::vector<std::string>& selection_order() const { return selection_order_; }
    void set_selection_order(std::vector<std::string> ids) { selection_order_ = std::move(ids); }

    // -- camadas --
    // Ponteiros para dentro de `layers`: valem ate a proxima insercao/remocao.
    Layer* get_layer(const std::string& name);
    const Layer* get_layer(const std::string& name) const;
    Layer& add_layer(const std::string& name);
    bool remove_layer(const std::string& name);

    // -- percursos --
    std::shared_ptr<cam::ToolpathJob> add_toolpath(std::shared_ptr<cam::ToolpathJob> job);
    void remove_toolpath(const std::shared_ptr<cam::ToolpathJob>& job);
    // Reordena o percurso na lista - a ordem da lista e a ordem de corte.
    void move_toolpath(const std::shared_ptr<cam::ToolpathJob>& job, int delta);
    // Recalcula os percursos e devolve os erros encontrados.
    std::vector<std::string> recalculate_toolpaths(const std::shared_ptr<cam::ToolpathJob>& only = nullptr);
    std::string unique_toolpath_name(const std::string& base) const;

    // -- caixa do desenho --
    std::optional<BBox> bounding_box() const;
    // Mantem cotas de raio/diametro ligadas ao circulo ou arco de origem.
    void sync_dimensions();

    // -- persistencia (JSON .cnc v2) --
    // Sem caminho, grava em file_path. Lanca std::runtime_error se nao houver nenhum.
    void save(const std::optional<std::filesystem::path>& path = std::nullopt);
    void load(const std::filesystem::path& path);
    Json to_json() const;
    void from_json(const Json& data);

private:
    std::vector<std::string> selection_order_;
};

}  // namespace cnc::cad
