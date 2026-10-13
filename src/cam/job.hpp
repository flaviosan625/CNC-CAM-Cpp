// Percurso como item do projeto (porte de src/cam/job.py).
//
// Um percurso e guardado no arquivo pela DEFINICAO - tipo, ferramenta, parametros e quais
// vetores ele usa - e nao pelos milhares de coordenadas calculadas. O arquivo fica pequeno,
// o percurso acompanha a edicao do desenho e "Recalcular" passa a significar alguma coisa.
#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "cad/entities.hpp"
#include "cam/dragknife.hpp"
#include "cam/drill.hpp"
#include "cam/fluting.hpp"
#include "cam/inlay.hpp"
#include "cam/keyhole.hpp"
#include "cam/model.hpp"
#include "cam/pocket.hpp"
#include "cam/profile.hpp"
#include "cam/texture.hpp"
#include "cam/tools.hpp"
#include "cam/vcarve.hpp"

namespace cnc::cad {
class Document;
}

namespace cnc::cam {

// Um alternativa por tipo de percurso (os nove do app).
using Settings = std::variant<ProfileSettings, PocketSettings, DrillSettings, VCarveSettings, FlutingSettings,
                              TextureSettings, KeyholeSettings, InlaySettings, DragknifeSettings>;

ToolpathType settings_kind(const Settings& s);
Settings default_settings(ToolpathType kind);
Json settings_to_json(const Settings& s);
// Campo desconhecido ou valor invalido e ignorado: arquivo de versao anterior continua abrindo.
Settings settings_from_json(ToolpathType kind, const Json& data);
void set_settings_name(Settings& s, const std::string& name);

// Definicao de um percurso dentro do projeto.
struct ToolpathJob {
    std::string name = "Percurso 1";
    ToolpathType kind = ToolpathType::Profile;
    Tool tool;
    Settings settings = ProfileSettings{};
    std::vector<std::string> entity_ids;
    bool visible = true;
    std::string id = new_id();

    // Percursos calculados - nao vao para o arquivo.
    std::vector<Toolpath> results;
    std::string error;

    const char* type_label() const { return toolpath_label(kind); }
    bool calculated() const { return !results.empty(); }

    // Os vetores deste percurso - cota e guia ficam de fora sempre. O filtro se repete
    // aqui, e nao so na hora de criar o percurso: um projeto antigo pode trazer id de cota
    // gravado, e recalcular nao pode ressuscitar o engano.
    std::vector<cad::EntityPtr> entities(const cad::Document& document) const;
    // Recalcula o percurso a partir do desenho atual.
    const std::vector<Toolpath>& calculate(const cad::Document& document);
    void set_visible(bool v);
    std::string summary() const;
    std::vector<std::string> all_notes() const;

    Json to_json() const;
    static ToolpathJob from_json(const Json& data);
};

// Todos os percursos calculados e visiveis, na ordem da lista.
std::vector<const Toolpath*> calculated_toolpaths(
    const std::vector<std::shared_ptr<ToolpathJob>>& jobs);

}  // namespace cnc::cam
