// Folha de trabalho em HTML - como o Setup Sheet do Aspire (porte de src/cam/setup_sheet.py).
// Arquivo HTML autonomo: dados do material e zero, tabela de percursos com ferramenta, avancos,
// profundidade e tempo, avisos de troca de ferramenta e tempo total. Estilo escuro que imprime
// bem em claro.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cad/material.hpp"
#include "cam/model.hpp"

namespace cnc::cam {

std::string html_escape(const std::string& text);

std::string setup_sheet_html(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                             const std::string& job_name = "");
std::string setup_sheet_html(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                             const std::string& job_name = "");

}  // namespace cnc::cam
