// Biblioteca de ferramentas de corte (porte de src/cam/tools.py).
//
// Modelo equivalente ao "Tool Database" do Vectric: cada ferramenta guarda a
// geometria (tipo, diametro, angulo) e os dados de corte (passo Z, passo lateral,
// avancos, rotacao).
//
// Regra de ouro herdada de um erro real em producao: a unidade e sempre milimetro
// aqui dentro. No Aspire o objeto Tool nasce em polegada e um diametro de 4 virava
// 4" (raio de 50,8 mm sobre o vetor). Nao existe "unidade da ferramenta": a conversao
// acontece so na interface.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "utils/enum_names.hpp"
#include "utils/json_util.hpp"

namespace cnc::cam {

enum class ToolType {
    EndMill,      // Fresa de topo reto
    BallNose,     // Fresa esferica
    VBit,         // Fresa em V
    Engraving,    // Ponta de gravacao (V com ponta plana)
    Radiused,     // Fresa de topo com raio
    TaperedBall,  // Esferica conica
    Drill,        // Broca
};

const char* tool_type_label(ToolType t);
// Tipos que removem material lateralmente (podem fazer perfil/bolso).
bool is_milling_type(ToolType t);

}  // namespace cnc::cam

namespace cnc {
CNC_ENUM(cam::ToolType, {cam::ToolType::EndMill, "end_mill"}, {cam::ToolType::BallNose, "ball_nose"},
         {cam::ToolType::VBit, "v_bit"}, {cam::ToolType::Engraving, "engraving"},
         {cam::ToolType::Radiused, "radiused"}, {cam::ToolType::TaperedBall, "tapered_ball"},
         {cam::ToolType::Drill, "drill"});
}  // namespace cnc

namespace cnc::cam {

// Uma ferramenta de corte. Todas as medidas em mm, avancos em mm/min.
struct Tool {
    std::string name = "Fresa de topo 6 mm";
    ToolType tool_type = ToolType::EndMill;
    double diameter = 6.0;
    int num_flutes = 2;

    // Geometria especifica
    double v_angle = 90.0;       // V-bit / gravacao: angulo total (graus)
    double tip_diameter = 0.0;   // gravacao / conica: diametro da ponta
    double corner_radius = 0.0;  // fresa de topo com raio

    // Dados de corte
    double pass_depth = 3.0;           // profundidade maxima por passada Z
    double stepover = 2.4;             // passo lateral no desbaste de area
    double clearance_stepover = 0.0;   // V-bit: passo das passadas de desbaste
    double final_stepover = 0.0;       // V-bit: passo da passada final
    int spindle_speed = 18000;         // RPM
    double feed_rate = 1000.0;         // avanco XY (mm/min)
    double plunge_rate = 300.0;        // avanco Z e de rampa (mm/min)

    int tool_number = 1;
    std::string notes;

    double radius() const { return diameter / 2.0; }
    double stepover_percent() const { return diameter != 0.0 ? 100.0 * stepover / diameter : 0.0; }
    // Avanco por dente (mm). Comparar com o valor do fabricante da fresa.
    double chip_load() const;
    // Passo lateral acima de 50% deixa material nos cantos (regra Vectric).
    bool needs_tails() const { return stepover > diameter * 0.5; }
    // Largura de corte de uma fresa em V a uma dada profundidade.
    double flat_width_at(double depth) const;
    // Profundidade em que a fresa em V atinge o diametro nominal.
    double max_v_depth() const;
    std::string describe() const;

    Json to_json() const;
    static Tool from_json(const Json& j);

    bool operator==(const Tool&) const = default;
};

// As tres fresas da casa e a biblioteca generica de router.
std::vector<Tool> default_tools();

// Banco de ferramentas gravado em JSON no perfil do usuario.
class ToolDatabase {
public:
    using ToolPtr = std::shared_ptr<Tool>;

    std::vector<ToolPtr> tools;
    std::filesystem::path path;

    // Banco novo com as ferramentas padrao, apontando para o arquivo do usuario.
    ToolDatabase();
    explicit ToolDatabase(std::filesystem::path file);

    // Le o arquivo; se faltar ou estiver quebrado, devolve o banco padrao.
    static ToolDatabase load(const std::filesystem::path& file = {});
    void save() const;

    ToolPtr find(const std::string& name) const;
    // Adiciona garantindo nome unico: o banco do Aspire permite nomes repetidos no
    // mesmo grupo e pedir pela ferramenta vira loteria.
    ToolPtr add(Tool tool);
    void remove(const ToolPtr& tool);
    std::vector<ToolPtr> by_type(ToolType kind) const;
};

// Caminho do arquivo do banco (funcao, nao constante: a pasta de configuracao pode
// ser trocada em teste).
std::filesystem::path db_path();

}  // namespace cnc::cam
