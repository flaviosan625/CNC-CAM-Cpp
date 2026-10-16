#include "app/main_window.hpp"

#include <algorithm>
#include <map>
#include <QTabBar>

#include "cam/presets.hpp"
#include "cad/selection.hpp"
#include "io/import_operations.hpp"
#include "ui/import_setup_panel.hpp"
#include "viewport/canvas_2d.hpp"

namespace cnc::app {

void MainWindow::classify_imported_entities(const std::vector<cad::EntityPtr>& entities) {
    if (!document_.preparation.contains("roles") || !document_.preparation["roles"].is_object())
        document_.preparation["roles"] = Json::object();
    std::map<std::string, cad::EntityPtr> by_id;
    for (const auto& entity : entities) if (entity) by_id.emplace(entity->id, entity);

    auto is_recess_layer_name = [](const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.find("rebaixo") != std::string::npos ||
               lower.find("pocket") != std::string::npos ||
               lower.find("cava") != std::string::npos ||
               lower.find("recess") != std::string::npos ||
               lower.find("desbaste") != std::string::npos ||
               lower.find("usinagem") != std::string::npos;
    };

    const auto assignments = io::classify_imported_operations(entities);
    for (const auto& assignment : assignments) {
        const auto found = by_id.find(assignment.entity_id);
        if (found == by_id.end()) continue;
        auto& entity = found->second;
        if (is_recess_layer_name(entity->layer)) {
            if (!document_.get_layer("REBAIXO")) document_.add_layer("REBAIXO");
            entity->layer = "REBAIXO";
            document_.preparation["roles"][entity->id] = "recess";
        } else {
            if (!document_.get_layer(assignment.layer_name)) document_.add_layer(assignment.layer_name);
            entity->layer = assignment.layer_name;
            const char* role = assignment.operation == io::ImportedOperation::CutExternal ? "external" :
                               assignment.operation == io::ImportedOperation::CutInternal ? "internal" :
                               assignment.operation == io::ImportedOperation::Open ? "open" : "review";
            document_.preparation["roles"][assignment.entity_id] = role;
        }
    }
    document_.modified = true;
}

void MainWindow::refresh_import_preparation() {
    if (!import_setup_panel_) return;
    std::vector<cam::Tool> tools;
    for (const auto& tool : cam::ToolDatabase::load().tools)
        if (tool && cam::is_milling_type(tool->tool_type)) tools.push_back(*tool);
    if (tools.empty()) tools = cam::house_tools();
    std::optional<cam::Tool> preferred;
    try {
        if (document_.preparation.contains("tool"))
            preferred = cam::Tool::from_json(document_.preparation.at("tool"));
    } catch (const std::exception&) { }
    if (!preferred) {
        const auto preset = cam::find_preset(document_.material.material_profile, document_.material.thickness);
        if (preset) preferred = preset->tool();
    }
    import_setup_panel_->set_material(document_.material);
    import_setup_panel_->set_tools(tools, preferred);
    if (document_.preparation.contains("has_recess") && document_.preparation["has_recess"].is_boolean())
        import_setup_panel_->set_recess_answer(document_.preparation["has_recess"].get<bool>());
    else import_setup_panel_->reset_recess_answer();
    import_setup_panel_->set_recess_depth(jget<double>(document_.preparation, "recess_depth", 3.0));
}

void MainWindow::begin_import_preparation() {
    refresh_import_preparation();
    if (!import_setup_panel_) return;
    import_preparation_pending_ = true;

    // Detecta se entidades ja vieram como rebaixo
    const Json roles = document_.preparation.value("roles", Json::object());
    bool cad_has_recess = false;
    for (const auto& entity : document_.entities) {
        if (!entity) continue;
        const std::string role = roles.is_object() ? jget<std::string>(roles, entity->id.c_str(), "") : "";
        if (role == "recess" || entity->layer == "REBAIXO") {
            cad_has_recess = true;
            break;
        }
    }

    if (cad_has_recess) {
        document_.preparation["has_recess"] = true;
        import_setup_panel_->set_recess_answer(true);
    } else {
        document_.preparation.erase("has_recess");
        import_setup_panel_->reset_recess_answer();
    }

    // Calcula dimensoes da geometria para o banner e auto-fit
    const auto bbox = document_.bounding_box();
    int closed_count = 0;
    for (const auto& entity : document_.entities) {
        if (entity && entity->closed() && cad::is_vector(*entity) && !cad::is_a<cad::Text>(*entity)) {
            ++closed_count;
        }
    }
    if (bbox) {
        import_setup_panel_->set_geometry_info(bbox->x1 - bbox->x0, bbox->y1 - bbox->y0, closed_count,
                                              static_cast<int>(document_.entities.size()));
    }

    // Pre-seleciona o preset correspondente se disponivel
    const auto preset = cam::find_preset(document_.material.material_profile, document_.material.thickness);
    if (preset) {
        const auto& presets = cam::house_presets();
        for (size_t i = 0; i < presets.size(); ++i) {
            if (presets[i].material == preset->material &&
                std::fabs(presets[i].thickness - preset->thickness) < 0.01) {
                import_setup_panel_->select_house_preset(static_cast<int>(i));
                break;
            }
        }
    }

    import_setup_panel_->set_status(
        "Desenho importado. Selecione o preset de corte e clique em Gerar Corte Automático.");
    if (view_tabs_) view_tabs_->setCurrentIndex(0);
    activate_tool("select");
    show_right_page(3);
    automatic_cut_panel_dirty_ = true;
    refresh_automatic_cut_panel();
}

bool MainWindow::apply_import_preparation() {
    if (!import_setup_panel_) return false;
    if (!import_setup_panel_->recess_answered()) {
        import_setup_panel_->set_status("Responda à pergunta: este trabalho tem rebaixo?");
        show_right_page(3);
        return false;
    }
    const auto values = import_setup_panel_->values();
    if (values.material.width <= 0 || values.material.height <= 0 || values.material.thickness <= 0 ||
        values.tool.diameter <= 0 || values.tool.pass_depth <= 0 || values.tool.feed_rate <= 0) {
        import_setup_panel_->set_status("Informe dimensões positivas e uma fresa com diâmetro, passo Z e avanço válidos.");
        return false;
    }
    if (values.has_recess && (values.recess_depth <= 0 || values.recess_depth >= values.material.thickness)) {
        import_setup_panel_->set_status("O rebaixo precisa ter profundidade maior que zero e menor que a espessura da chapa.");
        return false;
    }
    const bool material_changed = document_.material.to_json() != values.material.to_json();
    document_.material = values.material;
    document_.units = values.material.units;
    document_.preparation["tool"] = values.tool.to_json();
    document_.preparation["has_recess"] = values.has_recess;
    document_.preparation["recess_depth"] = values.recess_depth;
    document_.modified = true;
    import_preparation_pending_ = false;
    if (material_changed) {
        for (const auto& job : document_.toolpaths) if (job) { job->results.clear(); job->error.clear(); }
        refresh_document_view();
    } else refresh_toolpaths();
    const auto existing_roles = document_.preparation.value("roles", Json::object());
    const bool has_marked_recess = existing_roles.is_object() &&
        std::any_of(existing_roles.begin(), existing_roles.end(), [](const auto& role) { return role == "recess"; });
    if (values.has_recess && !has_marked_recess && !document_.get_layer("REBAIXO")) {
        document_.add_layer("REBAIXO");
        refresh_layers();
    }
    automatic_cut_panel_dirty_ = true;
    refresh_automatic_cut_panel();
    import_setup_panel_->set_status(values.has_recess
        ? "Chapa aplicada. Selecione os contornos do rebaixo no desenho e use Marcar seleção como rebaixo."
        : "Chapa aplicada. Posicione o desenho, selecione os vetores e escolha o percurso desejado.");
    return true;
}

void MainWindow::assign_selected_recess() {
    if (!apply_import_preparation()) return;
    const auto values = import_setup_panel_->values();
    if (!values.has_recess) return;
    const auto selected = document_.selected_entities();
    if (selected.empty() || std::any_of(selected.begin(), selected.end(), [](const auto& entity) {
            return !entity || !entity->closed() || !cad::is_vector(*entity) || cad::is_a<cad::Text>(*entity);
        })) {
        import_setup_panel_->set_status("Selecione apenas contornos fechados das áreas de rebaixo. Inclua os contornos das ilhas que devem permanecer.");
        return;
    }
    std::vector<std::string> ids;
    for (const auto& entity : selected) ids.push_back(entity->id);
    std::sort(ids.begin(), ids.end());
    std::shared_ptr<cam::ToolpathJob> job;
    const auto registered = document_.preparation.value("recess_jobs", Json::array());
    for (const auto& candidate : document_.toolpaths) {
        if (!candidate || candidate->kind != cam::ToolpathType::Pocket ||
            !registered.is_array() || std::find(registered.begin(), registered.end(), Json(candidate->id)) == registered.end()) continue;
        auto old_ids = candidate->entity_ids;
        std::sort(old_ids.begin(), old_ids.end());
        if (old_ids == ids) { job = candidate; break; }
        if (std::any_of(ids.begin(), ids.end(), [&](const auto& id) {
                return std::find(old_ids.begin(), old_ids.end(), id) != old_ids.end();
            })) {
            import_setup_panel_->set_status("Parte da seleção já pertence a outro rebaixo. Selecione o mesmo conjunto para atualizá-lo ou edite o percurso existente.");
            return;
        }
    }
    const bool is_new = !job;
    if (is_new) {
        job = std::make_shared<cam::ToolpathJob>();
        job->kind = cam::ToolpathType::Pocket;
        job->name = document_.unique_toolpath_name("Desbaste");
    }
    job->tool = values.tool;
    cam::PocketSettings settings;
    settings.name = job->name;
    settings.cut_depth = values.recess_depth;
    settings.allowance = 0.0;
    job->settings = settings;
    job->entity_ids = ids;
    job->results.clear();
    job->error.clear();
    if (is_new) {
        document_.add_toolpath(job);
        if (!document_.preparation.contains("recess_jobs") || !document_.preparation["recess_jobs"].is_array())
            document_.preparation["recess_jobs"] = Json::array();
        document_.preparation["recess_jobs"].push_back(job->id);
    }
    const std::string recess_layer = !is_new && !selected.empty() ? selected.front()->layer : "REBAIXO";
    if (!document_.get_layer(recess_layer)) document_.add_layer(recess_layer);
    if (!document_.preparation.contains("roles") || !document_.preparation["roles"].is_object())
        document_.preparation["roles"] = Json::object();
    for (const auto& entity : selected) {
        entity->layer = recess_layer;
        document_.preparation["roles"][entity->id] = "recess";
    }
    // Uma area reclassificada nao pode continuar num corte passante ja calculado.
    for (const auto& other : document_.toolpaths) {
        if (!other || other->kind != cam::ToolpathType::Profile) continue;
        const auto previous_size = other->entity_ids.size();
        other->entity_ids.erase(std::remove_if(other->entity_ids.begin(), other->entity_ids.end(), [&](const auto& id) {
            return std::binary_search(ids.begin(), ids.end(), id);
        }), other->entity_ids.end());
        if (other->entity_ids.size() != previous_size) { other->results.clear(); other->error.clear(); }
    }
    auto it = std::find(document_.toolpaths.begin(), document_.toolpaths.end(), job);
    if (it != document_.toolpaths.end()) { document_.toolpaths.erase(it); document_.toolpaths.insert(document_.toolpaths.begin(), job); }
    document_.modified = true;
    refresh_layers();
    canvas_widget_->canvas->refresh_entities();
    refresh_toolpaths();
    import_setup_panel_->set_status(QString("%1 contorno(s) marcado(s) como rebaixo de %2 mm. Gere o desbaste ou selecione outro conjunto.")
        .arg(ids.size()).arg(values.recess_depth, 0, 'g', 8));
}

bool MainWindow::calculate_prepared_recesses(const std::vector<std::string>& scope) {
    const auto registered = document_.preparation.value("recess_jobs", Json::array());
    if (!registered.is_array()) return true;
    std::vector<std::shared_ptr<cam::ToolpathJob>> pockets;
    for (const auto& job : document_.toolpaths) {
        if (!job || job->kind != cam::ToolpathType::Pocket ||
            std::find(registered.begin(), registered.end(), Json(job->id)) == registered.end()) continue;
        if (!job->visible) continue;
        const auto entities = job->entities(document_);
        if (entities.empty() || std::any_of(entities.begin(), entities.end(), [this](const auto& entity) {
                return !document_.is_editable(*entity);
            })) continue;
        if (!scope.empty() && std::none_of(job->entity_ids.begin(), job->entity_ids.end(), [&](const auto& id) {
                return std::find(scope.begin(), scope.end(), id) != scope.end();
            })) continue;
        const auto* settings = std::get_if<cam::PocketSettings>(&job->settings);
        if (!settings || settings->start_depth < 0 || settings->cut_depth <= 0 ||
            settings->start_depth + settings->cut_depth >= document_.material.thickness) {
            if (import_setup_panel_) import_setup_panel_->set_status("Revise a profundidade dos rebaixos: eles devem preservar o fundo da chapa.");
            return false;
        }
        const auto errors = document_.recalculate_toolpaths(job);
        if (!errors.empty()) {
            if (import_setup_panel_) import_setup_panel_->set_status(QString::fromStdString(errors.front()));
            return false;
        }
        pockets.push_back(job);
    }
    for (auto it = pockets.rbegin(); it != pockets.rend(); ++it) {
        auto pos = std::find(document_.toolpaths.begin(), document_.toolpaths.end(), *it);
        document_.toolpaths.erase(pos);
        document_.toolpaths.insert(document_.toolpaths.begin(), *it);
    }
    return true;
}

void MainWindow::generate_prepared_recesses() {
    if (!apply_import_preparation()) return;
    if (!document_.preparation.contains("recess_jobs") || document_.preparation["recess_jobs"].empty()) {
        import_setup_panel_->set_status("Selecione as áreas no desenho e marque o rebaixo antes de gerar.");
        return;
    }
    if (!calculate_prepared_recesses()) { refresh_toolpaths(); return; }
    document_.modified = true;
    refresh_toolpaths();
    import_setup_panel_->set_status("Desbastes calculados. Posicione e selecione os demais vetores para gerar os cortes.");
}

}  // namespace cnc::app
