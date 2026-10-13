// Percurso de canais (fluting) - canais decorativos com fresa esferica
// (porte de src/cam/fluting.py). Cada vetor vira um canal: a fresa desce ate a profundidade
// no inicio, percorre o caminho e sobe no fim. O canal pode afunilar (profundidade final).
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

struct FlutingSettings {
    static constexpr ToolpathType kKind = ToolpathType::Fluting;

    std::string name = "Fluting 1";
    double start_depth = 0.0;
    double cut_depth = 3.0;
    double end_depth = 0.0;
    double ramp_in = 5.0;
    double ramp_out = 5.0;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, FlutingSettings>
{
    f("name", s.name);
    f("start_depth", s.start_depth);
    f("cut_depth", s.cut_depth);
    f("end_depth", s.end_depth);
    f("ramp_in", s.ramp_in);
    f("ramp_out", s.ramp_out);
}

std::vector<Toolpath> generate_fluting(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const FlutingSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const FlutingSettings& s) {
    return generate_fluting(entities, tool, material, s);
}

// Pontos da entidade; fechada de verdade (com o primeiro ponto repetido no fim) quando for o
// caso. Compartilhado com a faca de arraste, que usa a mesma regra.
Points path_points_of(const cad::Entity& e);

}  // namespace cnc::cam
