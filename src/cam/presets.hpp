// Modelos de corte da casa - os 9 "Corte Automatico" por material
// (porte de src/cam/presets.py).
//
// Os numeros nao foram inventados: vieram de corte_parametros.py do projeto
// etiquetas_python, que alimenta os gadgets do Aspire e ja cortou chapa de verdade.
//
// Tres regras herdadas de la, todas ditadas pela pratica:
//  * Corta 1 mm alem da espessura. Chapa de 15 vira profundidade 16; parar exatamente
//    na espessura deixa a peca presa por um filme.
//  * A passada e por material E espessura, nao so por material.
//  * Avanco, rotacao e passo lateral sao iguais em tudo; muda a fresa e a passada.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cam/tools.hpp"

namespace cnc::cam {

struct ProfileSettings;  // cam/profile.hpp

// Quanto passar alem da chapa para garantir corte passante.
inline constexpr double kPassanteMm = 1.0;
// Iguais em todos os materiais - "deixar com mesmo parametro, so mudar a fresa".
inline constexpr double kAvancoMmMin = 2000.0;
inline constexpr double kMergulhoMmMin = 1000.0;
inline constexpr int kRotacaoRpm = 18000;
inline constexpr double kPassoLateralMm = 2.0;
inline constexpr double kRampaMm = 10.0;

// Uma fresa da casa com os dados de corte padrao.
Tool house_tool(double diameter, double pass_depth);
// As tres fresas da casa, com a passada mais usada de cada uma.
std::vector<Tool> house_tools();

// Parametros de corte de uma chapa.
struct MaterialPreset {
    std::string material;
    double thickness;
    double diameter;
    double pass_depth;
    std::string note;

    std::string name() const;
    double cut_depth() const { return thickness + kPassanteMm; }
    int passes() const;
    Tool tool() const { return house_tool(diameter, pass_depth); }
    ProfileSettings settings() const;
    std::string describe() const;
};

// As nove combinacoes que a casa realmente corta.
const std::vector<MaterialPreset>& house_presets();

// Preset de uma chapa, ou nullopt se a combinacao nao esta cadastrada. Devolver nada e
// de proposito: material sem parametro e material que ninguem definiu como cortar, e
// chutar isso quebra fresa.
std::optional<MaterialPreset> find_preset(const std::string& material, double thickness);
// Presets que combinam com a espessura do trabalho atual.
std::vector<MaterialPreset> presets_for_thickness(double thickness);

}  // namespace cnc::cam
