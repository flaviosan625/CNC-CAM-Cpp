// Percursos de CAM: o C++ tem de dar o MESMO resultado do app em Python.
//
// Os arquivos em tests/golden foram gerados por scripts/golden/gen_cam_golden.py rodando
// o codigo original. Para regenerar:
//     cd C:\Users\flavi\Desktop\CNC-CAM
//     venv\Scripts\python.exe "<projeto C++>\scripts\golden\gen_cam_golden.py" profile
#include "golden.hpp"

using namespace cnc;

namespace {
void run(const char* file, int min_cases) {
    if (!testing::golden_exists(file)) {
        MESSAGE("sem golden file: " << file << " (rode scripts/golden/gen_cam_golden.py)");
        return;
    }
    const int n = testing::check_cam_golden(file);
    CHECK(n >= min_cases);
}
}  // namespace

TEST_CASE("golden: perfil (Profile) igual ao Python") { run("profile.json", 40); }
TEST_CASE("golden: bolso (Pocket) igual ao Python") { run("pocket.json", 25); }

TEST_CASE("golden: furacao (Drill) igual ao Python") { run("drill.json", 15); }

TEST_CASE("golden: entalhe V (VCarve) igual ao Python") { run("vcarve.json", 20); }

TEST_CASE("golden: canais (Fluting) igual ao Python") { run("fluting.json", 10); }

TEST_CASE("golden: rasgo (Keyhole) igual ao Python") { run("keyhole.json", 5); }

TEST_CASE("golden: inlay igual ao Python") { run("inlay.json", 5); }

TEST_CASE("golden: faca de arraste (Dragknife) igual ao Python") { run("dragknife.json", 7); }

TEST_CASE("golden: textura (Texture) igual ao Python") { run("texture.json", 9); }
