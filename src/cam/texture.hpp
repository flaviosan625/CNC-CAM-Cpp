// Percurso de textura - linhas aleatorizadas com fresa esferica
// (porte de src/cam/texture.py). Reproduz o Vector Texture Toolpath do Aspire: preenche uma
// regiao com tracos de comprimento, profundidade, angulo e espacamento randomizados. O
// formato .vtexture e texto puro com pares "chave: valor variacao%".
//
// A aleatoriedade e o random.Random(seed) do Python, reproduzido bit a bit (PyRandom), para
// a mesma semente dar os mesmos tracos nos dois programas.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

struct TextureSettings {
    static constexpr ToolpathType kKind = ToolpathType::Texture;

    std::string name = "Textura 1";
    double tool_radius = 3.0;
    double length = 20.0;
    double length_var = 66.0;
    double depth = 1.2;
    double depth_var = 57.0;
    double stepover = 3.0;
    double stepover_var = 51.0;
    double overlap = 0.3;
    double overlap_var = 62.0;
    double angle = 0.0;
    double angle_var = 0.0;
    std::int64_t seed = 42;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, TextureSettings>
{
    f("name", s.name);
    f("tool_radius", s.tool_radius);
    f("length", s.length);
    f("length_var", s.length_var);
    f("depth", s.depth);
    f("depth_var", s.depth_var);
    f("stepover", s.stepover);
    f("stepover_var", s.stepover_var);
    f("overlap", s.overlap);
    f("overlap_var", s.overlap_var);
    f("angle", s.angle);
    f("angle_var", s.angle_var);
    f("seed", s.seed);
}

// Le/grava o formato .vtexture. load lanca ValueError se um numero for invalido.
TextureSettings load_vtexture(const std::filesystem::path& path);
void save_vtexture(const std::filesystem::path& path, const TextureSettings& s);

std::vector<Toolpath> generate_texture(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                       const cad::MaterialSetup& material, const TextureSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const TextureSettings& s) {
    return generate_texture(entities, tool, material, s);
}

}  // namespace cnc::cam
