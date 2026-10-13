// Excecoes que o app em Python levanta e o job transforma em mensagem para o usuario
// ("ValueError: ..."), como em ToolpathJob.calculate.
#pragma once

#include <stdexcept>

namespace cnc {

struct ValueError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace cnc
