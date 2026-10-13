// Entidades <-> JSON do arquivo .cnc (porte de Document._to_dict / Document._BUILDERS).
//
// O formato e o do app em Python: um arquivo gravado por um abre no outro. A ordem das
// chaves tambem e a mesma (type, campos da geometria, id, layer, color, group).
#pragma once

#include <string>

#include "cad/entities.hpp"
#include "utils/json_util.hpp"

namespace cnc::cad {

Json dimension_style_to_json(const DimensionStyle& style);
// Valor que nao e objeto -> estilo padrao; campos ausentes -> padrao (DimensionStyle.from_dict).
DimensionStyle dimension_style_from_json(const Json& data);

Json entity_to_json(const Entity& e);

// Reconstroi a entidade, ou devolve nulo se o tipo for desconhecido ou faltar campo
// obrigatorio (o Python ignora a entidade em vez de derrubar a abertura do projeto).
// `default_layer` e usado quando o JSON nao traz "layer".
EntityPtr entity_from_json(const Json& d, const std::string& default_layer = "Camada 1");

}  // namespace cnc::cad
