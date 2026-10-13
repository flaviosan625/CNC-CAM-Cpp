// Porte de src/cam/templates.py (e de seed/house_templates, de src/cam/presets.py).
#include "cam/templates.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "cam/presets.hpp"
#include "utils/user_config.hpp"

namespace cnc::cam {

std::filesystem::path templates_path() { return user_config::config_file("modelos.json"); }

ToolpathTemplate::ToolpathTemplate() = default;

ToolpathTemplate::ToolpathTemplate(ToolpathType k) : kind(k), settings(default_settings(k)) {}

std::string ToolpathTemplate::label() const { return std::format("{}  ·  {}", name, toolpath_label(kind)); }

namespace {

// getattr(settings, "cut_depth", None): so os tipos que tem o campo respondem.
std::optional<double> cut_depth_of(const Settings& s) {
    return std::visit(
        [](const auto& v) -> std::optional<double> {
            if constexpr (requires { v.cut_depth; }) {
                return v.cut_depth;
            } else {
                return std::nullopt;
            }
        },
        s);
}

// Pontes: so o perfil as tem.
const ProfileSettings* tabs_of(const Settings& s) {
    const auto* p = std::get_if<ProfileSettings>(&s);
    return (p && p->tabs) ? p : nullptr;
}

}  // namespace

std::string ToolpathTemplate::describe() const {
    std::vector<std::string> parts;
    parts.push_back(std::format("{} · {} ⌀{:g} mm", toolpath_label(kind), tool.name, tool.diameter));
    if (auto depth = cut_depth_of(settings)) {
        parts.push_back(std::format("profundidade {:g} mm, passo Z {:g} mm", *depth, tool.pass_depth));
    }
    if (const ProfileSettings* p = tabs_of(settings)) {
        parts.push_back(std::format("pontes {:g} × {:g} mm", p->tab_length, p->tab_thickness));
    }
    if (!notes.empty()) parts.push_back(notes);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += " · ";
        out += parts[i];
    }
    return out;
}

Json ToolpathTemplate::to_json() const {
    Json d = Json::object();
    d["name"] = name;
    d["kind"] = to_str(kind);
    d["notes"] = notes;
    d["material_thickness"] = material_thickness;
    d["tool"] = tool.to_json();
    d["settings"] = settings_to_json(settings);
    return d;
}

ToolpathTemplate ToolpathTemplate::from_json(const Json& data) {
    ToolpathType kind = ToolpathType::Profile;
    if (auto it = data.find("kind"); it != data.end() && it->is_string()) {
        kind = from_str<ToolpathType>(it->get<std::string>()).value_or(ToolpathType::Profile);
    }
    ToolpathTemplate t;
    t.name = jget<std::string>(data, "name", "Modelo");
    t.kind = kind;
    t.notes = jget<std::string>(data, "notes", "");
    t.material_thickness = jget<double>(data, "material_thickness", 0.0);
    t.tool = Tool::from_json(data.contains("tool") ? data.at("tool") : Json::object());
    t.settings = settings_from_json(kind, data.contains("settings") ? data.at("settings") : Json::object());
    return t;
}

ToolpathTemplate ToolpathTemplate::from_job(const ToolpathJob& job, const std::string& name, const std::string& notes) {
    ToolpathTemplate t;
    t.name = name.empty() ? job.name : name;
    t.kind = job.kind;
    t.tool = job.tool;
    t.settings = job.settings;
    t.notes = notes;
    return t;
}

std::string ToolpathTemplate::thickness_warning(const cad::MaterialSetup& material) const {
    if (material_thickness <= 0) return "";
    const double atual = material.thickness;
    if (std::fabs(atual - material_thickness) < 0.01) return "";
    return std::format(
        "Este modelo foi feito para chapa de {:g} mm e o trabalho esta com {:g} mm.\n\n"
        "A profundidade do modelo e {:g} mm: {}",
        material_thickness, atual, cut_depth_of(settings).value_or(0.0),
        atual > material_thickness ? "nao vai cortar passante." : "vai entrar na mesa.");
}

void ToolpathTemplate::apply_to(ToolpathJob& job) const {
    job.tool = tool;
    job.settings = settings;
    set_settings_name(job.settings, job.name);
}

// ----------------------------------------------------------------------
// Biblioteca
// ----------------------------------------------------------------------

TemplateLibrary::TemplateLibrary() : path(templates_path()) {}

TemplateLibrary TemplateLibrary::load(const std::filesystem::path& file, bool do_seed) {
    const std::filesystem::path target = file.empty() ? templates_path() : file;
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        TemplateLibrary library(target);
        if (do_seed) {
            seed(library);
            try {
                library.save();
            } catch (const std::exception&) {
            }
        }
        return library;
    }
    try {
        const Json data = Json::parse(read_text_file(target));
        TemplateLibrary library(target);
        if (auto it = data.find("templates"); it != data.end()) {
            for (const Json& d : *it) {
                library.templates.push_back(std::make_shared<ToolpathTemplate>(ToolpathTemplate::from_json(d)));
            }
        }
        return library;
    } catch (const std::exception&) {
        return TemplateLibrary(target);
    }
}

void TemplateLibrary::save() const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Json payload = Json::object();
    payload["version"] = 1;
    Json arr = Json::array();
    for (const Ptr& t : templates) arr.push_back(t->to_json());
    payload["templates"] = std::move(arr);
    write_text_file(path, dumps_pretty(payload));
}

TemplateLibrary::Ptr TemplateLibrary::find(const std::string& name) const {
    for (const Ptr& t : templates) {
        if (t->name == name) return t;
    }
    return nullptr;
}

TemplateLibrary::Ptr TemplateLibrary::add(ToolpathTemplate tpl) {
    auto stored = std::make_shared<ToolpathTemplate>(std::move(tpl));
    for (Ptr& t : templates) {
        if (t->name == stored->name) {
            t = stored;
            return stored;
        }
    }
    templates.push_back(stored);
    return stored;
}

void TemplateLibrary::remove(const Ptr& tpl) {
    templates.erase(std::remove(templates.begin(), templates.end(), tpl), templates.end());
}

std::vector<TemplateLibrary::Ptr> TemplateLibrary::for_kind(ToolpathType kind) const {
    std::vector<Ptr> out;
    for (const Ptr& t : templates) {
        if (t->kind == kind) out.push_back(t);
    }
    return out;
}

// ----------------------------------------------------------------------
// Modelos da casa
// ----------------------------------------------------------------------

std::vector<ToolpathTemplate> house_templates() {
    std::vector<ToolpathTemplate> out;
    for (const MaterialPreset& p : house_presets()) {
        ToolpathTemplate t;
        t.name = p.name();
        t.kind = ToolpathType::Profile;
        t.tool = p.tool();
        t.settings = p.settings();
        t.notes = p.note;
        t.material_thickness = p.thickness;
        out.push_back(std::move(t));
    }
    return out;
}

int seed(TemplateLibrary& library, bool overwrite) {
    int added = 0;
    for (ToolpathTemplate& t : house_templates()) {
        if (!overwrite && library.find(t.name)) continue;
        library.add(std::move(t));
        ++added;
    }
    return added;
}

}  // namespace cnc::cam
