// Variaveis do documento - carimbos que o texto do desenho pode usar
// (porte de src/cad/doc_vars.py).
//
// Escreve-se |TRABALHO| ou |ESPESSURA| no texto e a variavel vira o valor. Diferenca para o
// Aspire: la o carimbo continua vivo e se atualiza sozinho. Aqui a troca acontece quando
// voce manda aplicar, e o que fica no texto e o valor - depois disso e texto comum. Em
// compensacao, o que vai para o G-code e sempre o que esta na tela.
#pragma once

#include <string>
#include <vector>

#include "cad/document.hpp"

namespace cnc::cad {

// As que o programa sabe responder sozinho.
OrderedStrMap builtin_variables(const Document& document);
// Embutidas + as do usuario. As do usuario ganham, para dar como sobrescrever um carimbo.
OrderedStrMap all_variables(const Document& document);
// Troca |NOME| pelo valor. Nome desconhecido fica como esta: sumir carimbo e pior do que
// carimbo errado, porque ninguem percebe.
std::string expand(const std::string& texto, const OrderedStrMap& valores);
std::vector<std::string> tokens_in(const std::string& texto);

}  // namespace cnc::cad
