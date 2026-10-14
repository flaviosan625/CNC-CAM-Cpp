#include <doctest.h>

#include <QFile>
#include <QPixmap>
#include <QTemporaryDir>

#include "cam/gcode.hpp"
#include "cam/job_sheet.hpp"

using namespace cnc;

TEST_CASE("pranchas ilustrativas portadas do Python estão empacotadas como recursos Qt") {
    for (const char* name : {"01_usinar_vetores", "02_profundidades", "03_pontes",
                             "04_entradas_saidas", "05_rampas", "06_limpar_area",
                             "07_ataque_rampeado", "08_material", "09_ferramentas", "10_estrela"}) {
        const QPixmap image(QString(":/assets/figuras/%1.png").arg(QString::fromLatin1(name)));
        INFO("figura: " << name);
        CHECK_FALSE(image.isNull());
    }
}

TEST_CASE("ordem de serviço inclui dados da peça, cotas, ferramenta e pontes") {
    cam::Toolpath path;
    path.name = "Contorno externo";
    path.tool_name = "Fresa 6 mm";
    path.tool_number = 2;
    path.spindle_speed = 18000;
    path.feed_rate = 2400;
    path.plunge_rate = 500;
    path.moves = {{cam::MoveKind::Rapid, 10, 20, 5},
                  {cam::MoveKind::Plunge, 10, 20, -3},
                  {cam::MoveKind::Cut, 40, 20, -3},
                  {cam::MoveKind::Cut, 40, 60, -3}};

    cam::ToolpathJob job;
    job.tool.name = path.tool_name;
    job.tool.tool_number = path.tool_number;
    job.tool.diameter = 6;
    job.tool.pass_depth = 3;
    job.tool.stepover = 2.4;
    auto& profile = std::get<cam::ProfileSettings>(job.settings);
    profile.tabs = true;
    profile.tabs_3d = true;
    profile.tab_count = 4;
    profile.manual_tabs.push_back({"vector-id", 25, 20});

    cad::MaterialSetup material;
    material.width = 300;
    material.height = 200;
    material.thickness = 18;
    material.material_profile = "MDF 18 mm";
    material.clearance_z = 8;
    material.plunge_gap = 2;
    material.margin = 5;

    cam::SetupSheetInfo info;
    info.customer = "Cliente & <teste>";
    info.order_number = "OS-2026-042";
    info.part_name = "Suporte CNC";
    info.operator_name = "Flávio";
    info.notes = "Conferir fixação antes do corte.";
    info.logo_source = "job-logo";

    const std::string html = cam::job_setup_sheet_html({{&path, &job}}, material, "Projeto teste", info);
    CHECK(html.find("UNY CV") != std::string::npos);
    CHECK(html.find("ORDEM DE SERVIÇO") != std::string::npos);
    CHECK(html.find("Cliente &amp; &lt;teste&gt;") != std::string::npos);
    CHECK(html.find("OS-2026-042") != std::string::npos);
    CHECK(html.find("300 × 200 × 18 mm") != std::string::npos);
    CHECK(html.find("X 10.00–40.00") != std::string::npos);
    CHECK(html.find("Y 20.00–60.00") != std::string::npos);
    CHECK(html.find("4 auto + 1 manuais (3D)") != std::string::npos);
    CHECK(html.find("T2 · Fresa 6 mm") != std::string::npos);
    CHECK(html.find("Faixa Z de corte") != std::string::npos);
    CHECK(html.find("-3.00 / -3.00 mm") != std::string::npos);
    CHECK(html.find("job-logo") != std::string::npos);
    CHECK(html.find("8000 mm/min") != std::string::npos);
    CHECK(html.find("@@") == std::string::npos);
}

TEST_CASE("geração de ordem de serviço grava um PDF A4 válido") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString output_path = temp.filePath("OS-UNY-CV.pdf");

    cam::Toolpath path;
    path.name = "Corte de validação";
    path.tool_name = "Fresa 6 mm";
    path.moves = {{cam::MoveKind::Rapid, 0, 0, 5},
                  {cam::MoveKind::Plunge, 0, 0, -1},
                  {cam::MoveKind::Cut, 25, 0, -1},
                  {cam::MoveKind::Cut, 25, 15, -1}};
    cam::SetupSheetInfo info;
    info.customer = "Cliente teste";
    info.part_name = "Amostra";
    info.logo_source = "job-logo";

    QImage layout(640, 240, QImage::Format_ARGB32_Premultiplied);
    layout.fill(QColor("white"));
    QImage logo(160, 48, QImage::Format_ARGB32_Premultiplied);
    logo.fill(QColor("#1682d4"));
    QString error;
    REQUIRE(cam::write_job_setup_sheet_pdf(output_path, {{&path, nullptr}}, cad::MaterialSetup{},
                                           "Teste PDF", info, layout, logo, &error));
    CHECK(error.isEmpty());

    QFile pdf(output_path);
    REQUIRE(pdf.open(QIODevice::ReadOnly));
    CHECK(pdf.read(5) == "%PDF-");
    CHECK(pdf.size() > 2000);
}

TEST_CASE("exportação GRBL pode omitir troca M6 e Mach3 emitir troca configurada") {
    cam::Toolpath path;
    path.name = "Perfil";
    path.tool_name = "Fresa 6 mm";
    path.tool_number = 3;
    path.spindle_speed = 16000;
    path.moves = {{cam::MoveKind::Rapid, 0, 0, 5},
                  {cam::MoveKind::Plunge, 0, 0, -1},
                  {cam::MoveKind::Cut, 20, 0, -1}};

    cam::GCodeSettings settings;
    settings.post = cam::PostProcessor::Grbl;
    settings.tool_change = false;
    std::string code = cam::generate_gcode(std::vector<cam::Toolpath>{path}, cad::MaterialSetup{}, settings);
    CHECK(code.find("T3 M6") == std::string::npos);

    settings.post = cam::PostProcessor::Mach3;
    settings.tool_change = true;
    code = cam::generate_gcode(std::vector<cam::Toolpath>{path}, cad::MaterialSetup{}, settings);
    CHECK(code.find("T3 M6") != std::string::npos);
}
