// Otimizar a disposicao dos objetos na chapa (porte de src/cad/layout.py).
//
// O otimizador avancado usa retangulos livres maximos, varias ordens e heuristicas de encaixe
// para aproveitar melhor a chapa. Respeita a margem interna e a folga entre pecas.
//
// Limite assumido, de proposito: o encaixe e pela caixa envolvente, nao pelo contorno real. Duas
// pecas em L nao se aninham uma na outra. Isso desperdica chapa num caso que o Aspire resolve, mas
// nunca sobrepoe peca nenhuma - e peca sobreposta e chapa perdida de verdade.
#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "cad/material.hpp"

namespace cnc::cad {

// Folga entre pecas, em mm. Precisa caber a fresa mais a sobra do corte; 6 mm e a fresa padrao da casa.
inline constexpr double kGapPadrao = 8.0;

struct Resultado {
    int movidos = 0;
    int nao_couberam = 0;
    int faixas = 0;
    std::vector<std::string> nao_couberam_ids;

    bool ok() const { return nao_couberam == 0; }
};

// Onde e permitido pousar peca: a margem, se houver; senao a chapa. (x0, y0, x1, y1)
BBox area_util(const MaterialSetup& material);

struct LayoutOptions {
    double tool_diameter = 6.0;
    double clearance = 6.0;
    double margin = 10.0;
    bool rotate = true;
    double rotation_step = 45.0;
    bool mirror = false;
    std::string origin = "bottom-left";
    std::string direction = "x";
    std::optional<BBox> area;

    double gap() const { return std::max(tool_diameter, clearance); }
};

struct Placement {
    std::vector<EntityPtr> entities;
    double anchor_x = 0.0;
    double anchor_y = 0.0;
    double angle = 0.0;
    bool mirrored = false;
    double dx = 0.0;
    double dy = 0.0;

    void apply(Entity& entity) const;
};

// Planeja o encaixe retangular sem alterar o documento. Usa retangulos livres maximos e tenta
// varias ordens de pecas: uma peca nao e descartada so porque nao coube na faixa atual.
std::pair<std::vector<Placement>, Resultado> plan_layout_advanced(const std::vector<EntityPtr>& entities,
                                                                   const MaterialSetup& material,
                                                                   const LayoutOptions& options);

// Planeja o posicionamento das pecas rejeitadas em colunas fora da chapa: id -> (dx, dy).
std::map<std::string, std::pair<double, double>> plan_unfit_outside(const std::vector<EntityPtr>& entities,
                                                                    const std::set<std::string>& unfit_ids,
                                                                    const MaterialSetup& material,
                                                                    double gap = kGapPadrao);

// Calcula o deslocamento de cada peca, sem mexer em nada (faixas simples). Separar o calculo da
// aplicacao deixa isto testavel sem janela e permite desfazer num passo so.
std::pair<std::vector<std::tuple<EntityPtr, double, double>>, Resultado> plan_layout(
    const std::vector<EntityPtr>& entidades, const MaterialSetup& material, double gap = kGapPadrao);

}  // namespace cnc::cad
