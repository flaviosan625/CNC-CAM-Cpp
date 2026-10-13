// Porte de src/cam/vectric_tools.py.
#include "cam/vectric_tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <map>
#include <set>
#include <tuple>

#include <QRegularExpression>
#include <QStringDecoder>
#include <QString>

namespace cnc::cam {

std::filesystem::path default_aspire_tool_db() {
    const char* pd = std::getenv("PROGRAMDATA");
    std::filesystem::path program_data = pd && *pd ? std::filesystem::path(pd) : std::filesystem::path("C:\\ProgramData");
    return program_data / "Vectric" / "Aspire" / "V8.5" / "ToolDatabase" / "tools.tool_db";
}

namespace {

std::uint32_t read_u32(const std::vector<unsigned char>& d, size_t at) {
    std::uint32_t v;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

double read_f64(const std::vector<unsigned char>& d, size_t at) {
    double v;
    std::memcpy(&v, d.data() + at, 8);
    return v;
}

bool sane_record(double diameter, double pass_depth, double stepover, double feed, double plunge,
                 std::uint32_t number, std::uint32_t rpm, std::uint32_t rate_mode, int in_mm) {
    return 0.001 <= diameter && diameter <= 200.0 && 0.0 <= pass_depth && pass_depth <= 500.0 &&
           0.0 <= stepover && stepover <= 500.0 && 0.0 <= feed && feed <= 100000.0 && 0.0 <= plunge &&
           plunge <= 100000.0 && number <= 999 && rpm <= 100000 && (rate_mode == 0 || rate_mode == 1) &&
           (in_mm == 0 || in_mm == 1);
}

double angle_from_name(const std::string& name) {
    static const QRegularExpression re(
        QStringLiteral("(?:v-bit|engrave).*?([0-9]+(?:[.,][0-9]+)?)\\s*(?:deg|['°])"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(QString::fromStdString(name));
    if (!m.hasMatch()) return 90.0;
    QString v = m.captured(1);
    v.replace(',', '.');
    return v.toDouble();
}

double tip_from_name(const std::string& name, bool imperial) {
    static const QRegularExpression re(QStringLiteral("([0-9]+(?:[.,][0-9]+)?)\\s*(?:\"|mm)?\\s*tip"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(QString::fromStdString(name));
    if (!m.hasMatch()) return 0.0;
    QString v = m.captured(1);
    v.replace(',', '.');
    return v.toDouble() * (imperial ? 25.4 : 1.0);
}

std::optional<Tool> convert(const std::string& name, double diameter, double pass_depth, double stepover,
                            double feed, double plunge, std::uint32_t old_number, std::uint32_t rpm, bool in_mm,
                            std::uint32_t rate_mode) {
    const QString lowered = QString::fromStdString(name).toCaseFolded();
    auto has = [&](const char* word) { return lowered.contains(QString::fromUtf8(word)); };
    const double factor = in_mm ? 1.0 : 25.4;
    // O banco 8.5 usa modo 1 para as unidades convencionais da biblioteca: mm/s em ferramentas
    // metricas e pol/min nas imperiais. As ferramentas metricas proprias desta instalacao usam
    // modo 0 (mm/min).
    const double rate_factor = in_mm ? (rate_mode == 1 ? 60.0 : 1.0) : (rate_mode == 1 ? 25.4 : 25.4 * 60.0);

    if (has("diamond drag") || has("ogee") || has("roundover") || has("form tool")) return std::nullopt;
    ToolType kind;
    if (has("v-bit") || has("v bit")) {
        kind = ToolType::VBit;
    } else if (has("engrave")) {
        kind = ToolType::Engraving;
    } else if (has("ball nose") || has("esfér")) {
        kind = ToolType::BallNose;
    } else if (has("drill") || has("broca")) {
        kind = ToolType::Drill;
    } else if (has("end mill") || has("topo raso") || has("fresa")) {
        kind = ToolType::EndMill;
    } else {
        return std::nullopt;
    }

    Tool t;
    // name.strip()
    const QString trimmed = QString::fromStdString(name).trimmed();
    t.name = trimmed.toStdString();
    t.tool_type = kind;
    t.diameter = diameter * factor;
    t.v_angle = (kind == ToolType::VBit || kind == ToolType::Engraving) ? angle_from_name(name) : 90.0;
    t.tip_diameter = kind == ToolType::Engraving ? tip_from_name(name, !in_mm) : 0.0;
    t.pass_depth = pass_depth * factor;
    t.stepover = stepover * factor;
    t.spindle_speed = static_cast<int>(rpm);
    t.feed_rate = feed * rate_factor;
    t.plunge_rate = plunge * rate_factor;
    t.tool_number = static_cast<int>(old_number);
    t.notes = std::format("Importada do Aspire 8.5; número original T{}. Confirmar número de cortes antes de usinar.",
                          old_number);
    return t;
}

using Signature = std::tuple<int, double, double, double, double, double, double, double, int>;

Signature signature(const Tool& t) {
    return {static_cast<int>(t.tool_type), py_round(t.diameter, 5), py_round(t.v_angle, 3),
            py_round(t.tip_diameter, 5),   py_round(t.pass_depth, 5), py_round(t.stepover, 5),
            py_round(t.feed_rate, 3),      py_round(t.plunge_rate, 3), t.spindle_speed};
}

void disambiguate_names(std::vector<Tool>& tools) {
    std::map<std::string, int> counts;
    for (const Tool& t : tools) ++counts[t.name];
    for (Tool& t : tools) {
        if (counts[t.name] > 1) t.name = std::format("{} · Z{:g} · F{:g}", t.name, t.pass_depth, t.feed_rate);
    }
}

bool valid_utf8_name(const std::string& s) {
    const QByteArray bytes(s.data(), static_cast<qsizetype>(s.size()));
    QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = dec(bytes);
    return !dec.hasError();
}

}  // namespace

VectricReadResult read_vectric_tools(const std::filesystem::path& path) {
    std::vector<unsigned char> data;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("nao foi possivel abrir " + path.string());
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    VectricReadResult out;
    std::set<std::tuple<std::string, int, double, double, double, double, double, std::uint32_t, std::uint32_t,
                        std::uint32_t>>
        seen_records;

    for (size_t name_at = 57; name_at < data.size(); ++name_at) {
        const std::uint32_t length = read_u32(data, name_at - 4);
        if (!(4 <= length && length <= 161) || name_at + length > data.size()) continue;
        if (data[name_at + length - 1] != 0) continue;
        const std::string name(reinterpret_cast<const char*>(data.data() + name_at), length - 1);
        if (!valid_utf8_name(name)) continue;
        if (name.empty()) continue;
        const QString qname = QString::fromStdString(name);
        bool has_control = false;
        for (const QChar c : qname) {
            if (c.unicode() < 32) has_control = true;
        }
        if (has_control) continue;

        const size_t base = name_at - 56;
        const double diameter = read_f64(data, base), pass_depth = read_f64(data, base + 8);
        const double stepover = read_f64(data, base + 16), feed = read_f64(data, base + 24);
        const double plunge = read_f64(data, base + 32);
        const std::uint32_t old_number = read_u32(data, base + 40), rpm = read_u32(data, base + 44);
        const std::uint32_t rate_mode = read_u32(data, base + 48);
        const int in_mm = data[name_at - 57];
        if (!sane_record(diameter, pass_depth, stepover, feed, plunge, old_number, rpm, rate_mode, in_mm)) continue;
        ++out.found;
        const auto record = std::make_tuple(name, in_mm, py_round(diameter, 9), py_round(pass_depth, 9),
                                            py_round(stepover, 9), py_round(feed, 9), py_round(plunge, 9),
                                            old_number, rpm, rate_mode);
        if (!seen_records.insert(record).second) continue;

        auto tool = convert(name, diameter, pass_depth, stepover, feed, plunge, old_number, rpm, in_mm != 0, rate_mode);
        if (!tool) {
            if (std::find(out.unsupported.begin(), out.unsupported.end(), name) == out.unsupported.end()) {
                out.unsupported.push_back(name);
            }
            continue;
        }
        out.tools.push_back(std::move(*tool));
    }
    disambiguate_names(out.tools);
    return out;
}

VectricImportReport import_aspire_tools(ToolDatabase& database, const std::filesystem::path& path) {
    VectricImportReport report;
    report.source = path.empty() ? default_aspire_tool_db() : path;
    VectricReadResult read = read_vectric_tools(report.source);
    report.unsupported = std::move(read.unsupported);
    report.found = read.found;

    std::set<Signature> signatures;
    std::set<int> used_numbers;
    for (const auto& t : database.tools) {
        signatures.insert(signature(*t));
        used_numbers.insert(t->tool_number);
    }
    int next_number = (used_numbers.empty() ? 0 : *used_numbers.rbegin()) + 1;

    for (Tool& tool : read.tools) {
        const Signature sig = signature(tool);
        if (signatures.count(sig)) {
            ++report.duplicates;
            continue;
        }
        while (used_numbers.count(next_number) && next_number <= 999) ++next_number;
        tool.tool_number = std::min(next_number, 999);
        used_numbers.insert(tool.tool_number);
        ++next_number;
        report.imported.push_back(database.add(tool));
        signatures.insert(sig);
    }
    return report;
}

}  // namespace cnc::cam
