// Modelo interno de percursos: movimentos, passadas e utilidades de geometria.
//
// Convencao de Z, igual a do Aspire:
//  * A superficie do material esta em material.surface_z.
//  * Profundidade e sempre positiva e medida para baixo a partir dela.
//  * z = surface_z - profundidade.
//  * Deslocamento rapido acontece em surface_z + clearance_z.
// Todas as coordenadas internas sao milimetros absolutos de maquina.
//
// Porte de src/cam/model.py.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "geo/geom.hpp"
#include "geo/types.hpp"
#include "utils/enum_names.hpp"

namespace cnc::cam {

using Point = Pt;

// Fator de escala para o Clipper, que trabalha com inteiros.
// 1e5 da resolucao de 0,00001 mm e cabe folgado em int64.
inline constexpr double kClipperScale = 1e5;

// ======================================================================
// Movimentos
// ======================================================================
enum class MoveKind {
    Rapid,   // G0 - deslocamento sem corte
    Cut,     // G1 no avanco de corte
    Plunge,  // G1 no avanco de mergulho (descida e rampa)
    Dwell,   // G4 - pausa no fundo do furo
};

struct Move {
    MoveKind kind = MoveKind::Rapid;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double seconds = 0.0;  // so para Dwell

    bool cutting() const { return kind == MoveKind::Cut || kind == MoveKind::Plunge; }
};

enum class ToolpathType {
    Profile, Pocket, Drill, VCarve, Fluting, Texture, Keyhole, Inlay, Dragknife,
};
const char* toolpath_label(ToolpathType t);

enum class ProfileSide { Outside, Inside, On };
enum class OpenSide { Left, Right, On };
enum class CutDirection { Conventional, Climb };
enum class RampType { None, Smooth, Zigzag, Spiral };
enum class LeadType { None, Linear, Arc };
enum class FillType { Offset, Raster };
enum class ProfilePass { First, Last, None };
enum class MachineOrder { Selection, LeftRight, BottomTop, Grid, Shortest };
const char* order_label(MachineOrder o);

}  // namespace cnc::cam

namespace cnc {
CNC_ENUM(cam::MoveKind, {cam::MoveKind::Rapid, "rapid"}, {cam::MoveKind::Cut, "cut"},
         {cam::MoveKind::Plunge, "plunge"}, {cam::MoveKind::Dwell, "dwell"});
CNC_ENUM(cam::ToolpathType, {cam::ToolpathType::Profile, "profile"},
         {cam::ToolpathType::Pocket, "pocket"}, {cam::ToolpathType::Drill, "drill"},
         {cam::ToolpathType::VCarve, "vcarve"}, {cam::ToolpathType::Fluting, "fluting"},
         {cam::ToolpathType::Texture, "texture"}, {cam::ToolpathType::Keyhole, "keyhole"},
         {cam::ToolpathType::Inlay, "inlay"}, {cam::ToolpathType::Dragknife, "dragknife"});
CNC_ENUM(cam::ProfileSide, {cam::ProfileSide::Outside, "outside"},
         {cam::ProfileSide::Inside, "inside"}, {cam::ProfileSide::On, "on"});
CNC_ENUM(cam::OpenSide, {cam::OpenSide::Left, "left"}, {cam::OpenSide::Right, "right"},
         {cam::OpenSide::On, "on"});
CNC_ENUM(cam::CutDirection, {cam::CutDirection::Conventional, "conventional"},
         {cam::CutDirection::Climb, "climb"});
CNC_ENUM(cam::RampType, {cam::RampType::None, "none"}, {cam::RampType::Smooth, "smooth"},
         {cam::RampType::Zigzag, "zigzag"}, {cam::RampType::Spiral, "spiral"});
CNC_ENUM(cam::LeadType, {cam::LeadType::None, "none"}, {cam::LeadType::Linear, "linear"},
         {cam::LeadType::Arc, "arc"});
CNC_ENUM(cam::FillType, {cam::FillType::Offset, "offset"}, {cam::FillType::Raster, "raster"});
CNC_ENUM(cam::ProfilePass, {cam::ProfilePass::First, "first"}, {cam::ProfilePass::Last, "last"},
         {cam::ProfilePass::None, "none"});
CNC_ENUM(cam::MachineOrder, {cam::MachineOrder::Selection, "selection"},
         {cam::MachineOrder::LeftRight, "left_right"}, {cam::MachineOrder::BottomTop, "bottom_top"},
         {cam::MachineOrder::Grid, "grid"}, {cam::MachineOrder::Shortest, "shortest"});
}  // namespace cnc

namespace cnc::cam {

// ======================================================================
// Percurso calculado
// ======================================================================

// Um percurso calculado, pronto para desenhar, simular e pos-processar.
struct Toolpath {
    std::string name;
    ToolpathType kind = ToolpathType::Profile;
    std::string tool_name;
    int tool_number = 1;
    int spindle_speed = 18000;
    double feed_rate = 1000.0;
    double plunge_rate = 300.0;
    std::vector<Move> moves;
    bool visible = true;
    std::vector<std::string> notes;

    // -- estatisticas --
    std::optional<BBox> bounds() const;
    std::pair<double, double> depth_range() const;
    // (comprimento cortando, comprimento em rapido) em mm
    std::pair<double, double> lengths() const;
    // Tempo estimado. Conta mergulho no avanco de mergulho, que e o que mais
    // engana numa estimativa feita so com o avanco de corte.
    double estimated_seconds(double rapid_rate = 8000.0) const;
    // Duracao ideal de um movimento, sem aceleracao da maquina.
    double move_seconds(const Move& start, const Move& end, double rapid_rate = 8000.0) const;
    std::string summary() const;
};

// ======================================================================
// Passadas em Z
// ======================================================================

// Profundidades acumuladas de cada passada (positivas, crescentes).
// Por padrao o passo pode variar ate 15% para baixo se isso economizar uma passada
// inteira (comportamento documentado do Vectric). maintain_exact desliga a variacao.
std::vector<double> compute_passes(double total_depth, double pass_depth,
                                   bool maintain_exact = false, double variation = 0.15);

// ======================================================================
// Offset de contornos (Clipper)
// ======================================================================

// Offset de um poligono (com ilhas) por `delta` mm. Delta positivo cresce o contorno
// externo e encolhe os furos na mesma operacao, porque o Clipper respeita a orientacao
// dos aneis.
std::vector<Points> offset_polygon(const geo::Geom& polygon, double delta,
                                   double tolerance = 0.01);

// Offset de um vetor aberto para a esquerda, direita ou sobre a linha.
std::vector<Points> offset_open_path(const geo::Geom& line, double delta, OpenSide side,
                                     double tolerance = 0.01);

// ======================================================================
// Utilidades de polilinha
// ======================================================================
double signed_area(const Points& pts);
bool is_ccw(const Points& pts);
Points orient_ring(const Points& pts, bool want_ccw);
// Sentido de percurso para o corte pedido: concordante (climb) e horario ao contornar
// material por fora e anti-horario ao contornar um vazio por dentro; convencional e o
// oposto - e e o que a casa usa.
bool travel_ccw(bool material_inside, CutDirection direction);
// Garante o ponto final igual ao inicial, para percorrer o anel inteiro.
Points close_ring(Points pts);
double path_length(const Points& pts);
// Gira um anel fechado para comecar no ponto mais proximo de `start`.
Points rotate_to_start(const Points& ring, Pt start);
// Insere pontos nas distancias pedidas ao longo da polilinha.
Points resample(const Points& pts, const std::vector<double>& distances);
// Ponto a uma distancia ao longo da polilinha (extrapola no fim).
Pt point_at(const Points& pts, double distance);
// Trecho da polilinha entre duas distancias acumuladas.
Points slice_path(const Points& pts, double start, double end);
Pt tangent_at_start(const Points& pts);

// Indices dos lacos na ordem de usinagem pedida. So reordena deslocamento: a ordem
// furo-antes-de-peca e garantida fora daqui, agrupando por nivel de aninhamento.
std::vector<int> order_loops(const std::vector<Points>& loops, MachineOrder order,
                             double grid_band = 50.0);
// Quanto a fresa anda em vazio nesta ordem, a partir da origem.
double travel_length(const std::vector<Points>& loops, const std::vector<int>& sequence);
// Testa cada ordenacao pedida e devolve a que anda menos em vazio (e qual foi).
std::pair<std::vector<int>, MachineOrder> best_order(const std::vector<Points>& loops,
                                                     const std::vector<MachineOrder>& orders,
                                                     double grid_band = 50.0);

// Tempo no formato do app: "45 s", "12 min", "1 h 05 min".
std::string fmt_time(double minutes);

}  // namespace cnc::cam
