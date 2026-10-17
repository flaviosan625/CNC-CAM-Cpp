#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

#include "cad/mesh.hpp"

namespace cnc::cad::relief {

enum class CombineMode {
    Add,       // Adicionar (+): Z_final = Z_base + Z_comp
    Subtract,  // Subtrair (-): Z_final = Z_base - Z_comp (entalhe)
    MergeHigh, // Mesclar Alto (M): Z_final = max(Z_base, Z_comp)
    MergeLow,  // Mesclar Baixo: Z_final = min(Z_base, Z_comp)
    Multiply   // Multiplicar: modulação proporcional de alturas
};

enum class ReliefNature {
    Raised,    // Relevo elevado sobre a face
    Carved     // Entalhe na peça (rebaixado)
};

enum class SculptTool {
    Smooth,    // Suavizar ruído e arestas
    Smudge,    // Esfumar / arrastar argila digital
    Elevate,   // Elevar / depositar material
    Remove     // Remover / cavar material
};

// Grade local de altura (Local ROI Heightfield)
// Evita alocações globais gigantescas: armazena apenas a região retangular necessária.
struct LocalGrid {
    double x0 = 0.0;
    double y0 = 0.0;
    double width = 0.0;
    double height = 0.0;
    double step = 0.5; // mm por célula
    int cols = 0;
    int rows = 0;
    std::vector<float> z; // tamanho cols * rows

    bool empty() const { return cols <= 0 || rows <= 0 || z.empty(); }
    float get(int c, int r) const {
        if (c < 0 || c >= cols || r < 0 || r >= rows) return 0.0f;
        return z[static_cast<size_t>(r * cols + c)];
    }
    void set(int c, int r, float val) {
        if (c >= 0 && c < cols && r >= 0 && r < rows) {
            z[static_cast<size_t>(r * cols + c)] = val;
        }
    }
    float sample_bilinear(double x, double y) const;
};

// Componente independente de relevo (conforme especificação do Aspire)
struct ReliefComponent {
    std::string id;
    std::string name = "Relevo";
    bool visible = true;
    bool locked = false;

    // Dimensão e posicionamento físico na peça (em milímetros)
    double pos_x = 0.0;
    double pos_y = 0.0;
    double width = 100.0;
    double height = 100.0;

    // Controles de altura independentes
    double shape_height = 3.0; // Altura da forma (relevo da imagem)
    double base_height = 0.0;  // Altura da base (patamar / pedestal)

    // Parâmetros de calibração de imagem
    double smoothness = 0.0;   // 0.0 (nítido) a 1.0 (suave)
    double contrast = 1.0;     // 0.2 a 3.0
    bool invert = false;       // Inverter tons (preto vira pico)

    CombineMode combine_mode = CombineMode::Add;
    ReliefNature nature = ReliefNature::Raised;
    std::string vector_mask_id; // Vetor delimitador opcional do CAD

    // Armazenamento não-destrutivo dos dados originais da imagem
    bool is_16bit = false;
    int img_width = 0;
    int img_height = 0;
    std::vector<uint8_t> raw_8bit;      // Grayscale 0..255
    std::vector<uint16_t> raw_16bit;    // Grayscale de alta precisão 0..65535
    std::string original_filename;
    std::string original_encoded_base64; // Persistência do arquivo de imagem original

    // Métodos utilitários de amostragem
    bool has_image() const { return (img_width > 0 && img_height > 0) && (!raw_8bit.empty() || !raw_16bit.empty()); }
    float sample_normalized(double u, double v) const; // Retorna 0.0f .. 1.0f

    // Carregamento a partir de buffer (PNG, JPG, BMP, TIFF)
    bool load_from_memory(const uint8_t* data, size_t size, const std::string& filename, std::string* error = nullptr);
    bool load_from_file(const std::string& filepath, std::string* error = nullptr);

    // Serialização JSON
    nlohmann::json to_json() const;
    static ReliefComponent from_json(const nlohmann::json& j);
};

class ReliefEngine {
public:
    // Gera a grade de altura individual para um componente em sua área local
    static void generate_component_grid(
        const ReliefComponent& comp,
        double resolution_mm,
        LocalGrid& out_grid
    );

    // Compõe todos os componentes ativos respeitando a ordem, visibilidade e modos de combinação.
    // Usa uma região retangular restrita (bounding box) da peça ou dos componentes.
    static void compose(
        const std::vector<ReliefComponent>& components,
        double workpiece_w,
        double workpiece_h,
        double resolution_mm,
        LocalGrid& out_composite,
        double max_total_height = 200.0
    );

    // Converte a grade em malha 3D (Mesh) pronta para o Viewport3D, com iluminação direcional nos vértices
    static cad::Mesh generate_mesh(
        const LocalGrid& grid,
        double base_z = 0.0,
        double light_azimuth_deg = 45.0,
        double light_elevation_deg = 55.0
    );

    // Escultura interativa com pincel e falloff gaussiano
    static void apply_sculpt_stroke(
        LocalGrid& grid,
        SculptTool tool,
        double cx,
        double cy,
        double radius_mm,
        double strength,
        double max_total_height = 200.0
    );

    static const char* combine_mode_name(CombineMode m);
    static CombineMode parse_combine_mode(const std::string& s);
};

} // namespace cnc::cad::relief
