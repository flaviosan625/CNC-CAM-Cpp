// Atalhos de uma tecla, configuraveis pelo operador (porte de src/utils/shortcuts.py).
//
// Ideia trazida do SolidWorks: quem usa o programa o dia inteiro sabe melhor que o autor quais
// teclas quer debaixo da mao. A tabela tecla -> acao e um arquivo no perfil do usuario, nao uma
// constante no codigo. Sao atalhos de uma tecla so, sem Ctrl/Alt/Shift - os com modificador
// continuam nos menus. So valem com o foco no desenho.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include <QtGlobal>

#include "utils/user_config.hpp"

namespace cnc::shortcuts {

using Table = std::map<std::string, std::string>;  // tecla (maiuscula) -> acao

// Acao por tecla, como sai de fabrica. A chave e o nome da tecla em maiuscula ("V", "L",
// "DELETE"); o valor e a acao que o app executa.
const Table& defaults();

// Teclas que o programa usa para outra coisa e nao podem virar atalho.
const std::set<std::string>& reserved();

std::filesystem::path path();
// Tabela tecla -> acao, com os padroes para o que nao foi personalizado. Se o arquivo existe,
// ele manda: o que o operador apagou fica apagado.
Table load();
void save(const Table& table);

// Nome da tecla de um evento Qt (valor de Qt::Key), ou nada se nao serve como atalho.
std::optional<std::string> key_name(int key);
std::optional<std::string> action_for(int key, const Table* table = nullptr);

}  // namespace cnc::shortcuts
