// Classificacao de aninhamento: o que e peca e o que e furo
// (porte de src/cam/nesting.py).
//
// Este e o problema que decide se a maquina corta a peca por fora (medida certa) ou
// por dentro (peca menor pelo diametro da fresa). O algoritmo e o mesmo ja validado
// cortando chapa de verdade, com as duas armadilhas que custaram caro corrigidas:
//
// 1. Comparar area e obrigatorio. So contornos de area MAIOR contam como "contem".
//    Sem isso a letra 'O' e o furo dela se contem mutuamente.
// 2. Caixa envolvente nao serve. O teste e ponto-dentro-do-contorno real. Num caso
//    real, 13 letras caiam dentro da CAIXA de um logo varrido sem estar dentro dele;
//    11 viraram "furo" e sairiam 4 mm menores.
//
// A caixa envolvente entra so como filtro rapido (indice espacial), nunca como resposta.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "geo/geom.hpp"

namespace cnc::cam {

// Erro de corda aceito ao transformar curvas em segmentos (mm). O Aspire trabalha com
// 0,01 mm - mesma ordem de grandeza.
inline constexpr double kDefaultTolerance = 0.01;

// Numero de segmentos para aproximar um circulo dentro da tolerancia. Melhor que um
// numero fixo: um circulo de 5 mm e um de 500 mm recebem o detalhe que cada um precisa.
int segments_for(double radius, double tolerance = kDefaultTolerance);
int entity_segments(const cad::Entity& e, double tolerance = kDefaultTolerance);

// Entidade -> geometria GEOS, discretizada pela tolerancia pedida. Nula se nao houver.
geo::Geom entity_geometry(const cad::Entity& e, double tolerance = kDefaultTolerance);

// Um contorno fechado com seu nivel de aninhamento.
struct Contour {
    cad::EntityPtr entity;
    geo::Geom polygon;
    int level = 0;

    // Nivel impar = furo (material ao redor), par = peca.
    bool is_hole() const { return level % 2 == 1; }
    double area() const { return polygon.area(); }
};
using ContourPtr = std::shared_ptr<Contour>;

// Uma peca com os furos que estao de fato dentro dela. E isto que vai para o offset: o
// contorno externo cresce e os furos encolhem numa operacao so.
struct Region {
    ContourPtr outer;
    std::vector<ContourPtr> holes;

    geo::Geom to_polygon() const;
};

struct OpenPath {
    cad::EntityPtr entity;
    geo::Geom line;
};

struct SplitResult {
    std::vector<ContourPtr> closed;
    std::vector<OpenPath> open_paths;
};

// Separa as entidades em contornos fechados e vetores abertos. Uma entidade pode render
// mais de um contorno (texto com varias letras): cada anel vira um contorno independente.
SplitResult split_entities(const std::vector<cad::EntityPtr>& entities,
                           double tolerance = kDefaultTolerance);

// Calcula o nivel de aninhamento de cada contorno (no proprio vetor). Nivel = quantos
// contornos de area MAIOR contem este. Impar = furo.
void classify(std::vector<ContourPtr>& contours);

// Agrupa cada peca com os furos imediatamente dentro dela. Um furo pertence a menor peca
// de nivel imediatamente abaixo que o contem.
std::vector<Region> build_regions(const std::vector<ContourPtr>& contours);

struct RegionsResult {
    std::vector<Region> regions;
    std::vector<OpenPath> open_paths;
};
// Atalho: entidades -> (regioes peca+furos, vetores abertos).
RegionsResult regions_from(const std::vector<cad::EntityPtr>& entities,
                           double tolerance = kDefaultTolerance);

// Linhas de relatorio do que foi para dentro e do que foi para fora.
std::vector<std::string> nesting_report(const std::vector<ContourPtr>& contours);

}  // namespace cnc::cam
