// Percurso de perfil: contorno externo, interno ou sobre a linha
// (porte de src/cam/profile.py).
//
// Reproduz o comportamento do Profile Toolpath do Vectric - incluindo detectar sozinho o
// que e furo e corta-lo por dentro - e acrescenta duas coisas que a casa aprendeu na pratica:
//  * Furos e contornos saem em percursos separados por padrao. A aba "Ordem" do Aspire so
//    otimiza deslocamento; a unica garantia de cortar o interno antes de a peca soltar e
//    emitir dois percursos, o interno primeiro.
//  * Avisos de ferramenta grande demais. Se o offset consome um furo inteiro, o percurso
//    diz qual, em vez de simplesmente nao cortar nada ali.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"
#include "cam/model.hpp"
#include "cam/nesting.hpp"
#include "cam/settings_io.hpp"
#include "cam/tools.hpp"

namespace cnc::cam {

// Ponte posta a mao: (id do vetor, x, y).
struct ManualTab {
    std::string entity_id;
    double x = 0.0;
    double y = 0.0;

    Json to_json() const { return Json::array({entity_id, x, y}); }
    static ManualTab from_json(const Json& j) {
        return {j.at(0).get<std::string>(), j.at(1).get<double>(), j.at(2).get<double>()};
    }
    bool operator==(const ManualTab&) const = default;
};

// Parametros do percurso de perfil (espelham o formulario do Vectric).
struct ProfileSettings {
    static constexpr ToolpathType kKind = ToolpathType::Profile;

    std::string name = "Perfil 1";

    // Profundidades
    double start_depth = 0.0;
    double cut_depth = 18.0;
    bool maintain_exact_step = false;

    // Posicao da fresa
    ProfileSide side = ProfileSide::Outside;
    OpenSide open_side = OpenSide::On;
    CutDirection direction = CutDirection::Conventional;
    double allowance = 0.0;

    // Passada final separada
    bool separate_last_pass = false;
    double last_pass_allowance = 0.2;
    bool reverse_last_pass = false;

    // Rampa de entrada
    RampType ramp = RampType::Smooth;
    double ramp_distance = 10.0;
    double ramp_angle = 0.0;  // graus; 0 = usar distancia
    bool ramp_on_lead = false;

    // Entradas e saidas
    LeadType lead = LeadType::None;
    double lead_length = 5.0;
    double lead_radius = 3.0;
    double lead_angle = 45.0;
    bool lead_out = true;
    double overcut = 0.0;

    // Pontes de fixacao
    bool tabs = false;
    double tab_length = 12.0;
    double tab_thickness = 3.0;
    int tab_count = 4;
    double tab_spacing = 0.0;  // > 0 usa espacamento em vez de contagem
    int tab_min_count = 2;     // piso quando o espacamento da pouca ponte
    int tab_max_count = 24;    // teto quando o espacamento da ponte demais
    bool tab_first_at_start = false;
    bool tabs_3d = true;
    std::vector<ManualTab> manual_tabs;
    // Ativado ao arrastar uma ponte automatica. Nesse modo as posicoes manuais substituem
    // a distribuicao automatica, permitindo mover todas.
    bool tab_manual_only = false;

    // Ordem
    MachineOrder order = MachineOrder::Shortest;
    // Varias estrategias marcadas: calcula todas e fica com a que anda menos em vazio,
    // como o Aspire. Guardado como texto para o projeto do disco nao depender de enum.
    // Vazio = usar so `order`.
    std::vector<std::string> orders;
    bool use_start_points = false;  // respeitar o no inicial de cada vetor
    bool separate_interior = true;

    double tolerance = kDefaultTolerance;

    double total_depth() const { return std::max(0.0, cut_depth); }
};

template <class S, class F>
void visit_fields(S& s, F&& f)
    requires std::is_same_v<std::remove_const_t<S>, ProfileSettings>
{
    f("name", s.name);
    f("start_depth", s.start_depth);
    f("cut_depth", s.cut_depth);
    f("maintain_exact_step", s.maintain_exact_step);
    f("side", s.side);
    f("open_side", s.open_side);
    f("direction", s.direction);
    f("allowance", s.allowance);
    f("separate_last_pass", s.separate_last_pass);
    f("last_pass_allowance", s.last_pass_allowance);
    f("reverse_last_pass", s.reverse_last_pass);
    f("ramp", s.ramp);
    f("ramp_distance", s.ramp_distance);
    f("ramp_angle", s.ramp_angle);
    f("ramp_on_lead", s.ramp_on_lead);
    f("lead", s.lead);
    f("lead_length", s.lead_length);
    f("lead_radius", s.lead_radius);
    f("lead_angle", s.lead_angle);
    f("lead_out", s.lead_out);
    f("overcut", s.overcut);
    f("tabs", s.tabs);
    f("tab_length", s.tab_length);
    f("tab_thickness", s.tab_thickness);
    f("tab_count", s.tab_count);
    f("tab_spacing", s.tab_spacing);
    f("tab_min_count", s.tab_min_count);
    f("tab_max_count", s.tab_max_count);
    f("tab_first_at_start", s.tab_first_at_start);
    f("tabs_3d", s.tabs_3d);
    f("manual_tabs", s.manual_tabs);
    f("tab_manual_only", s.tab_manual_only);
    f("order", s.order);
    f("orders", s.orders);
    f("use_start_points", s.use_start_points);
    f("separate_interior", s.separate_interior);
    f("tolerance", s.tolerance);
}

// ======================================================================
// Emissor de movimentos
// ======================================================================

// Monta a lista de movimentos cuidando de alturas seguras.
class Emitter {
public:
    Emitter(double clearance_z, double plunge_z_gap)
        : clearance_z_(clearance_z), plunge_gap_(plunge_z_gap), z_(clearance_z) {}

    std::vector<Move> moves;

    void retract();
    void rapid_to(double x, double y);
    // Desce rapido ate a folga de mergulho e so entao entra no material.
    void plunge_to(double z);
    void cut_to(double x, double y, double z, bool plunging = false);

    Pt position() const { return {x_, y_}; }
    double z() const { return z_; }
    // Fixa o Z atual sem emitir movimento (o furo peck registra o recuo por conta propria).
    void force_z(double z) { z_ = z; }

private:
    double clearance_z_;
    double plunge_gap_;
    double x_ = 0.0;
    double y_ = 0.0;
    double z_;
};

// ======================================================================
// Pontes de fixacao (tabs)
// ======================================================================

// As estrategias de ordem a testar: as marcadas, ou a unica escolhida.
std::vector<MachineOrder> machine_orders(const ProfileSettings& s);

// Quantas pontes cabem neste contorno. Dois modos, como no Aspire: quantidade fixa, ou
// uma ponte a cada tantos milimetros - nunca menos que o minimo nem mais que o maximo.
int tab_count_for(double total_length, const ProfileSettings& s);

using Span = std::pair<double, double>;
// Trechos (distancia acumulada) ocupados por pontes, ja unidos.
std::vector<Span> tab_spans(double total_length, const ProfileSettings& s,
                            const Points* ring = nullptr, const std::string* entity_id = nullptr);

// Posicoes visuais das pontes no desenho original.
struct TabPreview {
    std::string entity_id;
    double x = 0.0;
    double y = 0.0;
    bool manual = false;  // veio da lista manual?
};
std::vector<TabPreview> profile_tab_preview(const std::vector<cad::EntityPtr>& entities,
                                            const ProfileSettings& s, const Tool* tool = nullptr);

// ======================================================================
// Geracao
// ======================================================================

// Gera o(s) percurso(s) de perfil para as entidades dadas.
std::vector<Toolpath> generate_profile(const std::vector<cad::EntityPtr>& entities,
                                       const Tool& tool, const cad::MaterialSetup& material,
                                       const ProfileSettings& s);

// Sobrecarga usada pelo ToolpathJob: o tipo dos parametros escolhe o gerador.
inline std::vector<Toolpath> generate(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                      const cad::MaterialSetup& material, const ProfileSettings& s) {
    return generate_profile(entities, tool, material, s);
}

}  // namespace cnc::cam
