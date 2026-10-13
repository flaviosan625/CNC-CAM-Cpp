// (De)serializacao generica dos *Settings, como settings_to_dict/settings_from_dict
// do Python (src/cam/job.py), que percorrem os campos do dataclass por reflexao.
//
// Em C++ nao ha reflexao: cada Settings declara uma funcao
//     template <class S, class F> void visit_fields(S& s, F&& f) { f("campo", s.campo); ... }
// (encontrada por ADL) e daqui sai o to_json/from_json. A ordem dos campos no visit e a
// ordem do dataclass, para o arquivo gravado sair com as chaves na mesma ordem.
#pragma once

#include <string>
#include <type_traits>
#include <vector>

#include "cam/tools.hpp"
#include "utils/enum_names.hpp"
#include "utils/json_util.hpp"

namespace cnc::cam::io {

template <class T>
struct is_vector : std::false_type {};
template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T>
Json encode(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return Json(to_str(v));
    } else if constexpr (std::is_same_v<T, std::string> || std::is_arithmetic_v<T>) {
        return Json(v);
    } else if constexpr (std::is_same_v<T, Tool>) {
        // _serialize do Python: uma Tool dentro de um settings vai embrulhada
        Json wrapped = Json::object();
        wrapped["__cam_type__"] = "tool";
        wrapped["value"] = v.to_json();
        return wrapped;
    } else if constexpr (is_vector<T>::value) {
        Json arr = Json::array();
        for (const auto& item : v) arr.push_back(encode(item));
        return arr;
    } else {
        return v.to_json();  // estrutura propria com to_json()
    }
}

// Campo ausente ou valor invalido: o chamador mantem o padrao (arquivo de versao
// anterior continua abrindo - igual ao Python).
template <class T>
void decode(const Json& j, T& out) {
    if constexpr (std::is_enum_v<T>) {
        if (auto e = from_str<T>(j.get<std::string>())) out = *e;
    } else if constexpr (std::is_same_v<T, std::string> || std::is_arithmetic_v<T>) {
        out = j.get<T>();
    } else if constexpr (std::is_same_v<T, Tool>) {
        out = Tool::from_json(j.value("value", Json::object()));
    } else if constexpr (is_vector<T>::value) {
        T tmp;
        for (const auto& item : j) {
            typename T::value_type v{};
            decode(item, v);
            tmp.push_back(std::move(v));
        }
        out = std::move(tmp);
    } else {
        out = T::from_json(j);
    }
}

template <class S>
Json settings_to_json(const S& s) {
    Json out = Json::object();
    visit_fields(s, [&](const char* name, const auto& value) { out[name] = encode(value); });
    return out;
}

template <class S>
S settings_from_json(const Json& data) {
    S s{};
    visit_fields(s, [&](const char* name, auto& value) {
        auto it = data.find(name);
        if (it == data.end()) return;
        try {
            decode(*it, value);
        } catch (const std::exception&) {
            // valor de tipo errado: fica o padrao
        }
    });
    return s;
}

}  // namespace cnc::cam::io
