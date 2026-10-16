#include "cam/job_sheet.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>

#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPainter>
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

}  // namespace

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

namespace {

// The sheet is laid out for A4 landscape: identification and machine setup stay
// compact on the left while the drawing gets most of the page width.
const char* kPage = R"HTML(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<title>Ordem de serviço - @@TITLE@@</title>
<style>
body { font-family: "Segoe UI", Arial, sans-serif; color: #172334; font-size: 8pt; margin: 0; padding: 0; }
.header { width:100%; border-collapse:collapse; border-bottom: 3px solid #1682d4; padding-bottom: 5px; margin-bottom: 5px; }
.logo { max-width: 125px; max-height: 38px; object-fit: contain; }
h1 { font-size: 19pt; margin: 0; color: #123b5a; line-height: 1.1; font-weight: bold; }
h2 { font-size: 9pt; color: #123b5a; margin: 6px 0 3px; border-bottom: 1px solid #b9c7d5; padding-bottom: 2px; font-weight: bold; }
.muted { color: #536579; font-size: 7pt; }
.stats { width:100%; border-collapse:collapse; margin:4px 0 6px; }
.stats td { width:25%; padding:6px 9px; border:1px solid #cbd5df; border-top:3px solid #1682d4; background:#f3f7fb; }
.stat-label { color:#52667a; font-size:6.3pt; text-transform:uppercase; font-weight:bold; }
.stat-value { color:#123b5a; font-size:10pt; font-weight:bold; }
.split { width:100%; border-collapse:collapse; }
.grid { width: 100%; border-collapse: collapse; margin-bottom: 2px; }
.grid td { padding: 4px 6px; border: 1px solid #cbd5df; vertical-align: top; }
.label { color: #52667a; font-size: 6pt; text-transform: uppercase; font-weight: bold; }
.value { font-weight: 600; font-size: 8pt; min-height: 10px; }
.material { background: #f2f6fa; }
.drawing { width:100%; border-collapse:collapse; margin:0; }
.drawing td { text-align:center; border:1px solid #cbd5df; background:#101923; padding:3px; }
.ops { width: 100%; border-collapse: collapse; table-layout:fixed; font-size: 6.1pt; margin-bottom: 2px; }
.ops td { width:50%; border-bottom:1px solid #cbd5df; padding:3px 4px; vertical-align:top; overflow-wrap:anywhere; }
.op-title { color:#123b5a; font-weight:bold; }
.op-change { color:#174d73; font-weight:bold; }
.total { font-size: 9pt; text-align: right; font-weight: 700; color: #123b5a; padding: 3px 0; }
.notes { min-height: 20px; white-space: pre-wrap; border: 1px solid #cbd5df; padding: 5px 7px; font-size: 7.5pt; background: #fafbfc; }
.sign { margin-top: 8px; width: 100%; }
.sign td { width: 50%; padding-top: 14px; text-align: center; font-size: 7.5pt; }
.sign span { display: inline-block; border-top: 1px solid #52667a; min-width: 75%; padding-top: 3px; font-weight: 600; }
.ophead { width:100%; border-collapse:collapse; border-bottom:3px solid #1682d4; padding-bottom:4px; margin-bottom:5px; }
.opnum { font-size:14pt; font-weight:bold; color:#123b5a; }
.opright { font-size:11pt; font-weight:bold; color:#123b5a; text-align:right; }
.opright-muted { color:#536579; font-size:7pt; text-align:right; }
thead { display: table-header-group; }
</style></head><body>
<table class="header">
<tr><td width="150" valign="middle">@@LOGO@@</td>
<td valign="middle"><div class="muted">UNY CV | PRODUÇÃO CNC</div>
<h1>ORDEM DE SERVIÇO</h1><div class="muted">Projeto: @@TITLE@@ · Emitida em @@NOW@@</div></td></tr></table>
@@STATS@@
<table class="split"><tr>
<td width="34%" valign="top" style="padding-right:8px;">
<h2>Cliente e identificação</h2>
<table class="grid"><tr><td><div class="label">Cliente</div><div class="value">@@CUSTOMER@@</div></td>
<td><div class="label">Contato</div><div class="value">@@CONTACT@@</div></td>
<td><div class="label">Ordem / pedido</div><div class="value">@@ORDER@@</div></td></tr>
<tr><td><div class="label">Peça</div><div class="value">@@PART@@</div></td>
<td><div class="label">Revisão</div><div class="value">@@REVISION@@</div></td>
<td><div class="label">Operador</div><div class="value">@@OPERATOR@@</div></td></tr></table>
<h2>Material e preparação da máquina</h2>
<table class="grid material"><tr><td><div class="label">Material informado</div><div class="value">@@MATERIAL@@</div></td>
<td><div class="label">Chapa X × Y × Z</div><div class="value">@@W@@ × @@H@@ × @@T@@ mm</div></td>
<td><div class="label">Área da chapa</div><div class="value">@@AREA_M2@@ m²</div></td></tr>
<tr><td><div class="label">Origem / Zero Z</div><div class="value">@@ZX@@ · @@ZY@@ / @@ZZ@@</div></td>
<td><div class="label">Altura segura Z</div><div class="value">@@CLEARANCE@@ mm</div></td>
<td><div class="label">Folga de mergulho</div><div class="value">@@PLUNGE_GAP@@ mm</div></td></tr>
<tr><td><div class="label">Origem adicional X / Y</div><div class="value">@@OFFSET_X@@ / @@OFFSET_Y@@ mm</div></td>
<td><div class="label">Margem do material</div><div class="value">@@MARGIN@@ mm</div></td>
<td><div class="label">Ponto inicial X / Y</div><div class="value">@@HOME_X@@ / @@HOME_Y@@ mm</div></td></tr>
<tr><td><div class="label">Envelope usinado X × Y</div><div class="value">@@CUT_W@@ × @@CUT_H@@ mm</div></td>
<td colspan="2"><div class="label">Limites do percurso</div><div class="value">X @@X0@@–@@X1@@ · Y @@Y0@@–@@Y1@@ mm</div></td></tr></table>
</td>
<td width="66%" valign="top">
<h2>Desenho geral do percurso e cotas</h2>
<table class="drawing"><tr><td>
<img src="job-layout.png" width="@@DRAW_W@@" height="@@DRAW_H@@" />
</td></tr></table>
<div class="muted">Envelope e cotas calculados sobre os movimentos de corte. Confirme medidas, origem e fixação na máquina antes de usinar.</div>
</td></tr></table>
<h2>Operações, ferramentas e parâmetros de corte</h2>
<table class="ops"><tbody>@@ROWS@@</tbody></table>
<div class="total">Comprimento de corte: @@CUT_TOTAL@@ m · Tempo estimado: @@TOTAL@@</div>
<div class="muted">Estimativa ideal com avanços programados e rápido assumido em 8000 mm/min; não inclui aceleração, preparação, fixação ou trocas manuais.</div>
<h2>Observações da ordem de serviço</h2><div class="notes">@@NOTES@@</div>
<table class="sign"><tr><td width="50%"><span>Responsável pela programação</span></td>
<td width="50%"><span>Conferência / produção</span></td></tr></table>
@@OP_SHEETS@@
</body></html>)HTML";

const char* kRow = R"HTML(<td><span class="op-title">@@INDEX@@ · @@NAME@@</span> ·
T@@TOOL_NUMBER@@ @@TOOL_NAME@@ Ø@@DIAMETER@@mm · @@RPM@@rpm · XY/Z @@FEED@@/@@PLUNGE@@mm/min ·
Z @@Z0@@…@@Z1@@mm · passos @@PASS_DEPTH@@/@@STEPOVER@@mm · @@CUT@@m · @@BRIDGES@@ · @@TIME@@</td>)HTML";

// Everything the sheet needs about one toolpath, collected once and reused by
// the summary table and by that toolpath's own sheet.
struct OperationFacts {
    std::string name;
    std::string tool_name;
    int tool_number = 0;
    int rpm = 0;
    double feed = 0.0;
    double plunge = 0.0;
    double diameter = 0.0;
    double pass_depth = 0.0;
    double stepover = 0.0;
    double z_min = 0.0;
    double z_max = 0.0;
    double cut_mm = 0.0;
    double seconds = 0.0;
    std::string bridges;
    std::vector<std::string> notes;
};

void apply_operation_fields(std::string& text, const OperationFacts& facts) {
    replace_all(text, "@@NAME@@", html_escape(facts.name));
    replace_all(text, "@@TOOL_NUMBER@@", std::to_string(facts.tool_number));
    replace_all(text, "@@TOOL_NAME@@", html_escape(facts.tool_name));
    replace_all(text, "@@DIAMETER@@", std::format("{:.3f}", facts.diameter));
    replace_all(text, "@@RPM@@", std::to_string(facts.rpm));
    replace_all(text, "@@FEED@@", std::format("{:g}", facts.feed));
    replace_all(text, "@@PLUNGE@@", std::format("{:g}", facts.plunge));
    replace_all(text, "@@Z0@@", std::format("{:.2f}", facts.z_min));
    replace_all(text, "@@Z1@@", std::format("{:.2f}", facts.z_max));
    replace_all(text, "@@PASS_DEPTH@@", std::format("{:g}", facts.pass_depth));
    replace_all(text, "@@STEPOVER@@", std::format("{:g}", facts.stepover));
    replace_all(text, "@@CUT@@", std::format("{:.2f}", facts.cut_mm / 1000.0));
    replace_all(text, "@@BRIDGES@@", html_escape(facts.bridges));
    replace_all(text, "@@TIME@@", std::format("{:.1f} min", facts.seconds / 60.0));
}

}  // namespace

std::string job_setup_sheet_html(const std::vector<SetupSheetOperation>& operations,
                                const cad::MaterialSetup& material,
                                const std::string& job_name,
                                const SetupSheetInfo& info,
                                const JobSheetDrawing& drawing) {
    std::vector<OperationFacts> facts;
    std::optional<BBox> envelope;
    double total_seconds = 0.0;
    double total_cut_mm = 0.0;

    for (const SetupSheetOperation& operation : operations) {
        const Toolpath* path = operation.path;
        if (!path) continue;
        OperationFacts entry;
        entry.seconds = path->estimated_seconds();
        total_seconds += entry.seconds;
        const auto [cut_length, rapid_length] = path->lengths();
        (void)rapid_length;
        entry.cut_mm = cut_length;
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
        entry.z_min = cutting_z ? cutting_z->first : fallback_z_min;
        entry.z_max = cutting_z ? cutting_z->second : fallback_z_max;

        const Tool* tool = operation.job ? &operation.job->tool : nullptr;
        entry.name = path->name;
        entry.tool_name = path->tool_name;
        entry.tool_number = path->tool_number;
        entry.rpm = path->spindle_speed;
        entry.feed = path->feed_rate;
        entry.plunge = path->plunge_rate;
        entry.diameter = tool ? tool->diameter : 0.0;
        entry.pass_depth = tool ? tool->pass_depth : 0.0;
        entry.stepover = tool ? tool->stepover : 0.0;
        entry.bridges = bridge_summary(operation.job);
        entry.notes = path->notes;
        facts.push_back(std::move(entry));
    }

    std::vector<std::string> cards;
    cards.reserve(facts.size());
    std::optional<int> previous_tool;
    for (size_t i = 0; i < facts.size(); ++i) {
        std::string card = kRow;
        replace_all(card, "@@INDEX@@", std::to_string(i + 1));
        apply_operation_fields(card, facts[i]);
        if (previous_tool && *previous_tool != facts[i].tool_number) {
            const std::string title = "<div class=\"op-title\">";
            const size_t title_at = card.find(title);
            if (title_at != std::string::npos)
                card.insert(title_at, "<div class=\"op-change\">Troca de ferramenta</div>");
        }
        previous_tool = facts[i].tool_number;
        const size_t cell_end = card.rfind("</td>");
        if (cell_end != std::string::npos)
            for (const std::string& note : facts[i].notes)
                card.insert(cell_end, std::format("<br><span class=\"muted\">Aviso: {}</span>",
                                                  html_escape(note)));
        cards.push_back(std::move(card));
    }

    const size_t columns = cards.size() > 3 ? 2 : 1;
    std::string rows;
    for (size_t i = 0; i < cards.size(); i += columns) {
        rows += "<tr>";
        for (size_t column = 0; column < columns; ++column) {
            const size_t index = i + column;
            if (index < cards.size()) {
                std::string card = cards[index];
                const size_t cell = card.find("<td>");
                if (cell != std::string::npos)
                    card.replace(cell, 4, std::format("<td style=\"width:{:.3f}%\">",
                                                      100.0 / columns));
                rows += card;
            } else {
                rows += std::format("<td style=\"width:{:.3f}%\"></td>", 100.0 / columns);
            }
        }
        rows += "</tr>";
    }

    const double cut_width = envelope ? std::max(0.0, envelope->x1 - envelope->x0) : 0.0;
    const double cut_height = envelope ? std::max(0.0, envelope->y1 - envelope->y0) : 0.0;
    const std::string title = html_escape(job_name.empty() ? "Sem título" : job_name);
    const double area_m2_val = (material.width * material.height) / 1000000.0;
    const std::string area_m2_str = !info.area_m2.empty() ? info.area_m2 : std::format("{:.3f}", area_m2_val);
    const std::string material_label =
        html_escape(!info.material_type.empty() ? info.material_type : material.material_profile);
    const int total_minutes = static_cast<int>(total_seconds / 60.0);
    const int total_seconds_remainder = static_cast<int>(total_seconds) % 60;
    const std::string total_label = std::format("{} min {:02} s", total_minutes, total_seconds_remainder);

    const std::string stats = std::format(
        "<table class=\"stats\"><tr>"
        "<td width=\"22%\"><div class=\"stat-label\">Área da chapa</div><div class=\"stat-value\">{} m²</div></td>"
        "<td width=\"34%\"><div class=\"stat-label\">Material / dimensões</div><div class=\"stat-value\">{} · {} × {} × {} mm</div></td>"
        "<td width=\"22%\"><div class=\"stat-label\">Tempo estimado</div><div class=\"stat-value\">{}</div></td>"
        "<td width=\"22%\"><div class=\"stat-label\">Operações / corte</div><div class=\"stat-value\">{} · {:.2f} m</div></td>"
        "</tr></table>",
        area_m2_str, material_label,
        std::format("{:g}", material.width), std::format("{:g}", material.height),
        std::format("{:g}", material.thickness), total_label,
        facts.size(), total_cut_mm / 1000.0);

    std::string page = kPage;
    replace_all(page, "@@STATS@@", stats);
    replace_all(page, "@@OP_SHEETS@@", "");
    replace_all(page, "@@DRAW_W@@", std::to_string(drawing.summary_width));
    replace_all(page, "@@DRAW_H@@", std::to_string(drawing.summary_height));
    replace_all(page, "@@TITLE@@", title);
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
    replace_all(page, "@@AREA_M2@@", area_m2_str);
    replace_all(page, "@@MATERIAL@@", material_label);
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
    replace_all(page, "@@TOTAL@@", total_label);
    replace_all(page, "@@NOTES@@", html_escape(info.notes));
    return page;
}

namespace {

// A4 landscape minus the writer's margins, in the writer's own device pixels.
QSizeF sheet_page_size(const QPdfWriter& pdf) {
    return pdf.pageLayout().paintRectPixels(pdf.resolution()).size();
}

void configure_writer(QPdfWriter& pdf) {
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setResolution(144);
    pdf.setPageMargins(QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
    pdf.setCreator("CNC-CAM C++ / UNY CV");
}

// Qt reads an <img> width/height as 96-dpi units and scales them by the layout
// device's DPI, so the sizes written into the HTML have to be divided back down
// or the drawing overflows the page and drags the whole sheet onto a second one.
constexpr double kHtmlImageDpi = 96.0;

// Fits an image into `box_width` device pixels without distorting it, capped at
// `max_height`, and returns the size to write into the HTML.
QSize fit_drawing(const QImage& image, int box_width, int max_height, double resolution) {
    const double aspect = (image.isNull() || image.width() <= 0 || image.height() <= 0)
                              ? 1.0 / 3.0
                              : static_cast<double>(image.height()) / image.width();
    double width = box_width;
    double height = width * aspect;
    if (height > max_height) {
        height = max_height;
        width = height / aspect;
    }
    const double to_html = kHtmlImageDpi / resolution;
    return QSize(qRound(width * to_html), qRound(height * to_html));
}

JobSheetDrawing drawing_metrics(const QPdfWriter& pdf,
                                const QImage& summary_preview,
                                const std::vector<QImage>& operation_previews) {
    const QSizeF page = sheet_page_size(pdf);
    JobSheetDrawing drawing;
    // The summary drawing shares the page with the setup tables, but the right
    // column is deliberately wider so the overall toolpath is easy to inspect.
    const double resolution = pdf.resolution();
    const QSize summary = fit_drawing(summary_preview, static_cast<int>(page.width() * 0.63),
                                      static_cast<int>(page.height() * 0.50), resolution);
    drawing.summary_width = summary.width();
    drawing.summary_height = summary.height();
    // A per-toolpath sheet carries only its drawing and its parameters, so the
    // drawing takes the whole width and most of the height.
    const QImage& first = operation_previews.empty() ? summary_preview : operation_previews.front();
    const QSize operation = fit_drawing(first, static_cast<int>(page.width()) - 16,
                                        static_cast<int>(page.height() * 0.73), resolution);
    drawing.operation_width = operation.width();
    drawing.operation_height = operation.height();
    drawing.operation_sheets = static_cast<int>(operation_previews.size());
    return drawing;
}

void prepare_document(QTextDocument& document,
                      QPdfWriter& pdf,
                      const std::string& html,
                      const QImage& layout_preview,
                      const std::vector<QImage>& operation_previews,
                      const QImage& logo) {
    // Laying out against the writer keeps the document's own DPI equal to the
    // PDF's, so the page size below maps 1:1 and the sheet fills the paper
    // instead of being scaled into a corner of it.
    document.documentLayout()->setPaintDevice(&pdf);
    document.setDocumentMargin(0);
    // Lay out the complete OS as one tall page. draw_job_sheet_page() scales
    // that finished layout onto the single landscape sheet after measuring it.
    const QSizeF page = sheet_page_size(pdf);
    document.setPageSize(QSizeF(page.width(), 1000000.0));
    document.addResource(QTextDocument::ImageResource, QUrl("job-layout.png"), layout_preview);
    for (size_t i = 0; i < operation_previews.size(); ++i)
        document.addResource(QTextDocument::ImageResource,
                             QUrl(QString("job-op-%1.png").arg(i)), operation_previews[i]);
    if (!logo.isNull()) document.addResource(QTextDocument::ImageResource, QUrl("job-logo"), logo);
    document.setHtml(QString::fromUtf8(html.data(), static_cast<qsizetype>(html.size())));
}

void draw_job_sheet_page(QTextDocument& document, QPdfWriter& pdf) {
    const QSizeF page = sheet_page_size(pdf);
    const QSizeF content = document.documentLayout()->documentSize();
    const double scale = std::min({1.0, page.width() / std::max(1.0, content.width()),
                                   page.height() / std::max(1.0, content.height())});
    QPainter painter(&pdf);
    painter.scale(scale, scale);
    document.drawContents(&painter, QRectF(0.0, 0.0, content.width(), content.height()));
    painter.end();
}

bool verify_written(const QString& path, QString* error) {
    if (!QFileInfo::exists(path) || QFileInfo(path).size() < 5) {
        if (error) *error = "Não foi possível gravar um PDF válido no caminho selecionado.";
        return false;
    }
    if (error) error->clear();
    return true;
}

}  // namespace

bool write_job_setup_sheet_pdf(const QString& path,
                               const std::vector<SetupSheetOperation>& operations,
                               const cad::MaterialSetup& material,
                               const std::string& job_name,
                               const SetupSheetInfo& info,
                               const QImage& layout_preview,
                               const QImage& logo,
                               const std::vector<QImage>& operation_previews,
                               QString* error) {
    QPdfWriter pdf(path);
    configure_writer(pdf);
    pdf.setTitle(QString("Ordem de serviço - %1").arg(QString::fromStdString(info.part_name)));

    const JobSheetDrawing drawing = drawing_metrics(pdf, layout_preview, operation_previews);
    const std::string html = job_setup_sheet_html(operations, material, job_name, info, drawing);

    QTextDocument document;
    prepare_document(document, pdf, html, layout_preview, operation_previews, logo);
    draw_job_sheet_page(document, pdf);

    return verify_written(path, error);
}

bool write_multi_job_setup_sheet_pdf(const QString& path,
                                     const std::vector<MultiJobSheetEntry>& entries,
                                     const QImage& logo,
                                     QString* error) {
    if (entries.empty()) {
        if (error) *error = "Nenhuma ordem de serviço acumulada para exportar.";
        return false;
    }

    QPdfWriter pdf(path);
    configure_writer(pdf);
    pdf.setTitle("Ordens de Serviço CNC - UNY CV");

    // The production OS is intentionally a one-page document. Accumulated
    // export uses the first OS record only; never append pages for history.
    const auto& entry = entries.front();
    JobSheetDrawing drawing = drawing_metrics(pdf, entry.layout_preview, entry.operation_previews);
    SetupSheetInfo info = entry.info;
    if (!logo.isNull()) info.logo_source = "job-logo";
    std::string html = job_setup_sheet_html(entry.operations, entry.material, entry.job_name,
                                            info, drawing);
    QTextDocument document;
    prepare_document(document, pdf, html, entry.layout_preview,
                     entry.operation_previews, logo);
    draw_job_sheet_page(document, pdf);

    return verify_written(path, error);
}

}  // namespace cnc::cam
