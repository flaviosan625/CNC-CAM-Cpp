// Selecoes em massa: abertos, fechados, duplicados, por camada
// (porte de src/cad/selection.py).
//
// Antes de mandar cortar, o operador precisa de respostas rapidas a tres perguntas que
// custam chapa: o que esta aberto (nao vira perfil fechado), o que esta duplicado (a fresa
// passa duas vezes no mesmo lugar) e o que esta nesta camada. Sao funcoes puras sobre o
// documento, sem interface.
#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "cam/model.hpp"

namespace cnc::cad {

class Document;

// Quanto dois pontos podem diferir e ainda contarem como o mesmo ponto. 0,01 mm esta
// abaixo da precisao de qualquer fresa e acima do ruido de arredondamento de PDF e DXF.
inline constexpr double kDupTol = 0.01;

// Guia e cota nao sao vetores: nao se cortam, nao entram nas selecoes. Guia e a linha que
// sai da regua; cota e a medida escrita ao lado da peca. O filtro e por tipo e esta num
// lugar so - aqui.
bool is_vector(const Entity& e);
// Dos itens dados, os que podem virar percurso.
std::vector<EntityPtr> machinable(const std::vector<EntityPtr>& items);
// Itens aceitos por uma operacao CAM especifica (furacao aceita tambem os pontos de furo).
std::vector<EntityPtr> machinable_for(const std::vector<EntityPtr>& items, cam::ToolpathType kind);
std::vector<EntityPtr> not_machinable(const std::vector<EntityPtr>& items);
// Frase que nomeia o que foi descartado, para a barra de status.
std::string why_not_machinable(const std::vector<EntityPtr>& items);

std::vector<EntityPtr> selectable(const Document& document);
std::vector<EntityPtr> open_vectors(const Document& document);
std::vector<EntityPtr> closed_vectors(const Document& document);
std::vector<EntityPtr> vectors_on_layer(const Document& document, const std::string& layer);
// Os vetores que sao copia de outro NO MESMO LUGAR. Devolve as copias, nunca o primeiro de
// cada grupo: da para apagar o resultado inteiro e sobrar exatamente um de cada. Pecas
// iguais em posicoes diferentes nao sao duplicatas - aquilo e producao.
std::vector<EntityPtr> duplicate_vectors(const Document& document, double tol = kDupTol);
// Seleciona a lista dada; sem `adicionar`, limpa o que estava antes.
int apply_selection(Document& document, const std::vector<EntityPtr>& items, bool adicionar = false);

}  // namespace cnc::cad
