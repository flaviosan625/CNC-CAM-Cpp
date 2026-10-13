// Percurso de bolso: esvaziar uma area fechada (porte de src/cam/pocket.py).
//
// Dois padroes de preenchimento, como no Vectric: Offset (aneis concentricos) e Raster
// (varredura paralela num angulo). Ilhas sao respeitadas - um furo dentro do bolso vira
// material que fica.
//
// Dois cuidados que fazem diferenca no resultado:
//  * Cada anel e calculado a partir do contorno original, nao do anel anterior. Offset
//    sobre offset acumula distorcao nos cantos.
//  * A ligacao entre aneis so corta em linha reta quando a reta inteira cabe dentro do
//    bolso; caso contrario levanta e desloca. Sem isso a fresa atravessa ilha.
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/nesting.hpp"
#include "cam/profile.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"
#include "geo/geom.hpp"

namespace cnc::cam {

struct PocketSettings {
    static constexpr ToolpathType kKind = ToolpathType::Pocket;

    std::string name = "Bolso 1";

    double start_depth = 0.0;
    double cut_depth = 6.0;
    bool maintain_exact_step = false;

    FillType fill = FillType::Offset;
    double raster_angle = 0.0;  // 0 graus = paralelo ao eixo X
    CutDirection direction = CutDirection::Conventional;
    double stepover = 0.0;  // 0 = usar o da ferramenta
    ProfilePass profile_pass = ProfilePass::Last;
    double allowance = 0.2;  // material deixado para a passada de perfil

    RampType ramp = RampType::Smooth;
    double ramp_distance = 10.0;

    MachineOrder order = MachineOrder::Shortest;
    double tolerance = kDefaultTolerance;

    double total_depth() const { return std::max(0.0, cut_depth); }
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, PocketSettings>
{
    f("name", s.name);
    f("start_depth", s.start_depth);
    f("cut_depth", s.cut_depth);
    f("maintain_exact_step", s.maintain_exact_step);
    f("fill", s.fill);
    f("raster_angle", s.raster_angle);
    f("direction", s.direction);
    f("stepover", s.stepover);
    f("profile_pass", s.profile_pass);
    f("allowance", s.allowance);
    f("ramp", s.ramp);
    f("ramp_distance", s.ramp_distance);
    f("order", s.order);
    f("tolerance", s.tolerance);
}

std::vector<Toolpath> generate_pocket(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const PocketSettings& s);

// Gera um bolso a partir de regioes prontas. O V-Carve usa esta entrada para desbastar a
// mesma regiao sem converter a geometria de volta em entidades CAD e sem perder ilhas.
std::vector<Toolpath> generate_pocket_polygons(const std::vector<geo::Geom>& polys, const Tool& tool,
                                               const cad::MaterialSetup& material, const PocketSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const PocketSettings& s) {
    return generate_pocket(entities, tool, material, s);
}

}  // namespace cnc::cam
