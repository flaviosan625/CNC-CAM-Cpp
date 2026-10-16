// Porte de src/cam/job.py.
#include "cam/job.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "cad/document.hpp"
#include "cad/selection.hpp"
#include "utils/errors.hpp"

namespace cnc::cam {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Os tipos de percurso ja portados. Cada novo gerador entra aqui (e no Settings do job.hpp).
using PortedSettings =
    std::tuple<ProfileSettings, PocketSettings, DrillSettings, VCarveSettings, FlutingSettings, TextureSettings,
               KeyholeSettings, InlaySettings, DragknifeSettings>;

// Chama f(std::type_identity<S>) para o tipo de settings que corresponde a `kind`.
// Devolve false se esse tipo de percurso ainda nao foi portado.
template <class F>
bool with_settings_type(ToolpathType kind, F&& f) {
    bool done = false;
    auto try_one = [&]<class S>(std::type_identity<S>) {
        if (!done && S::kKind == kind) {
            f(std::type_identity<S>{});
            done = true;
        }
    };
    std::apply([&](auto... t) { (try_one(std::type_identity<decltype(t)>{}), ...); }, PortedSettings{});
    return done;
}

}  // namespace

ToolpathType settings_kind(const Settings& s) {
    return std::visit([](const auto& v) { return std::decay_t<decltype(v)>::kKind; }, s);
}

Settings default_settings(ToolpathType kind) {
    Settings out = ProfileSettings{};
    with_settings_type(kind, [&](auto id) { out = typename decltype(id)::type{}; });
    return out;
}

Json settings_to_json(const Settings& s) {
    return std::visit([](const auto& v) { return io::settings_to_json(v); }, s);
}

Settings settings_from_json(ToolpathType kind, const Json& data) {
    Settings out = ProfileSettings{};
    with_settings_type(kind, [&](auto id) {
        using S = typename decltype(id)::type;
        out = io::settings_from_json<S>(data);
    });
    return out;
}

void set_settings_name(Settings& s, const std::string& name) {
    std::visit([&](auto& v) { v.name = name; }, s);
}

namespace {

std::vector<Toolpath> run_generator(const Settings& settings, const std::vector<cad::EntityPtr>& ents,
                                    const Tool& tool, const cad::MaterialSetup& material) {
    return std::visit([&](const auto& s) { return generate(ents, tool, material, s); }, settings);
}

}  // namespace

std::vector<cad::EntityPtr> ToolpathJob::entities(const cad::Document& document) const {
    const std::set<std::string> wanted(entity_ids.begin(), entity_ids.end());
    std::vector<cad::EntityPtr> chosen;
    for (const cad::EntityPtr& e : document.entities) {
        if (wanted.count(e->id)) chosen.push_back(e);
    }
    return cad::machinable_for(chosen, kind);
}

const std::vector<Toolpath>& ToolpathJob::calculate(const cad::Document& document) {
    error.clear();
    results.clear();
    const std::vector<cad::EntityPtr> ents = entities(document);
    if (ents.empty()) {
        error = "Os vetores deste percurso não estão mais no desenho.";
        return results;
    }
    set_settings_name(settings, name);
    try {
        results = run_generator(settings, ents, tool, document.material);
        const bool any_moves = std::any_of(results.begin(), results.end(),
                                           [](const Toolpath& tp) { return !tp.moves.empty(); });
        if (results.empty() || !any_moves) {
            std::string expected = "vetores compatíveis com a operação";
            switch (kind) {
                case ToolpathType::Fluting: expected = "um vetor com comprimento"; break;
                case ToolpathType::Texture: expected = "uma área vetorial válida"; break;
                case ToolpathType::Keyhole: expected = "uma linha aberta indicando o rasgo"; break;
                case ToolpathType::Inlay: expected = "ao menos um vetor fechado"; break;
                case ToolpathType::Dragknife: expected = "um vetor aberto ou fechado"; break;
                default: break;
            }
            error = "Nenhum movimento foi gerado. Selecione " + expected + ".";
            results.clear();
        }
    } catch (const ValueError& ex) {
        error = std::string("ValueError: ") + ex.what();
        results.clear();
    } catch (const std::exception& ex) {
        error = std::string("Exception: ") + ex.what();
        results.clear();
    }
    for (Toolpath& tp : results) {
        tp.visible = visible;
        tp.kind = kind;
        if (kind == ToolpathType::Profile && std::holds_alternative<ProfileSettings>(settings)) {
            const auto& ps = std::get<ProfileSettings>(settings);
            if (tp.name.find("internos") != std::string::npos) tp.profile_side = ProfileSide::Inside;
            else if (tp.name.find("externos") != std::string::npos) tp.profile_side = ProfileSide::Outside;
            else tp.profile_side = ps.side;
        }
    }
    return results;
}

void ToolpathJob::set_visible(bool v) {
    visible = v;
    for (Toolpath& tp : results) tp.visible = v;
}

std::string ToolpathJob::summary() const {
    if (!error.empty()) return error;
    if (results.empty()) return "Não calculado";
    if (results.size() == 1) return results[0].summary();
    const double seconds = py_sum(results, [](const Toolpath& t) { return t.estimated_seconds(); });
    return std::format("{} percursos · ≈ {:.0f} min", results.size(), seconds / 60);
}

std::vector<std::string> ToolpathJob::all_notes() const {
    std::vector<std::string> seen;
    for (const Toolpath& tp : results) {
        for (const std::string& note : tp.notes) {
            if (std::find(seen.begin(), seen.end(), note) == seen.end()) seen.push_back(note);
        }
    }
    return seen;
}

Json ToolpathJob::to_json() const {
    Json d = Json::object();
    d["id"] = id;
    d["name"] = name;
    d["kind"] = to_str(kind);
    d["visible"] = visible;
    d["tool"] = tool.to_json();
    d["settings"] = settings_to_json(settings);
    d["entity_ids"] = entity_ids;
    return d;
}

ToolpathJob ToolpathJob::from_json(const Json& data) {
    ToolpathJob job;
    job.kind = ToolpathType::Profile;
    if (auto it = data.find("kind"); it != data.end() && it->is_string()) {
        job.kind = from_str<ToolpathType>(it->get<std::string>()).value_or(ToolpathType::Profile);
    }
    job.settings = settings_from_json(job.kind, data.contains("settings") ? data.at("settings") : Json::object());
    job.id = jget<std::string>(data, "id", new_id());
    job.name = jget<std::string>(data, "name", "Percurso");
    job.visible = jget<bool>(data, "visible", true);
    job.tool = Tool::from_json(data.contains("tool") ? data.at("tool") : Json::object());
    job.entity_ids = jget<std::vector<std::string>>(data, "entity_ids", {});
    return job;
}

std::vector<const Toolpath*> calculated_toolpaths(const std::vector<std::shared_ptr<ToolpathJob>>& jobs) {
    std::vector<const Toolpath*> out;
    for (const auto& job : jobs) {
        if (!job->visible) continue;
        for (const Toolpath& t : job->results) {
            if (t.visible) out.push_back(&t);
        }
    }
    return out;
}

}  // namespace cnc::cam
