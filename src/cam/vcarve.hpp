// Entalhe em V com profundidade variavel e desbaste de areas largas
// (porte de src/cam/vcarve.py).
//
// O percurso de acabamento segue uma aproximacao do eixo medial da regiao. Em cada ponto, a
// distancia ate a parede define o Z da fresa. Isso preserva cantos agudos e nao depende de uma
// pilha de offsets com profundidade fixa.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/nesting.hpp"
#include "cam/pocket.hpp"
#include "cam/profile.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

enum class VCarveMode { Automatic, Manual };
enum class VCarveMaterial { Mdf, Mdp, Plywood, Softwood, Hardwood, ExpandedPvc, Acrylic, Other };

const char* vcarve_material_label(VCarveMaterial m);

}  // namespace cnc::cam

namespace cnc {
CNC_ENUM(cam::VCarveMode, {cam::VCarveMode::Automatic, "automatic"}, {cam::VCarveMode::Manual, "manual"});
CNC_ENUM(cam::VCarveMaterial, {cam::VCarveMaterial::Mdf, "mdf"}, {cam::VCarveMaterial::Mdp, "mdp"},
         {cam::VCarveMaterial::Plywood, "plywood"}, {cam::VCarveMaterial::Softwood, "softwood"},
         {cam::VCarveMaterial::Hardwood, "hardwood"}, {cam::VCarveMaterial::ExpandedPvc, "expanded_pvc"},
         {cam::VCarveMaterial::Acrylic, "acrylic"}, {cam::VCarveMaterial::Other, "other"});
}  // namespace cnc

namespace cnc::cam {

struct MaterialAdvice {
    double max_flat_depth;
    double feed_factor;
    double plunge_factor;
    double stepover_factor;
    const char* note;
};
const MaterialAdvice& material_advice(VCarveMaterial m);

struct VCarveSettings {
    static constexpr ToolpathType kKind = ToolpathType::VCarve;

    std::string name = "Entalhe V 1";
    VCarveMode mode = VCarveMode::Automatic;
    VCarveMaterial material_profile = VCarveMaterial::Mdf;

    double start_depth = 0.0;
    double flat_depth = 0.0;  // 0 = profundidade definida pela geometria
    double max_depth = 0.0;   // 0 = limite fisico da ferramenta/material
    bool maintain_exact_step = false;

    std::vector<Tool> clearance_tools;
    FillType clearance_fill = FillType::Offset;
    double raster_angle = 0.0;
    CutDirection direction = CutDirection::Conventional;
    double clearance_stepover = 0.0;
    double finish_stepover = 0.35;
    double clearance_allowance = 0.15;
    bool skip_flat_regions = false;

    RampType ramp = RampType::Smooth;
    double ramp_distance = 10.0;
    MachineOrder order = MachineOrder::Shortest;
    double tolerance = kDefaultTolerance;
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, VCarveSettings>
{
    f("name", s.name);
    f("mode", s.mode);
    f("material_profile", s.material_profile);
    f("start_depth", s.start_depth);
    f("flat_depth", s.flat_depth);
    f("max_depth", s.max_depth);
    f("maintain_exact_step", s.maintain_exact_step);
    f("clearance_tools", s.clearance_tools);
    f("clearance_fill", s.clearance_fill);
    f("raster_angle", s.raster_angle);
    f("direction", s.direction);
    f("clearance_stepover", s.clearance_stepover);
    f("finish_stepover", s.finish_stepover);
    f("clearance_allowance", s.clearance_allowance);
    f("skip_flat_regions", s.skip_flat_regions);
    f("ramp", s.ramp);
    f("ramp_distance", s.ramp_distance);
    f("order", s.order);
    f("tolerance", s.tolerance);
}

// Ponto de partida conservador, sempre editavel pelo operador.
VCarveSettings automatic_settings(const Tool& tool, VCarveMaterial material_profile,
                                  double material_thickness);
// Copia da ferramenta com avancos conservadores do material.
Tool advised_tool(const Tool& tool, VCarveMaterial material_profile);

struct VCarveRecommendation {
    std::shared_ptr<Tool> tool;  // nulo se nao houver fresa/vetor adequados
    VCarveSettings settings;
    std::string report;
};
// Escolhe fresa, limites e desbaste a partir do desenho e do material. Devolve objetos do
// banco de ferramentas, mas nao os altera.
VCarveRecommendation recommend_vcarve(const std::vector<cad::EntityPtr>& entities,
                                      const std::vector<std::shared_ptr<Tool>>& tools,
                                      const cad::MaterialSetup& material,
                                      VCarveMaterial material_profile = VCarveMaterial::Mdf);

std::vector<Toolpath> generate_vcarve(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const VCarveSettings& s);

inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const VCarveSettings& s) {
    return generate_vcarve(entities, tool, material, s);
}

// Aproxima o eixo medial por Voronoi das bordas e filtra ramos falsos.
std::vector<Points> medial_paths(const geo::Geom& poly, double sample_step,
                                 double tolerance = kDefaultTolerance);

}  // namespace cnc::cam
