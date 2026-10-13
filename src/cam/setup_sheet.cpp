// Folha de trabalho em HTML (porte de src/cam/setup_sheet.py). O texto da pagina e da linha da
// tabela e o do Python, copiado por script; so os marcadores @@X@@ sao trocados.
#include "cam/setup_sheet.hpp"

#include <format>

#include <QDateTime>

namespace cnc::cam {

namespace {

const std::string kPage = R"HTMLPAGE(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<title>Folha de trabalho — @@TITLE@@</title>
<style>
body {
    font-family: -apple-system, "Segoe UI", sans-serif;
    margin: 2em auto; max-width: 900px; color: #e8edf3;
    background: #0d1520;
}
@media print {
    body { color: #1a1a1a; background: #fff; }
    .change { color: #c00 !important; }
}
h1 { font-size: 1.4em; border-bottom: 2px solid #2589f5; padding-bottom: .3em; }
.meta { color: #8ea3bd; margin-bottom: 1.5em; }
table { width: 100%; border-collapse: collapse; font-size: .9em; }
th { background: #162236; text-align: left; padding: 8px 10px;
     border-bottom: 2px solid #2589f5; }
td { padding: 6px 10px; border-bottom: 1px solid #1e3048; }
tr:hover td { background: #162236; }
.change { color: #ffb347; font-weight: 600; display: block; font-size: .85em; }
.note { color: #ffb347; font-size: .85em; }
.total { font-size: 1.2em; margin-top: 1em; text-align: right;
          color: #2589f5; font-weight: 700; }
@media print {
    th { background: #f0f0f0; -webkit-print-color-adjust: exact; }
    td { border-bottom-color: #ccc; }
}
</style>
</head>
<body>
<h1>Folha de trabalho — @@TITLE@@</h1>
<div class="meta">
  @@NOW@@ &nbsp;·&nbsp;
  Material: @@W@@ × @@H@@ × @@T@@ mm
  &nbsp;·&nbsp;
  Zero XY: @@ZX@@ / @@ZY@@
  &nbsp;·&nbsp;
  Zero Z: @@ZZ@@
</div>
<table>
<thead>
<tr>
  <th>#</th><th>Percurso</th><th>Ferramenta</th><th>RPM</th>
  <th>Avanço / Mergulho</th><th>Z</th><th>Corte</th><th>Tempo</th>
</tr>
</thead>
<tbody>
@@ROWS@@
</tbody>
</table>
<div class="total">Tempo total estimado: @@TOTAL@@ min</div>
</body>
</html>
)HTMLPAGE";
const std::string kRow = R"HTMLROW(<tr>
<td>@@I@@</td>
<td>@@NAME@@@@CHANGE@@</td>
<td>T@@TN@@ — @@TNAME@@</td>
<td>@@RPM@@</td>
<td>@@FEED@@ / @@PLUNGE@@</td>
<td>@@Z1@@ → @@Z0@@</td>
<td>@@CUT@@ m</td>
<td>@@SEC@@ min</td>
</tr>)HTMLROW";

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

}  // namespace

std::string html_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string setup_sheet_html(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                             const std::string& job_name) {
    const std::string title = job_name.empty() ? std::string("Sem título") : job_name;
    const std::string now = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm").toStdString();

    std::vector<std::string> rows;
    double total_seconds = 0.0;
    std::optional<int> prev_tool;
    int i = 1;
    for (const Toolpath* tp : toolpaths) {
        const double seconds = tp->estimated_seconds();
        total_seconds += seconds;
        const auto [cut, rapid] = tp->lengths();
        (void)rapid;
        const auto [z0, z1] = tp->depth_range();
        std::string notes_html;
        for (const std::string& n : tp->notes) notes_html += "<div class=\"note\">⚠ " + html_escape(n) + "</div>";

        std::string tool_change;
        if (prev_tool && tp->tool_number != *prev_tool) tool_change = "<span class=\"change\">🔧 TROCAR FERRAMENTA</span>";
        prev_tool = tp->tool_number;

        std::string row = kRow;
        replace_all(row, "@@I@@", std::to_string(i++));
        replace_all(row, "@@NAME@@", html_escape(tp->name));
        replace_all(row, "@@CHANGE@@", tool_change);
        replace_all(row, "@@TN@@", std::to_string(tp->tool_number));
        replace_all(row, "@@TNAME@@", html_escape(tp->tool_name));
        replace_all(row, "@@RPM@@", std::to_string(tp->spindle_speed));
        replace_all(row, "@@FEED@@", std::format("{:g}", tp->feed_rate));
        replace_all(row, "@@PLUNGE@@", std::format("{:g}", tp->plunge_rate));
        replace_all(row, "@@Z1@@", std::format("{:.2f}", z1));
        replace_all(row, "@@Z0@@", std::format("{:.2f}", z0));
        replace_all(row, "@@CUT@@", std::format("{:.2f}", cut / 1000));
        replace_all(row, "@@SEC@@", std::format("{:.1f}", seconds / 60));
        rows.push_back(row);
        if (!notes_html.empty()) rows.push_back("<tr><td></td><td colspan=\"7\">" + notes_html + "</td></tr>");
    }

    std::string table_body;
    for (size_t k = 0; k < rows.size(); ++k) {
        if (k) table_body += "\n";
        table_body += rows[k];
    }

    std::string page = kPage;
    replace_all(page, "@@TITLE@@", html_escape(title));
    replace_all(page, "@@NOW@@", now);
    replace_all(page, "@@W@@", std::format("{:g}", material.width));
    replace_all(page, "@@H@@", std::format("{:g}", material.height));
    replace_all(page, "@@T@@", std::format("{:g}", material.thickness));
    replace_all(page, "@@ZX@@", html_escape(material.zero_x));
    replace_all(page, "@@ZY@@", html_escape(material.zero_y));
    replace_all(page, "@@ZZ@@", material.zero_z == "top" ? "face superior" : "base");
    replace_all(page, "@@ROWS@@", table_body);
    replace_all(page, "@@TOTAL@@", std::format("{:.0f}", total_seconds / 60));
    return page;
}

std::string setup_sheet_html(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                             const std::string& job_name) {
    std::vector<const Toolpath*> ptrs;
    for (const Toolpath& t : toolpaths) ptrs.push_back(&t);
    return setup_sheet_html(ptrs, material, job_name);
}

}  // namespace cnc::cam
