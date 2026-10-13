// Importacao somente leitura do banco de ferramentas Vectric/Aspire 8.5
// (porte de src/cam/vectric_tools.py).
//
// O formato antigo tools.tool_db e um arquivo binario. Os registros de ferramenta guardam um
// bloco estavel de cinco doubles, tres inteiros e uma string terminada em NUL. O importador
// procura apenas blocos que passam por limites fisicos rigorosos; bytes desconhecidos e
// geometrias que o CNC-CAM nao representa sao ignorados.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cam/tools.hpp"

namespace cnc::cam {

struct VectricReadResult {
    std::vector<Tool> tools;           // ferramentas suportadas
    std::vector<std::string> unsupported;  // nomes nao suportados
    int found = 0;                     // registros validos encontrados
};

struct VectricImportReport {
    std::filesystem::path source;
    int found = 0;
    std::vector<ToolDatabase::ToolPtr> imported;
    int duplicates = 0;
    std::vector<std::string> unsupported;
};

std::filesystem::path default_aspire_tool_db();

// Le o arquivo binario. Lanca std::runtime_error se nao abrir.
VectricReadResult read_vectric_tools(const std::filesystem::path& path);

// Importa para o banco, pulando o que ja existe (mesma assinatura) e renumerando as ferramentas.
// Sem caminho, usa o do Aspire 8.5 desta maquina.
VectricImportReport import_aspire_tools(ToolDatabase& database, const std::filesystem::path& path = {});

}  // namespace cnc::cam
