#include <doctest.h>

#include "cad/relief.hpp"
#include "cad/model3d.hpp"

#include <cmath>
#include <vector>

using namespace cnc::cad;
using namespace cnc::cad::relief;

TEST_CASE("relevo_3d: local grid roi e amostragem bilinear") {
    LocalGrid grid;
    grid.x0 = 0.0;
    grid.y0 = 0.0;
    grid.width = 10.0;
    grid.height = 10.0;
    grid.step = 5.0; // 3x3 pontos
    grid.cols = 3;
    grid.rows = 3;
    grid.z = {
        0.0f, 2.0f, 4.0f,
        1.0f, 3.0f, 5.0f,
        2.0f, 4.0f, 6.0f
    };

    // Amostragem nos nós
    CHECK(std::abs(grid.sample_bilinear(0.0, 0.0) - 0.0f) < 1e-4f);
    CHECK(std::abs(grid.sample_bilinear(10.0, 10.0) - 6.0f) < 1e-4f);
    CHECK(std::abs(grid.sample_bilinear(5.0, 5.0) - 3.0f) < 1e-4f);

    // Amostragem bilinear no ponto médio (2.5, 2.5)
    // Z esperado: media de (0, 2, 1, 3) = 1.5
    CHECK(std::abs(grid.sample_bilinear(2.5, 2.5) - 1.5f) < 1e-3f);
}

TEST_CASE("relevo_3d: preservacao de precisao 16-bit vs 8-bit") {
    ReliefComponent comp16;
    comp16.img_width = 2;
    comp16.img_height = 2;
    comp16.is_16bit = true;
    // 0, 32767 (~0.5), 65535 (1.0), 16384 (~0.25)
    comp16.raw_16bit = {0, 32767, 65535, 16384};

    CHECK(comp16.has_image());
    CHECK(std::abs(comp16.sample_normalized(0.0, 0.0) - 0.0f) < 1e-4f);
    CHECK(std::abs(comp16.sample_normalized(1.0, 0.0) - (32767.0f / 65535.0f)) < 1e-4f);
    CHECK(std::abs(comp16.sample_normalized(0.0, 1.0) - 1.0f) < 1e-4f);

    ReliefComponent comp8;
    comp8.img_width = 2;
    comp8.img_height = 2;
    comp8.is_16bit = false;
    comp8.raw_8bit = {0, 128, 255, 64};
    CHECK(std::abs(comp8.sample_normalized(0.0, 0.0) - 0.0f) < 1e-4f);
    CHECK(std::abs(comp8.sample_normalized(1.0, 0.0) - (128.0f / 255.0f)) < 1e-3f);
    CHECK(std::abs(comp8.sample_normalized(0.0, 1.0) - 1.0f) < 1e-4f);
}

TEST_CASE("relevo_3d: 5 modos de combinacao (Add, Subtract, MergeHigh, MergeLow, Multiply)") {
    // Componente base: 20x20 mm com rampa
    ReliefComponent comp1;
    comp1.name = "Base";
    comp1.pos_x = 0.0;
    comp1.pos_y = 0.0;
    comp1.width = 20.0;
    comp1.height = 20.0;
    comp1.shape_height = 4.0;
    comp1.base_height = 1.0; // Altura base separada
    comp1.combine_mode = CombineMode::Add;
    comp1.img_width = 2;
    comp1.img_height = 2;
    comp1.raw_8bit = {255, 255, 255, 255}; // Valor plano normalizado 1.0 -> Z = 1.0 + 4.0 = 5.0 mm

    // Componente 2: Relevo sobreposto
    ReliefComponent comp2;
    comp2.name = "Sobreposto";
    comp2.pos_x = 0.0;
    comp2.pos_y = 0.0;
    comp2.width = 20.0;
    comp2.height = 20.0;
    comp2.shape_height = 2.0;
    comp2.base_height = 0.0;
    comp2.img_width = 2;
    comp2.img_height = 2;
    comp2.raw_8bit = {255, 255, 255, 255}; // Valor plano normalizado 1.0 -> Z = 2.0 mm

    // 1. ADICIONAR (+): Z1 + Z2 = 5.0 + 2.0 = 7.0 mm
    comp2.combine_mode = CombineMode::Add;
    LocalGrid composite_add;
    ReliefEngine::compose({comp1, comp2}, 20.0, 20.0, 5.0, composite_add);
    CHECK(std::abs(composite_add.get(1, 1) - 7.0f) < 1e-3f);

    // 2. SUBTRAIR (-): Z1 - Z2 = 5.0 - 2.0 = 3.0 mm (entalhe)
    comp2.combine_mode = CombineMode::Subtract;
    LocalGrid composite_sub;
    ReliefEngine::compose({comp1, comp2}, 20.0, 20.0, 5.0, composite_sub);
    CHECK(std::abs(composite_sub.get(1, 1) - 3.0f) < 1e-3f);

    // 3. MESCLAR ALTO (Merge High): max(Z1, Z2) = max(5.0, 2.0) = 5.0 mm
    comp2.combine_mode = CombineMode::MergeHigh;
    LocalGrid composite_high;
    ReliefEngine::compose({comp1, comp2}, 20.0, 20.0, 5.0, composite_high);
    CHECK(std::abs(composite_high.get(1, 1) - 5.0f) < 1e-3f);

    // 4. MESCLAR BAIXO (Merge Low): min(Z1, Z2) = min(5.0, 2.0) = 2.0 mm
    comp2.combine_mode = CombineMode::MergeLow;
    LocalGrid composite_low;
    ReliefEngine::compose({comp1, comp2}, 20.0, 20.0, 5.0, composite_low);
    CHECK(std::abs(composite_low.get(1, 1) - 2.0f) < 1e-3f);

    // 5. MULTIPLICAR: (Z1 * Z2) / shape_height = (5.0 * 2.0) / 2.0 = 5.0 mm
    comp2.combine_mode = CombineMode::Multiply;
    LocalGrid composite_mul;
    ReliefEngine::compose({comp1, comp2}, 20.0, 20.0, 5.0, composite_mul);
    CHECK(std::abs(composite_mul.get(1, 1) - 5.0f) < 1e-3f);
}

TEST_CASE("relevo_3d: trava de seguranca de altura maxima 200 mm") {
    ReliefComponent huge_comp;
    huge_comp.pos_x = 0.0;
    huge_comp.pos_y = 0.0;
    huge_comp.width = 10.0;
    huge_comp.height = 10.0;
    huge_comp.shape_height = 350.0; // Ultrapassa o limite de 200 mm
    huge_comp.img_width = 2;
    huge_comp.img_height = 2;
    huge_comp.raw_8bit = {255, 255, 255, 255};

    LocalGrid composite;
    ReliefEngine::compose({huge_comp}, 10.0, 10.0, 5.0, composite, 200.0);
    for (float z : composite.z) {
        CHECK(z <= 200.0f);
    }
}

TEST_CASE("relevo_3d: serializacao e roundtrip em Part") {
    model3d::Part part;
    part.name = "Peça Teste Relevo";

    model3d::Extrusion ext;
    ext.name = "Base MDF";
    ext.width = 200.0;
    ext.height = 150.0;
    ext.depth = 18.0;
    part.features.push_back(ext);

    ReliefComponent rc;
    rc.id = "comp_flor_1";
    rc.name = "Flor em Relevo";
    rc.pos_x = 10.0;
    rc.pos_y = 15.0;
    rc.width = 80.0;
    rc.height = 80.0;
    rc.shape_height = 4.5;
    rc.base_height = 1.2;
    rc.smoothness = 0.35;
    rc.contrast = 1.25;
    rc.invert = true;
    rc.combine_mode = CombineMode::MergeHigh;
    rc.img_width = 2;
    rc.img_height = 2;
    rc.raw_8bit = {10, 50, 100, 200};
    part.relief_components.push_back(rc);

    // Salvar em arquivo temporário
    std::string error;
    const std::string path = "test_part_relief.cncpart";
    CHECK(model3d::save_part(part, path, &error));

    // Reabrir
    model3d::Part loaded;
    CHECK(model3d::load_part(loaded, path, &error));
    CHECK(loaded.name == "Peça Teste Relevo");
    REQUIRE(loaded.features.size() == 1);
    CHECK(loaded.features[0].name == "Base MDF");
    REQUIRE(loaded.relief_components.size() == 1);

    const auto& l_rc = loaded.relief_components[0];
    CHECK(l_rc.id == "comp_flor_1");
    CHECK(l_rc.name == "Flor em Relevo");
    CHECK(std::abs(l_rc.pos_x - 10.0) < 1e-4);
    CHECK(std::abs(l_rc.shape_height - 4.5) < 1e-4);
    CHECK(std::abs(l_rc.base_height - 1.2) < 1e-4);
    CHECK(std::abs(l_rc.smoothness - 0.35) < 1e-4);
    CHECK(std::abs(l_rc.contrast - 1.25) < 1e-4);
    CHECK(l_rc.invert == true);
    CHECK(l_rc.combine_mode == CombineMode::MergeHigh);
}

TEST_CASE("relevo_3d: geracao de malha com shading") {
    LocalGrid grid;
    grid.x0 = 0.0;
    grid.y0 = 0.0;
    grid.width = 10.0;
    grid.height = 10.0;
    grid.step = 5.0;
    grid.cols = 3;
    grid.rows = 3;
    grid.z = {
        0.0f, 1.0f, 0.0f,
        1.0f, 2.0f, 1.0f,
        0.0f, 1.0f, 0.0f
    };

    auto mesh = ReliefEngine::generate_mesh(grid, 18.0);
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    // 3x3 pontos = 9 vertices * 6 floats = 54 floats
    CHECK(mesh.vertices.size() == 54);
    // (3-1)*(3-1) = 4 quads * 2 triângulos * 3 índices = 24 índices
    CHECK(mesh.indices.size() == 24);
}

