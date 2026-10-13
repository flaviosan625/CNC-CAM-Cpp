// Percurso de rasgo de fixacao (keyhole) para pendurar pecas na parede
// (porte de src/cam/keyhole.py). O vetor e uma linha que define o rasgo; o ponto inicial
// da linha e o furo de entrada.
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

struct KeyholeSettings {
    static constexpr ToolpathType kKind = ToolpathType::Keyhole;

    std::string name = "Keyhole 1";
    double plunge_depth = 10.0;
    double slot_depth = 3.0;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, KeyholeSettings>
{
    f("name", s.name);
    f("plunge_depth", s.plunge_depth);
    f("slot_depth", s.slot_depth);
}

std::vector<Toolpath> generate_keyhole(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const KeyholeSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const KeyholeSettings& s) {
    return generate_keyhole(entities, tool, material, s);
}

}  // namespace cnc::cam
