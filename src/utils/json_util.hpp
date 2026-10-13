// Atalhos para o JSON dos arquivos .cnc e das configuracoes.
//
// ordered_json mantem a ordem de insercao das chaves, como o dict do Python: o
// arquivo gravado aqui fica com os campos na mesma ordem que o app em Python grava.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "geo/types.hpp"

namespace cnc {

using Json = nlohmann::ordered_json;

// dict.get(key, default) do Python. Chave ausente ou null devolve o padrao; tipo
// errado lanca (o Python tambem falharia na primeira conta com o valor).
template <class T>
T jget(const Json& j, const char* key, const T& fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    return it->template get<T>();
}

inline Json points_to_json(const Points& pts) {
    Json arr = Json::array();
    for (const Pt& p : pts) arr.push_back(Json::array({p.x, p.y}));
    return arr;
}

inline Points points_from_json(const Json& arr) {
    Points out;
    out.reserve(arr.size());
    for (const Json& p : arr) out.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return out;
}

inline Json rgb_to_json(const Rgb& c) { return Json::array({c[0], c[1], c[2]}); }

inline Rgb rgb_from_json(const Json& j, const Rgb& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()};
}

// Caminhos <-> texto UTF-8 (std::filesystem::path::string() usaria a pagina ANSI e
// estragaria acentos).
inline std::string path_to_utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
inline std::filesystem::path path_from_utf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// Le um arquivo UTF-8 inteiro. Lanca std::runtime_error se nao abrir.
std::string read_text_file(const std::filesystem::path& path);
// Grava UTF-8 (sem BOM). Cria a pasta pai se preciso? Nao: igual ao Python, quem chama decide.
void write_text_file(const std::filesystem::path& path, const std::string& text);

// json.dumps(obj, indent=2, ensure_ascii=False)
inline std::string dumps_pretty(const Json& j) { return j.dump(2); }

}  // namespace cnc
