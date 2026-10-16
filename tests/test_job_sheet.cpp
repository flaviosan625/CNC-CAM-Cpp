#include <doctest.h>

#include <QFile>
#include <QPixmap>
#include <QTemporaryDir>

#include "app/main_window.hpp"
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
    CHECK(html.find("T2 Fresa 6 mm") != std::string::npos);
    CHECK(html.find("XY/Z 2400/500mm/min") != std::string::npos);
    CHECK(html.find("Z -3.00…-3.00mm") != std::string::npos);
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
    QImage operation_sheet(640, 300, QImage::Format_ARGB32_Premultiplied);
    operation_sheet.fill(QColor("white"));
    QString error;
    REQUIRE(cam::write_job_setup_sheet_pdf(output_path, {{&path, nullptr}}, cad::MaterialSetup{},
                                           "Teste PDF", info, layout, logo, {operation_sheet},
                                           &error));
    CHECK(error.isEmpty());

    QFile pdf(output_path);
    REQUIRE(pdf.open(QIODevice::ReadOnly));
    CHECK(pdf.read(5) == "%PDF-");
    CHECK(pdf.size() > 2000);
}

TEST_CASE("ordem de serviço reúne todos os percursos em uma folha paisagem") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString output_path = temp.filePath("OS-multifolha.pdf");

    cam::Toolpath internals;
    internals.name = "Corte PVC 20 — internos";
    internals.tool_name = "Topo Raso (4 mm)";
    internals.tool_number = 4;
    internals.spindle_speed = 18000;
    internals.feed_rate = 2000;
    internals.plunge_rate = 1000;
    internals.moves = {{cam::MoveKind::Rapid, 0, 0, 5},
                       {cam::MoveKind::Plunge, 120, 90, -1},
                       {cam::MoveKind::Cut, 420, 90, -1},
                       {cam::MoveKind::Cut, 420, 760, -1},
                       {cam::MoveKind::Cut, 120, 760, -1},
                       {cam::MoveKind::Cut, 120, 90, -1}};
    cam::Toolpath externals = internals;
    externals.name = "Corte PVC 20 — externos";
    externals.moves = {{cam::MoveKind::Rapid, 0, 0, 5},
                       {cam::MoveKind::Plunge, 60, 40, -21},
                       {cam::MoveKind::Cut, 900, 40, -21},
                       {cam::MoveKind::Cut, 900, 1900, -21},
                       {cam::MoveKind::Cut, 60, 1900, -21},
                       {cam::MoveKind::Cut, 60, 40, -21}};

    cad::MaterialSetup material;
    material.width = 1220.0;
    material.height = 2440.0;
    material.thickness = 18.0;
    material.material_profile = "mdf";

    cam::SetupSheetInfo info;
    info.part_name = "Unipar_mdf15mm_34ao35";
    info.customer = "UNY CV";
    info.order_number = "07";
    info.notes = "Conferir fixação antes do corte externo.";

    const std::vector<cam::SetupSheetOperation> operations = {{&internals, nullptr},
                                                              {&externals, nullptr}};
    const QImage summary = app::make_job_layout_preview(operations, &material);
    const std::vector<QImage> per_path = app::make_operation_previews(operations, &material);
    REQUIRE(per_path.size() == 2);
    // The stock fills the frame instead of floating in padding: the projected
    // drawing is what the operator checks before cutting.
    CHECK(per_path[0].width() == summary.width());

    QString error;
    REQUIRE(cam::write_job_setup_sheet_pdf(output_path, operations, material, "Unipar", info,
                                           summary, QImage(), per_path, &error));

    QFile file(output_path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.count("/Type /Page\n") == 1);
    // A4 landscape is 842 x 595 pt; the writer must not fall back to portrait.
    CHECK(bytes.contains("/MediaBox [0 0 842.000000 595.000000]"));
}

TEST_CASE("exportação acumulada bloqueia o PDF em uma única página") {
    cam::Toolpath first_path;
    first_path.name = "Primeira OS";
    first_path.moves = {{cam::MoveKind::Plunge, 10, 10, -1},
                        {cam::MoveKind::Cut, 40, 10, -1}};
    cam::Toolpath second_path = first_path;
    second_path.name = "Segunda OS";
    cam::Toolpath third_path = first_path;
    third_path.name = "Terceira OS";

    QImage preview(640, 240, QImage::Format_ARGB32_Premultiplied);
    preview.fill(QColor("white"));
    std::vector<cam::MultiJobSheetEntry> entries(3);
    entries[0].operations = {{&first_path, nullptr}};
    entries[0].job_name = "Primeira OS";
    entries[0].info.part_name = "Primeira OS";
    entries[0].layout_preview = preview;
    entries[1].operations = {{&second_path, nullptr}};
    entries[1].job_name = "Segunda OS";
    entries[1].info.part_name = "Segunda OS";
    entries[1].layout_preview = preview;
    entries[2].operations = {{&third_path, nullptr}};
    entries[2].job_name = "Terceira OS";
    entries[2].info.part_name = "Terceira OS";
    entries[2].layout_preview = preview;

    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString output_path = temp.filePath("OS-primeira-folha.pdf");
    QString error;
    REQUIRE(cam::write_multi_job_setup_sheet_pdf(output_path, entries, {}, &error));
    CHECK(error.isEmpty());

    QFile file(output_path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.count("/Type /Page\n") == 1);
    CHECK(bytes.contains("/MediaBox [0 0 842.000000 595.000000]"));
}

TEST_CASE("preview da OS mostra letras usinadas e deixa deslocamentos rapidos de fora") {
    cam::Toolpath letters;
    letters.name = "Letras";
    letters.moves = {{cam::MoveKind::Rapid, 20, 40, 5},
                     {cam::MoveKind::Plunge, 20, 40, -1},
                     {cam::MoveKind::Cut, 80, 40, -1},
                     {cam::MoveKind::Cut, 80, 80, -1},
                     {cam::MoveKind::Cut, 20, 80, -1},
                     {cam::MoveKind::Rapid, 4000, 3500, 5},
                     {cam::MoveKind::Rapid, 120, 40, 5},
                     {cam::MoveKind::Plunge, 120, 40, -1},
                     {cam::MoveKind::Cut, 180, 40, -1},
                     {cam::MoveKind::Cut, 150, 40, -1},
                     {cam::MoveKind::Cut, 150, 80, -1},
                     {cam::MoveKind::Cut, 180, 80, -1}};
    cam::SetupSheetOperation operation{&letters, nullptr};
    cad::MaterialSetup material;
    material.width = 300;
    material.height = 200;
    material.thickness = 18;
    const QImage preview = app::make_job_layout_preview({operation}, &material);
    REQUIRE_FALSE(preview.isNull());
    cam::Toolpath without_rapid_jump = letters;
    without_rapid_jump.moves[5].x = 120;
    without_rapid_jump.moves[5].y = 40;
    CHECK(preview == app::make_job_layout_preview({{&without_rapid_jump, nullptr}}, &material));
    int cyan_cut_pixels = 0;
    for (int y = 0; y < preview.height(); ++y)
        for (int x = 0; x < preview.width(); ++x) {
            const QColor color = preview.pixelColor(x, y);
            if (color.red() < 60 && color.green() > 180 && color.blue() > 190)
                ++cyan_cut_pixels;
        }
    CHECK(cyan_cut_pixels > 80);
}

TEST_CASE("preview da OS remove miolos e conserva a peca em perfil passante") {
    cam::Toolpath exterior;
    exterior.kind = cam::ToolpathType::Profile;
    exterior.profile_side = cam::ProfileSide::Outside;
    exterior.moves = {{cam::MoveKind::Rapid, 40, 30, 5},
                      {cam::MoveKind::Plunge, 40, 30, -18},
                      {cam::MoveKind::Cut, 160, 30, -18},
                      {cam::MoveKind::Cut, 160, 100, -18},
                      {cam::MoveKind::Cut, 40, 100, -18},
                      {cam::MoveKind::Cut, 40, 30, -18},
                      {cam::MoveKind::Rapid, 40, 30, 5}};
    cam::Toolpath counter;
    counter.kind = cam::ToolpathType::Profile;
    counter.profile_side = cam::ProfileSide::Inside;
    counter.moves = {{cam::MoveKind::Rapid, 80, 50, 5},
                     {cam::MoveKind::Plunge, 80, 50, -18},
                     {cam::MoveKind::Cut, 120, 50, -18},
                     {cam::MoveKind::Cut, 120, 80, -18},
                     {cam::MoveKind::Cut, 80, 80, -18},
                     {cam::MoveKind::Cut, 80, 50, -18},
                     {cam::MoveKind::Rapid, 80, 50, 5}};
    cad::MaterialSetup material;
    material.width = 300;
    material.height = 200;
    material.thickness = 18;

    const QImage preview = app::make_job_layout_preview(
        {{&exterior, nullptr}, {&counter, nullptr}}, &material);
    REQUIRE_FALSE(preview.isNull());

    // Convert two known XY points through the same isometric projection. The
    // product remains wood; its through-cut counter reveals the dark viewport.
    constexpr double x_scale = .92, x_y = .28, y_scale = .92, y_y = -.28, z_scale = .9;
    const double raw_x_min = 0.0, raw_x_max = x_scale * 300 + y_scale * 200;
    const double raw_y_min = y_y * 200;
    const double raw_y_max = x_y * 300 + z_scale * 18;
    const double drawing_width = 944.0;
    const double drawing_height = preview.height() / 3.0 - 70.0 - 62.0 - 16.0 - 6.0;
    const double scale = std::min(drawing_width / (raw_x_max - raw_x_min),
                                  drawing_height / (raw_y_max - raw_y_min));
    const double raw_center_x = (raw_x_min + raw_x_max) / 2.0;
    const double raw_center_y = (raw_y_min + raw_y_max) / 2.0;
    const auto sample = [&](double x, double y) {
        const double raw_x = x_scale * x + y_scale * y;
        const double raw_y = x_y * x + y_y * y;
        const int px = qRound((18.0 + drawing_width / 2.0 + (raw_x - raw_center_x) * scale) * 3.0);
        const int py = qRound((70.0 + drawing_height / 2.0 + (raw_y - raw_center_y) * scale) * 3.0);
        return preview.pixelColor(px, py);
    };
    const QColor product = sample(60, 60);
    const QColor counter_color = sample(100, 65);
    CHECK(qGray(product.rgb()) > qGray(counter_color.rgb()) + 30);
}

TEST_CASE("ordem de serviço mantém muitos percursos na mesma folha horizontal") {
    std::vector<cam::Toolpath> paths;
    std::vector<cam::SetupSheetOperation> operations;
    constexpr int count = 48;
    paths.reserve(count);
    operations.reserve(count);
    for (int i = 0; i < count; ++i) {
        cam::Toolpath path;
        path.name = "Operação " + std::to_string(i + 1);
        path.tool_name = "Fresa 6 mm";
        path.tool_number = 1 + (i % 3);
        path.moves = {{cam::MoveKind::Rapid, 0, 0, 5},
                      {cam::MoveKind::Plunge, 20, 20, -1},
                      {cam::MoveKind::Cut, 80, 20, -1},
                      {cam::MoveKind::Cut, 80, 50, -1}};
        paths.push_back(std::move(path));
    }
    for (const cam::Toolpath& path : paths) operations.push_back({&path, nullptr});

    const std::string html = cam::job_setup_sheet_html(
        operations, cad::MaterialSetup{}, "Muitos percursos", cam::SetupSheetInfo{});
    CHECK(html.find("Operação 48") != std::string::npos);
    CHECK(html.find("page-break-before:always") == std::string::npos);

    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString capture = qEnvironmentVariable("CNC_JOB_SHEET_CAPTURE");
    const QString output_path = capture.isEmpty()
                                    ? temp.filePath("OS-48-percursos.pdf")
                                    : capture;
    cad::MaterialSetup material;
    material.width = 1220;
    material.height = 2440;
    material.thickness = 18;
    const QImage layout = app::make_job_layout_preview(operations, &material);
    REQUIRE_FALSE(layout.isNull());
    QString error;
    REQUIRE(cam::write_job_setup_sheet_pdf(output_path, operations, material,
                                           "Muitos percursos", cam::SetupSheetInfo{},
                                           layout, {}, {}, &error));
    QFile file(output_path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.count("/Type /Page\n") == 1);
    CHECK(bytes.contains("/MediaBox [0 0 842.000000 595.000000]"));
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
