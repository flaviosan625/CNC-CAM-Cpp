// Percurso de furacao, com ciclo peck (porte de src/cam/drill.py).
//
// A broca desce ate o passo definido na ferramenta, recua para soltar o cavaco e volta a
// descer, ate a profundidade pedida. O recuo tem os dois modos do Vectric:
//  * Acima do inicio do corte - sai inteiro do furo a cada passo. Limpa de verdade, mas o
//    percurso cresce conforme o furo fica fundo.
//  * Acima da passada anterior - recuo constante. Mais rapido, nem sempre tira todo o cavaco.
// Acrescimo nosso: furo maior que a fresa pode sair helicoidal, em vez de exigir uma broca
// do diametro exato.
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

namespace cnc::cam {

enum class RetractMode { AboveStart, AbovePrevious };
const char* retract_label(RetractMode m);

}  // namespace cnc::cam

namespace cnc {
CNC_ENUM(cam::RetractMode, {cam::RetractMode::AboveStart, "above_start"},
         {cam::RetractMode::AbovePrevious, "above_previous"});
}  // namespace cnc

namespace cnc::cam {

struct DrillSettings {
    static constexpr ToolpathType kKind = ToolpathType::Drill;

    std::string name = "Furação 1";

    double start_depth = 0.0;
    double cut_depth = 18.0;

    bool peck = true;
    double retract_gap = 2.0;
    RetractMode retract_mode = RetractMode::AbovePrevious;
    bool dwell = false;
    double dwell_time = 0.2;

    bool helical = true;  // furo maior que a fresa sai helicoidal
    CutDirection helical_direction = CutDirection::Conventional;

    MachineOrder order = MachineOrder::Shortest;
    double tolerance = kDefaultTolerance;

    double total_depth() const { return std::max(0.0, cut_depth); }
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, DrillSettings>
{
    f("name", s.name);
    f("start_depth", s.start_depth);
    f("cut_depth", s.cut_depth);
    f("peck", s.peck);
    f("retract_gap", s.retract_gap);
    f("retract_mode", s.retract_mode);
    f("dwell", s.dwell);
    f("dwell_time", s.dwell_time);
    f("helical", s.helical);
    f("helical_direction", s.helical_direction);
    f("order", s.order);
    f("tolerance", s.tolerance);
}

std::vector<Toolpath> generate_drill(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                     const cad::MaterialSetup& material, const DrillSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const DrillSettings& s) {
    return generate_drill(entities, tool, material, s);
}

}  // namespace cnc::cam
