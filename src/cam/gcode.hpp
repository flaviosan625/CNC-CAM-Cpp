// Pos-processador: percursos internos -> G-Code (porte de src/cam/gcode.py).
//
// Perfis de controladora cobertos, com as diferencas que realmente importam:
//  * GRBL nao tem ciclo fixo de furacao (G81/G83). O ciclo peck sai expandido em G0/G1.
//  * Os ciclos de furacao saem expandidos em G0/G1 para evitar depender de ciclos fixos.
//  * RichAuto A11/A18 usa G54 e nao recebe G94 neste perfil; alimentacao fica em mm/min.
// Saida modal: cada eixo so aparece quando muda, e o avanco so quando muda.
#pragma once

#include <string>
#include <vector>

#include "cad/material.hpp"
#include "cam/model.hpp"
#include "utils/enum_names.hpp"

namespace cnc::cam {

enum class PostProcessor { Grbl, Mach3, RichAutoA11, RichAutoA18, NcStudio, LinuxCnc, VectricAspire };
const char* post_label(PostProcessor p);

struct VectricPostFile {
    std::string name;
    std::string path;
    std::string extension;
};
std::vector<VectricPostFile> installed_vectric_post_files();

}  // namespace cnc::cam

namespace cnc {
CNC_ENUM(cam::PostProcessor, {cam::PostProcessor::Grbl, "grbl"}, {cam::PostProcessor::Mach3, "mach3"},
         {cam::PostProcessor::RichAutoA11, "richauto_a11"},
         {cam::PostProcessor::RichAutoA18, "richauto_a18"},
         {cam::PostProcessor::NcStudio, "ncstudio"},
         {cam::PostProcessor::LinuxCnc, "linuxcnc"},
         {cam::PostProcessor::VectricAspire, "vectric_aspire"});
}  // namespace cnc

namespace cnc::cam {

struct GCodeSettings {
    PostProcessor post = PostProcessor::Grbl;
    int decimals = 3;
    bool line_numbers = false;
    int line_increment = 10;
    std::string spindle_on = "M3";
    std::string spindle_off = "M5";
    std::string coolant_on;   // M8 se a maquina tiver refrigeracao
    std::string coolant_off;  // M9
    std::string program_end;  // vazio = padrao da controladora
    bool tool_change = false; // default seguro para GRBL; habilitar M6 conforme controladora
    bool comments = true;
    bool home_at_end = true;
    std::string vectric_post_file;
    std::string vectric_post_extension;

    std::string end_command() const;
};

// Remove acentos e parenteses dos comentarios. A controladora le o arquivo como ASCII: um
// travessao vira lixo na tela e um parentese dentro de comentario fecha o comentario cedo e
// transforma o resto da linha em comando.
std::string ascii_safe(const std::string& text);

// Texto do G-code. So entram percursos visiveis e com movimentos.
std::string generate_gcode(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                           const GCodeSettings& s = {}, const std::string& job_name = "");
std::string generate_gcode(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                           const GCodeSettings& s = {}, const std::string& job_name = "");

// Resumo em texto para levar para a maquina.
std::string setup_sheet(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                        const std::string& job_name = "");
std::string setup_sheet(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                        const std::string& job_name = "");

}  // namespace cnc::cam
