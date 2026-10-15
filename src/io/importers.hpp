// Importacao e exportacao de vetores - DXF, SVG, PDF (porte de src/cad/importers/__init__.py).
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include "cad/entities.hpp"

namespace cnc::cad {
class Document;
}

namespace cnc::io {

// Extensoes aceitas na importacao e no arrastar-e-soltar.
inline constexpr std::array<const char*, 3> kSupportedSuffixes = {".dxf", ".svg", ".pdf"};

// Filtro pronto para o QFileDialog.
inline constexpr const char* kImportFilter =
    "Vetores (*.pdf *.dxf *.svg);;PDF (*.pdf);;DXF (*.dxf);;SVG (*.svg);;Todos os arquivos (*)";

bool is_supported(const std::filesystem::path& path);

// Importa vetores e normaliza o conjunto para que o canto inferior esquerdo fique em (0, 0).
std::vector<cad::EntityPtr> import_file(const std::filesystem::path& path);
std::vector<cad::EntityPtr> import_svg(const std::filesystem::path& path);
std::vector<cad::EntityPtr> import_dxf(const std::filesystem::path& path);

struct BatchImportResult {
    size_t total_files = 0;
    size_t imported = 0;
    std::vector<std::string> failed;
    std::map<std::string, std::vector<cad::EntityPtr>> entities_by_file;
};

BatchImportResult import_folder(const std::filesystem::path& folder, bool recursive = true);

// Exporta o desenho para SVG em milimetros, mantendo a origem no canto inferior esquerdo.
bool export_svg(const std::filesystem::path& path, const cnc::cad::Document& document);

// Cria entidade de texto com o nome do arquivo gravado na parte inferior do desenho, com largura de 200 mm.
std::shared_ptr<cad::Text> create_filename_label(const std::string& filename,
                                                 const std::vector<cad::EntityPtr>& entities,
                                                 const std::string& layer = "Gravação");

}  // namespace cnc::io
