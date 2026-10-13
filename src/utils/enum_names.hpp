// Enum <-> texto, para ler e gravar os mesmos valores do app em Python
// (ex.: ProfileSide.OUTSIDE.value == "outside").
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cnc {

// Especialize dentro de namespace cnc com a macro CNC_ENUM (abaixo).
template <class E>
struct EnumNames;

template <class E>
const char* to_str(E value) {
    for (const auto& [v, name] : EnumNames<E>::table) {
        if (v == value) return name;
    }
    return "";
}

// Como Enum(valor) do Python: devolve nullopt onde o Python levantaria ValueError.
template <class E>
std::optional<E> from_str(std::string_view text) {
    for (const auto& [v, name] : EnumNames<E>::table) {
        if (text == name) return v;
    }
    return std::nullopt;
}

}  // namespace cnc

// Declara a tabela:  CNC_ENUM(E, {E::A, "a"}, {E::B, "b"})   (dentro de namespace cnc)
#define CNC_ENUM(E, ...)                                                    \
    template <>                                                             \
    struct EnumNames<E> {                                                   \
        static constexpr std::pair<E, const char*> table[] = {__VA_ARGS__}; \
    }
