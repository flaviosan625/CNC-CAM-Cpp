#include "cam/job_sheet.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>

#include <QDateTime>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QUrl>

#include "cam/profile.hpp"
#include "cam/setup_sheet.hpp"

namespace cnc::cam {
namespace {

void replace_all(std::string& text, const std::string& marker, const std::string& value) {
    size_t position = 0;
    while ((position = text.find(marker, position)) != std::string::npos) {
        text.replace(position, marker.size(), value);
        position += value.size();
    }
}

std::string zero_label(const std::string& value, const char* left, const char* center, const char* right) {
    if (value == "center") return center;
    if (value == "right" || value == "top") return right;
    return left;
}

std::string bridge_summary(const ToolpathJob* job) {
    if (!job) return "—";
    const auto* profile = std::get_if<ProfileSettings>(&job->settings);
    if (!profile || !profile->tabs) return "—";
    const std::string automatic = profile->tab_manual_only
                                      ? "0"
                                      : (profile->tab_spacing > 0.0
                                             ? std::format("≥ {}", profile->tab_min_count)
                                             : std::to_string(profile->tab_count));
    return std::format("{} auto + {} manuais ({})", automatic, profile->manual_tabs.size(),
                       profile->tabs_3d ? "3D" : "2D");
}

const char* kPage = R"HTML(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<title>Ordem de serviço - @@TITLE@@</title>
<style>
body { font-family: "Segoe UI", Arial, sans-serif; color: #172334; font-size: 8.5pt; }
.header { display: flex; align-items: center; border-bottom: 3px solid #1682d4; padding-bottom: 8px; }
.logo { max-width: 155px; max-height: 52px; object-fit: contain; margin-right: 18px; }
h1 { font-size: 18pt; margin: 0; color: #123b5a; }
h2 { font-size: 10.5pt; color: #123b5a; margin: 10px 0 4px; border-bottom: 1px solid #b9c7d5; padding-bottom: 3px; }
.muted { color: #536579; font-size: 7.5pt; }
.grid { width: 100%; border-collapse: collapse; }
.grid td { padding: 3px 5px; border: 1px solid #d3dce5; vertical-align: top; }
.label { color: #52667a; font-size: 6.5pt; text-transform: uppercase; }
.value { font-weight: 600; min-height: 10px; }
.material { background: #f2f6fa; }
.layout { width: 100%; height: 210px; object-fit: contain; border: 1px solid #d3dce5; }
.ops { width: 100%; border-collapse: collapse; font-size: 6.8pt; }
.ops th { color: #fff; background: #174d73; padding: 4px 3px; text-align: left; }
.ops td { border-bottom: 1px solid #cbd5df; padding: 4px 3px; }
.total { font-size: 10pt; text-align: right; font-weight: 700; color: #123b5a; padding: 6px; }
.notes { min-height: 30px; white-space: pre-wrap; border: 1px solid #d3dce5; padding: 5px; }
.sign { margin-top: 20px; width: 100%; }
.sign td { width: 50%; padding-top: 15px; text-align: center; }
.sign span { display: inline-block; border-top: 1px solid #52667a; min-width: 80%; padding-top: 4px; }
thead { display: table-header-group; }
</style></head><body>
<div class="header">@@LOGO@@<div><div class="muted">UNY CV | PRODUÇÃO CNC</div>
<h1>ORDEM DE SERVIÇO</h1><div class="muted">Projeto: @@TITLE@@ · Emitida em @@NOW@@</div></div></div>
<h2>Cliente e identificação</h2>
<table class="grid"><tr><td><div class="label">Cliente</div><div class="value">@@CUSTOMER@@</div></td>
<td><div class="label">Contato</div><div class="value">@@CONTACT@@</div></td>
<td><div class="label">Ordem / pedido</div><div class="value">@@ORDER@@</div></td></tr>
<tr><td><div class="label">Peça</div><div class="value">@@PART@@</div></td>
<td><div class="label">Revisão</div><div class="value">@@REVISION@@</div></td>
<td><div class="label">Operador</div><div class="value">@@OPERATOR@@</div></td></tr></table>
<h2>Material e preparação da máquina</h2>
<table class="grid material"><tr><td><div class="label">Material / perfil</div><div class="value">@@MATERIAL@@</div></td>
<td><div class="label">Chapa X × Y × Z</div><div class="value">@@W@@ × @@H@@ × @@T@@ mm</div></td>
<td><div class="label">Origem XY</div><div class="value">@@ZX@@ · @@ZY@@</div></td>
<td><div class="label">Zero Z</div><div class="value">@@ZZ@@</div></td></tr>
<tr><td><div class="label">Altura segura Z</div><div class="value">@@CLEARANCE@@ mm</div></td>
<td><div class="label">Folga de mergulho</div><div class="value">@@PLUNGE_GAP@@ mm</div></td>
<td><div class="label">Origem adicional X / Y</div><div class="value">@@OFFSET_X@@ / @@OFFSET_Y@@ mm</div></td>
<td><div class="label">Margem do material</div><div class="value">@@MARGIN@@ mm</div></td></tr>
<tr><td><div class="label">Ponto inicial X / Y</div><div class="value">@@HOME_X@@ / @@HOME_Y@@ mm</div></td>
<td><div class="label">Envelope usinado X × Y</div><div class="value">@@CUT_W@@ × @@CUT_H@@ mm</div></td>
<td colspan="2"><div class="label">Limites do percurso</div><div class="value">X @@X0@@–@@X1@@ · Y @@Y0@@–@@Y1@@ mm</div></td></tr></table>
<h2>Desenho do percurso e cotas</h2>
<img class="layout" src="job-layout.png" />
<div class="muted">Envelope e cotas calculados sobre os movimentos de corte. Confirme medidas, origem e fixação na máquina antes de usinar.</div>
<h2>Operações, ferramentas e parâmetros de corte</h2>
<table class="ops"><thead><tr><th># / operação</th><th>Fresa</th><th>RPM</th><th>Avanço / mergulho</th>
<th>Faixa Z de corte</th><th>Passo Z / lateral</th><th>Corte</th><th>Pontes</th><th>Tempo</th></tr></thead>
<tbody>@@ROWS@@</tbody></table>
<div class="total">Comprimento de corte: @@CUT_TOTAL@@ m · Tempo estimado: @@TOTAL@@</div>
<div class="muted">Estimativa ideal com avanços programados e rápido assumido em 8000 mm/min; não inclui aceleração, preparação, fixação ou trocas manuais.</div>
<h2>Observações da ordem de serviço</h2><div class="notes">@@NOTES@@</div>
<table class="sign"><tr><td><span>Responsável pela programação</span></td><td><span>Conferência / produção</span></td></tr></table>
</body></html>)HTML";

const char* kRow = R"HTML(<tr><td>@@INDEX@@ · @@NAME@@</td><td>T@@TOOL_NUMBER@@ · @@TOOL_NAME@@<br>Ø @@DIAMETER@@ mm</td>
<td>@@RPM@@</td><td>@@FEED@@ / @@PLUNGE@@ mm/min</td><td>@@Z0@@ / @@Z1@@ mm</td>
<td>@@PASS_DEPTH@@ / @@STEPOVER@@ mm</td><td>@@CUT@@ m</td><td>@@BRIDGES@@</td><td>@@TIME@@</td></tr>)HTML";

}  // namespace

std::string job_setup_sheet_html(const std::vector<SetupSheetOperation>& operations,
                                const cad::MaterialSetup& material,
                                const std::string& job_name,
                                const SetupSheetInfo& info) {
    std::string rows;
    std::optional<BBox> envelope;
    double total_seconds = 0.0;
    double total_cut_mm = 0.0;
    std::optional<int> previous_tool;
    int index = 1;

    for (const SetupSheetOperation& operation : operations) {
        const Toolpath* path = operation.path;
        if (!path) continue;
        const double seconds = path->estimated_seconds();
        total_seconds += seconds;
        const auto [cut_length, rapid_length] = path->lengths();
        (void)rapid_length;
        total_cut_mm += cut_length;
        std::optional<std::pair<double, double>> cutting_z;
        for (const Move& move : path->moves) {
            if (!move.cutting()) continue;
            if (!cutting_z) cutting_z = std::pair{move.z, move.z};
            else {
                cutting_z->first = std::min(cutting_z->first, move.z);
                cutting_z->second = std::max(cutting_z->second, move.z);
            }
            if (!envelope) envelope = BBox{move.x, move.y, move.x, move.y};
            else {
                envelope->x0 = std::min(envelope->x0, move.x);
                envelope->y0 = std::min(envelope->y0, move.y);
                envelope->x1 = std::max(envelope->x1, move.x);
                envelope->y1 = std::max(envelope->y1, move.y);
            }
        }
        const auto [fallback_z_min, fallback_z_max] = path->depth_range();
        const double z_min = cutting_z ? cutting_z->first : fallback_z_min;
        const double z_max = cutting_z ? cutting_z->second : fallback_z_max;

        const Tool* tool = operation.job ? &operation.job->tool : nullptr;
        const double diameter = tool ? tool->diameter : 0.0;
        const double pass_depth = tool ? tool->pass_depth : 0.0;
        const double stepover = tool ? tool->stepover : 0.0;
        std::string row = kRow;
        replace_all(row, "@@INDEX@@", std::to_string(index++));
        replace_all(row, "@@NAME@@", html_escape(path->name));
        replace_all(row, "@@TOOL_NUMBER@@", std::to_string(path->tool_number));
        replace_all(row, "@@TOOL_NAME@@", html_escape(path->tool_name));
        replace_all(row, "@@DIAMETER@@", std::format("{:.3f}", diameter));
        replace_all(row, "@@RPM@@", std::to_string(path->spindle_speed));
        replace_all(row, "@@FEED@@", std::format("{:g}", path->feed_rate));
        replace_all(row, "@@PLUNGE@@", std::format("{:g}", path->plunge_rate));
        replace_all(row, "@@Z0@@", std::format("{:.2f}", z_min));
        replace_all(row, "@@Z1@@", std::format("{:.2f}", z_max));
        replace_all(row, "@@PASS_DEPTH@@", std::format("{:g}", pass_depth));
        replace_all(row, "@@STEPOVER@@", std::format("{:g}", stepover));
        replace_all(row, "@@CUT@@", std::format("{:.2f}", cut_length / 1000.0));
        replace_all(row, "@@BRIDGES@@", html_escape(bridge_summary(operation.job)));
        replace_all(row, "@@TIME@@", std::format("{:.1f} min", seconds / 60.0));
        if (previous_tool && *previous_tool != path->tool_number)
            row.insert(4, "<strong>Troca de ferramenta</strong><br>");
        previous_tool = path->tool_number;
        rows += row;
        for (const std::string& note : path->notes)
            rows += std::format("<tr><td colspan=\"9\" class=\"muted\">Aviso: {}</td></tr>",
                                html_escape(note));
    }

    const double cut_width = envelope ? std::max(0.0, envelope->x1 - envelope->x0) : 0.0;
    const double cut_height = envelope ? std::max(0.0, envelope->y1 - envelope->y0) : 0.0;
    std::string page = kPage;
    replace_all(page, "@@TITLE@@", html_escape(job_name.empty() ? "Sem título" : job_name));
    replace_all(page, "@@NOW@@", QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm").toStdString());
    replace_all(page, "@@LOGO@@", info.logo_source.empty()
                                       ? std::string("<strong class=\"logo\">UNY CV</strong>")
                                       : std::format("<img class=\"logo\" src=\"{}\" />",
                                                     html_escape(info.logo_source)));
    replace_all(page, "@@CUSTOMER@@", html_escape(info.customer));
    replace_all(page, "@@CONTACT@@", html_escape(info.contact));
    replace_all(page, "@@ORDER@@", html_escape(info.order_number));
    replace_all(page, "@@PART@@", html_escape(info.part_name));
    replace_all(page, "@@REVISION@@", html_escape(info.revision));
    replace_all(page, "@@OPERATOR@@", html_escape(info.operator_name));
    replace_all(page, "@@MATERIAL@@", html_escape(material.material_profile));
    replace_all(page, "@@W@@", std::format("{:g}", material.width));
    replace_all(page, "@@H@@", std::format("{:g}", material.height));
    replace_all(page, "@@T@@", std::format("{:g}", material.thickness));
    replace_all(page, "@@ZX@@", zero_label(material.zero_x, "Esquerda", "Centro", "Direita"));
    replace_all(page, "@@ZY@@", zero_label(material.zero_y, "Inferior", "Centro", "Superior"));
    replace_all(page, "@@ZZ@@", material.zero_z == "top" ? "Face superior" : "Face inferior");
    replace_all(page, "@@CLEARANCE@@", std::format("{:g}", material.clearance_z));
    replace_all(page, "@@PLUNGE_GAP@@", std::format("{:g}", material.plunge_gap));
    replace_all(page, "@@OFFSET_X@@", std::format("{:g}", material.use_origin_offset ? material.origin_offset_x : 0.0));
    replace_all(page, "@@OFFSET_Y@@", std::format("{:g}", material.use_origin_offset ? material.origin_offset_y : 0.0));
    replace_all(page, "@@MARGIN@@", std::format("{:g}", material.margin));
    replace_all(page, "@@HOME_X@@", std::format("{:g}", material.home_x));
    replace_all(page, "@@HOME_Y@@", std::format("{:g}", material.home_y));
    replace_all(page, "@@CUT_W@@", std::format("{:.2f}", cut_width));
    replace_all(page, "@@CUT_H@@", std::format("{:.2f}", cut_height));
    replace_all(page, "@@X0@@", envelope ? std::format("{:.2f}", envelope->x0) : "—");
    replace_all(page, "@@X1@@", envelope ? std::format("{:.2f}", envelope->x1) : "—");
    replace_all(page, "@@Y0@@", envelope ? std::format("{:.2f}", envelope->y0) : "—");
    replace_all(page, "@@Y1@@", envelope ? std::format("{:.2f}", envelope->y1) : "—");
    replace_all(page, "@@ROWS@@", rows);
    replace_all(page, "@@CUT_TOTAL@@", std::format("{:.2f}", total_cut_mm / 1000.0));
    const int total_minutes = static_cast<int>(total_seconds / 60.0);
    const int total_seconds_remainder = static_cast<int>(total_seconds) % 60;
    replace_all(page, "@@TOTAL@@", std::format("{} min {:02} s", total_minutes, total_seconds_remainder));
    replace_all(page, "@@NOTES@@", html_escape(info.notes));
    return page;
}

bool write_job_setup_sheet_pdf(const QString& path,
                               const std::vector<SetupSheetOperation>& operations,
                               const cad::MaterialSetup& material,
                               const std::string& job_name,
                               const SetupSheetInfo& info,
                               const QImage& layout_preview,
                               const QImage& logo,
                               QString* error) {
    QTextDocument document;
    document.addResource(QTextDocument::ImageResource, QUrl("job-layout.png"), layout_preview);
    if (!logo.isNull()) document.addResource(QTextDocument::ImageResource, QUrl("job-logo"), logo);
    const std::string html = job_setup_sheet_html(operations, material, job_name, info);
    document.setHtml(QString::fromUtf8(html.data(), static_cast<qsizetype>(html.size())));

    {
        QPdfWriter pdf(path);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setResolution(144);
        pdf.setPageMargins(QMarginsF(13, 13, 13, 13), QPageLayout::Millimeter);
        pdf.setTitle(QString("Ordem de serviço - %1").arg(QString::fromStdString(info.part_name)));
        pdf.setCreator("CNC-CAM C++ / UNY CV");
        document.setDocumentMargin(4);
        document.setPageSize(pdf.pageLayout().paintRect(QPageLayout::Point).size());
        document.print(&pdf);
    }

    if (!QFileInfo::exists(path) || QFileInfo(path).size() < 5) {
        if (error) *error = "Não foi possível gravar um PDF válido no caminho selecionado.";
        return false;
    }
    if (error) error->clear();
    return true;
}

}  // namespace cnc::cam
