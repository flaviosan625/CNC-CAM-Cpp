// Onde ficam as preferencias do usuario (porte de src/utils/user_config.py).
//
// Existe por um motivo pratico: teste que grava em ~/.cnc-cam mexe na configuracao
// real de quem usa o programa. Ja aconteceu no app em Python: uma bateria de testes
// desligou o snap do usuario. Por isso a decisao mora aqui e nao depende de quem
// roda os testes: processo de teste NUNCA escreve na pasta real.
#pragma once

#include <filesystem>
#include <string>

namespace cnc::user_config {

// Variavel de ambiente que redireciona a pasta de configuracao.
inline constexpr const char* kEnvVar = "CNC_CAM_CONFIG";

// O executavel de testes chama isto no inicio; dai em diante, sem CNC_CAM_CONFIG,
// a configuracao vai para uma pasta temporaria em vez de ~/.cnc-cam.
void mark_under_test();
bool running_under_test();

std::filesystem::path config_dir();
std::filesystem::path config_file(const std::string& name);

}  // namespace cnc::user_config
