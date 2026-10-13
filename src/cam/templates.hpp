// Modelos de percurso - parametros salvos para reaplicar em outro trabalho
// (porte de src/cam/templates.py).
//
// E o equivalente aos gadgets de "Corte Automatico" por material: em vez de reconfigurar fresa,
// passo, avanco e pontes toda vez que entra uma chapa de MDF 15, grava-se uma vez e aplica-se.
// O modelo guarda a ferramenta e os parametros, nunca a geometria.
//
// Arquivo: ~/.cnc-cam/modelos.json
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cad/material.hpp"
#include "cam/job.hpp"
#include "cam/model.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

// Caminho do arquivo (funcao, nao constante: a pasta de configuracao pode ser trocada em teste).
std::filesystem::path templates_path();

struct ToolpathTemplate {
    std::string name = "Modelo";
    ToolpathType kind = ToolpathType::Profile;
    Tool tool;
    Settings settings = ProfileSettings{};
    std::string notes;
    // Espessura de chapa para a qual o modelo foi feito (0 = qualquer). Serve para avisar quando o
    // trabalho aberto tem outra espessura: aplicar "MDF 15" numa chapa de 18 nao corta passante.
    double material_thickness = 0.0;

    ToolpathTemplate();
    explicit ToolpathTemplate(ToolpathType k);

    std::string label() const;
    std::string describe() const;

    Json to_json() const;
    static ToolpathTemplate from_json(const Json& data);
    static ToolpathTemplate from_job(const ToolpathJob& job, const std::string& name = "",
                                     const std::string& notes = "");

    // Aviso quando a chapa do trabalho nao e a do modelo ("" se esta tudo certo).
    std::string thickness_warning(const cad::MaterialSetup& material) const;
    // Copia ferramenta e parametros para um percurso, mantendo os vetores.
    void apply_to(ToolpathJob& job) const;
};

class TemplateLibrary {
public:
    using Ptr = std::shared_ptr<ToolpathTemplate>;

    std::vector<Ptr> templates;
    std::filesystem::path path;

    TemplateLibrary();
    explicit TemplateLibrary(std::filesystem::path file) : path(std::move(file)) {}

    // Carrega a biblioteca; na primeira vez ja vem com os modelos da casa.
    static TemplateLibrary load(const std::filesystem::path& file = {}, bool seed = true);
    void save() const;

    Ptr find(const std::string& name) const;
    // Substitui o modelo de mesmo nome, ou acrescenta.
    Ptr add(ToolpathTemplate tpl);
    void remove(const Ptr& tpl);
    std::vector<Ptr> for_kind(ToolpathType kind) const;
};

// Os nove presets da casa como modelos de percurso prontos para aplicar.
std::vector<ToolpathTemplate> house_templates();
// Poe os modelos da casa na biblioteca. Devolve quantos entraram.
int seed(TemplateLibrary& library, bool overwrite = false);

}  // namespace cnc::cam
