// Camadas e configuracao do material (Definicoes do Trabalho).
// Porte do inicio de src/cad/document.py; separado do Document para que os
// geradores de percurso dependam so do material, sem puxar o documento inteiro.
#pragma once

#include <optional>
#include <string>

#include "cad/entities.hpp"
#include "utils/json_util.hpp"

namespace cnc::cad {

struct Layer {
    std::string name = "Camada 1";
    Rgb color = kDefaultColor;
    bool visible = true;
    bool locked = false;
};

// Configuracao do material (Definicoes do Trabalho).
struct MaterialSetup {
    double width = 300.0;      // mm, eixo X
    double height = 200.0;     // mm, eixo Y
    double thickness = 18.0;   // mm, eixo Z
    std::string material_profile = "mdf";  // perfil de corte, independente da aparencia
    std::string zero_x = "left";    // left | center | right
    std::string zero_y = "bottom";  // bottom | center | top
    std::string zero_z = "top";     // top | bottom
    std::string units = "mm";
    bool use_origin_offset = false;
    double origin_offset_x = 0.0;  // mm, relativo ao ponto de origem escolhido
    double origin_offset_y = 0.0;
    bool scale_design = false;
    std::string model_resolution = "normal";
    std::string appearance = "canadian-maple";
    std::string solid_color = "#d9c29a";

    // Margem interna desenhada na chapa, em mm. 0 = sem margem. E so uma referencia
    // visual (e pontos de snap): nao recorta nem bloqueia nada.
    double margin = 0.0;

    // Alturas de seguranca e posicao de partida (usadas pelos percursos).
    double clearance_z = 5.0;  // altura de deslocamento rapido acima do material
    double plunge_gap = 2.0;   // altura onde o rapido termina e o mergulho comeca
    double home_x = 0.0;
    double home_y = 0.0;

    // Milimetros por unidade exibida; a geometria permanece em mm.
    double unit_factor() const { return units == "in" ? 25.4 : 1.0; }
    Pt origin_xy() const;
    // (x0, y0, x1, y1) da margem interna, ou nada se nao couber. Uma margem que engole a
    // chapa inteira nao e uma margem; melhor nao desenhar nada do que uma linha invertida.
    std::optional<BBox> margin_rect() const;
    double surface_z() const { return zero_z == "top" ? 0.0 : thickness; }
    int curve_segments() const;

    Json to_json() const;
    // Campos ausentes ficam no padrao; chaves desconhecidas sao ignoradas.
    static MaterialSetup from_json(const Json& j, const std::string& fallback_units = "mm");
};

// Chapas comerciais e padroes de mercado para cada material (medidas em mm).
struct SheetPreset {
    std::string name;             // Ex: "MDF", "PVC", "Acrílico", "ACM"
    std::string label;            // Ex: "MDF (1850 × 2750 mm)"
    double width = 1220.0;        // Largura (X), mm
    double height = 2440.0;       // Comprimento (Y), mm
    double default_thickness = 15.0; // Espessura típica, mm
    double tool_diameter = 6.0;   // Diâmetro de fresa sugerido, mm
    std::string note;
};

const std::vector<SheetPreset>& standard_sheet_presets();
const SheetPreset* find_sheet_preset(const std::string& material_name);

}  // namespace cnc::cad
