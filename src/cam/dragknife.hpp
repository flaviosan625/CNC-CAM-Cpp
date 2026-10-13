// Percurso de faca de arraste (drag knife) - compensa o offset da lamina
// (porte de src/cam/dragknife.py). A lamina fica atras do eixo da ferramenta: para cortar um
// canto, ela precisa ser retraida, girada no canto e mergulhar de novo.
//
//  * blade_offset    - distancia do centro do eixo ao fio da lamina (mm)
//  * tolerance_angle - cantos mais suaves que este angulo passam direto (graus)
//  * swivel_depth    - profundidade de giro no canto (mais rasa que o corte)
//  * cut_depth       - profundidade de corte
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

struct DragknifeSettings {
    static constexpr ToolpathType kKind = ToolpathType::Dragknife;

    std::string name = "Faca de arraste 1";
    double blade_offset = 2.0;
    double tolerance_angle = 30.0;
    double swivel_depth = 0.5;
    double cut_depth = 2.0;
    int passes = 1;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, DragknifeSettings>
{
    f("name", s.name);
    f("blade_offset", s.blade_offset);
    f("tolerance_angle", s.tolerance_angle);
    f("swivel_depth", s.swivel_depth);
    f("cut_depth", s.cut_depth);
    f("passes", s.passes);
}

std::vector<Toolpath> generate_dragknife(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                         const cad::MaterialSetup& material, const DragknifeSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const DragknifeSettings& s) {
    return generate_dragknife(entities, tool, material, s);
}

}  // namespace cnc::cam
