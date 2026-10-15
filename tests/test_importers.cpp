#include <fstream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <doctest.h>

#include "cad/entities.hpp"
#include "io/importers.hpp"

using namespace cnc;

namespace {

std::filesystem::path write_fixture(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
    return path;
}

}  // namespace

TEST_CASE("importadores: SVG normaliza e converte formas e caminhos") {
    const auto path = write_fixture(
        "cnccam_import_svg_test.svg",
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 80">
          <path d="M 10 80 L 30 80 L 30 70 Z"/>
          <circle cx="60" cy="30" r="5"/>
          <ellipse cx="80" cy="60" rx="4" ry="2"/>
        </svg>)");

    const auto entities = io::import_svg(path);
    REQUIRE(entities.size() == 3);
    CHECK(entities[0]->kind() == cad::EntityKind::Polyline);
    CHECK(entities[1]->kind() == cad::EntityKind::Circle);
    CHECK(entities[2]->kind() == cad::EntityKind::Ellipse);
    const auto box = entities[0]->bounding_box();
    CHECK(box.x0 == doctest::Approx(0.0));
    CHECK(box.y0 == doctest::Approx(0.0));
    CHECK(entities[0]->closed());

    std::filesystem::remove(path);
}

TEST_CASE("importadores: DXF ASCII aceita polilinhas antigas e splines") {
    const auto path = write_fixture(
        "cnccam_import_dxf_test.dxf",
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\nCORTE\n10\n10\n20\n20\n11\n30\n21\n20\n"
        "0\nLWPOLYLINE\n8\nGRAVACAO\n70\n1\n10\n40\n20\n10\n10\n60\n20\n10\n"
        "0\nPOLYLINE\n70\n1\n"
        "0\nVERTEX\n10\n70\n20\n10\n"
        "0\nVERTEX\n10\n80\n20\n10\n"
        "0\nSEQEND\n"
        "0\nSPLINE\n70\n0\n10\n90\n20\n20\n10\n100\n20\n20\n"
        "0\nENDSEC\n0\nEOF\n");

    const auto entities = io::import_dxf(path);
    REQUIRE(entities.size() == 4);
    CHECK(entities[0]->kind() == cad::EntityKind::Line);
    CHECK(entities[0]->layer == "CORTE");
    CHECK(entities[1]->kind() == cad::EntityKind::Polyline);
    CHECK(entities[1]->layer == "GRAVACAO");
    CHECK(entities[2]->kind() == cad::EntityKind::Polyline);
    CHECK(entities[3]->kind() == cad::EntityKind::Polyline);
    CHECK(entities[1]->closed());
    CHECK(entities[2]->closed());
    CHECK(!entities[3]->closed());
    CHECK(entities[0]->bounding_box().x0 == doctest::Approx(0.0));

    std::filesystem::remove(path);
}

TEST_CASE("importadores: extensoes e PDF invalido") {
    CHECK(io::is_supported("peca.SVG"));
    CHECK(io::is_supported("peca.dxf"));
    CHECK(io::is_supported("peca.PDF"));
    CHECK_FALSE(io::is_supported("peca.cnc"));

    CHECK_THROWS(io::import_file("peca.pdf"));
}

TEST_CASE("importadores: PDF vetorial nativo abre arquivo real") {
#ifdef CNC_HAVE_MUPDF
    const auto path = std::filesystem::path(CNC_SOURCE_DIR).parent_path() / "CNC-CAM" /
                      "artifacts" / "router_cnc_brasil_2026-10-11.pdf";
    if (!std::filesystem::exists(path)) return;
    const auto entities = io::import_file(path);
    CHECK_FALSE(entities.empty());
    MESSAGE("Entities count: ", entities.size());
    if (!entities.empty()) CHECK(entities.front()->layer == "PDF p1");
#endif
}

TEST_CASE("importadores: pasta em lote cria uma camada por arquivo") {
    const auto folder = std::filesystem::temp_directory_path() / "cnccam_import_batch_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "sub");
    {
        std::ofstream file(folder / "a.svg", std::ios::binary);
        file << R"(<svg><line x1="0" y1="0" x2="10" y2="0"/></svg>)";
    }
    {
        std::ofstream file(folder / "sub" / "b.dxf", std::ios::binary);
        file << "0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n0\n20\n0\n11\n5\n21\n0\n0\nENDSEC\n0\nEOF\n";
    }

    const auto result = io::import_folder(folder);
    REQUIRE(result.total_files == 2);
    REQUIRE(result.imported == 2);
    CHECK(result.entities_by_file.count("a") == 1);
    CHECK(result.entities_by_file.count("sub/b") == 1);
    CHECK(result.failed.empty());

    std::filesystem::remove_all(folder);
}

TEST_CASE("importadores: gravacao do nome do arquivo na parte inferior com 200mm") {
    std::vector<cad::EntityPtr> ents;
    auto rect = std::make_shared<cad::Rectangle>();
    rect->x = 0; rect->y = 0; rect->width = 600; rect->height = 400;
    ents.push_back(rect);

    auto label = io::create_filename_label("gabinete_lateral.dxf", ents);
    REQUIRE(label != nullptr);
    CHECK(label->content == "gabinete_lateral.dxf");
    CHECK(label->layer == "Gravação");
    const auto box = label->bounding_box();
    CHECK(box.x1 - box.x0 == doctest::Approx(200.0).epsilon(0.01));
    CHECK((box.x0 + box.x1) / 2.0 == doctest::Approx(300.0).epsilon(0.01));
    CHECK(box.y1 <= -5.0);
}
