// Percurso de inlay - gera bolso na base e plug de recorte automaticamente
// (porte de src/cam/inlay.py). Dos mesmos vetores saem dois conjuntos de percursos:
//  1. Bolso na peca base;
//  2. Plug de recorte na peca incrustada (perfil externo com folga negativa).
// O plug pode ter uma sobra (glue allowance) para colar e depois lixar.
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

struct InlaySettings {
    static constexpr ToolpathType kKind = ToolpathType::Inlay;

    std::string name = "Inlay 1";
    double pocket_depth = 3.0;
    double plug_depth = 5.0;
    double glue_allowance = 0.1;
    double start_depth = 0.0;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, InlaySettings>
{
    f("name", s.name);
    f("pocket_depth", s.pocket_depth);
    f("plug_depth", s.plug_depth);
    f("glue_allowance", s.glue_allowance);
    f("start_depth", s.start_depth);
}

std::vector<Toolpath> generate_inlay(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                     const cad::MaterialSetup& material, const InlaySettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const InlaySettings& s) {
    return generate_inlay(entities, tool, material, s);
}

}  // namespace cnc::cam
