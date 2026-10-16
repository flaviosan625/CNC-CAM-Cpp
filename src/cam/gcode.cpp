// Porte de src/cam/gcode.py.
#include "cam/gcode.hpp"

#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <QDateTime>
#include <QString>

namespace cnc::cam {

const char* post_label(PostProcessor p) {
    switch (p) {
        case PostProcessor::Grbl: return "GRBL (Arduino / ESP32)";
        case PostProcessor::Mach3: return "Mach3 / Mach4";
        case PostProcessor::RichAutoA11: return "RichAuto DSP A11 (3 eixos)";
        case PostProcessor::RichAutoA18: return "RichAuto DSP A18 (XYZ)";
        case PostProcessor::NcStudio: return "NCStudio (Weihong)";
        case PostProcessor::LinuxCnc: return "LinuxCNC";
        case PostProcessor::VectricAspire: return "Aspire instalado (.pp)";
    }
    return "";
}

std::string GCodeSettings::end_command() const {
    if (!program_end.empty()) return program_end;
    return post == PostProcessor::Grbl ? "M2" : "M30";
}

namespace {

struct ParsedVectricPost {
    std::string name;
    std::string extension;
    std::string units;
    int line_start = 0;
    int line_increment = 10;
    std::map<std::string, std::vector<std::string>> sections;
    std::map<std::string, std::string> variable_prefix;
    std::map<std::string, int> variable_decimals;
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

ParsedVectricPost read_vectric_post(const std::string& path) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) throw std::runtime_error("Não foi possível abrir o pós-processador Aspire: " + path);
    ParsedVectricPost post;
    std::string line;
    std::string section;
    const std::regex assignment("^\\s*([A-Z_]+)\\s*=\\s*\"?([^\"\\r\\n]*)\"?\\s*$");
    const std::regex post_name("POST_NAME.*\"([^\"]+)\"");
    const std::regex var_line(R"(^\s*VAR\s+([A-Z0-9_]+)\s*=\s*\[([^\]]+)\])");
    while (std::getline(input, line)) {
        const std::string clean = trim(line);
        if (clean.empty() || clean.front() == '+') continue;
        std::smatch match;
        if (std::regex_search(clean, match, post_name)) post.name = match[1].str();
        if (std::regex_search(clean, match, assignment)) {
            const std::string key = match[1].str();
            const std::string value = trim(match[2].str());
            if (key == "FILE_EXTENSION") post.extension = value;
            else if (key == "UNITS") post.units = value;
            else if (key == "LINE_NUMBER_START") post.line_start = std::stoi(value);
            else if (key == "LINE_NUMBER_INCREMENT") post.line_increment = std::stoi(value);
        }
        if (std::regex_search(clean, match, var_line)) {
            const std::string var_name = match[1].str();
            std::stringstream parts(match[2].str());
            std::vector<std::string> fields;
            std::string part;
            while (std::getline(parts, part, '|')) fields.push_back(part);
            if (fields.size() >= 4) {
                const std::string format = fields.back();
                const auto dot = format.rfind('.');
                post.variable_prefix[var_name] = fields[2];
                if (dot != std::string::npos)
                    post.variable_decimals[var_name] = std::stoi(format.substr(dot + 1));
            }
        }
        if (clean.rfind("begin ", 0) == 0) {
            section = trim(clean.substr(6));
            continue;
        }
        if (!section.empty() && clean.size() >= 2 && clean.front() == '"' && clean.back() == '"')
            post.sections[section].push_back(clean.substr(1, clean.size() - 2));
    }
    if (post.name.empty()) post.name = std::filesystem::path(path).stem().string();
    return post;
}

bool is_richauto(PostProcessor post) {
    return post == PostProcessor::RichAutoA11 || post == PostProcessor::RichAutoA18;
}

}  // namespace

namespace {

// Pontuacao que nao existe em ASCII, traduzida antes de descartar o resto. Sem isto um
// travessao simplesmente some e deixa espaco duplo no comentario.
const std::vector<std::pair<const char*, const char*>>& punctuation() {
    static const std::vector<std::pair<const char*, const char*>> table = {
        {"—", "-"}, {"–", "-"}, {"―", "-"}, {"·", "-"}, {"•", "*"},   {"“", "\""}, {"”", "\""},
        {"„", "\""}, {"‘", "'"}, {"’", "'"}, {"…", "..."}, {"×", "x"}, {"≈", "~"},  {"°", "deg"},
        {"⌀", "d"}, {"²", "2"}, {"⚠", "!"},
    };
    return table;
}

class Writer {
public:
    explicit Writer(const GCodeSettings& s) : s_(s) {}

    void raw(const std::string& text) {
        if (s_.line_numbers && !text.empty() && text[0] != '(') {
            n_ += s_.line_increment;
            lines_.push_back("N" + std::to_string(n_) + " " + text);
        } else {
            lines_.push_back(text);
        }
    }
    void comment(const std::string& text) {
        if (s_.comments) lines_.push_back("(" + ascii_safe(text) + ")");
    }
    void blank() { lines_.push_back(""); }
    std::string text() const {
        std::string out;
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (i) out += "\n";
            out += lines_[i];
        }
        return out + "\n";
    }

private:
    const GCodeSettings& s_;
    std::vector<std::string> lines_;
    int n_ = 0;
};

struct VectricRenderContext {
    const ParsedVectricPost& post;
    const Toolpath* path = nullptr;
    const cad::MaterialSetup& material;
    std::string job_name;
    double x = 0.0, y = 0.0, z = 0.0, feed = 0.0;
    int line_number = 0;
    bool inches = false;
};

std::string fixed_number(double value, int decimals) {
    return std::format("{:.{}f}", value, std::clamp(decimals, 0, 8));
}

std::string vectric_variable(const std::string& token, VectricRenderContext& ctx) {
    const auto& p = ctx.post;
    const double scale = ctx.inches ? 1.0 / 25.4 : 1.0;
    std::string variable;
    double value = 0.0;
    bool has_number = true;
    if (token == "X") { variable = "X_POSITION"; value = ctx.x * scale; }
    else if (token == "Y") { variable = "Y_POSITION"; value = ctx.y * scale; }
    else if (token == "Z") { variable = "Z_POSITION"; value = ctx.z * scale; }
    else if (token == "XH") { variable = "X_HOME_POSITION"; value = ctx.material.home_x * scale; }
    else if (token == "YH") { variable = "Y_HOME_POSITION"; value = ctx.material.home_y * scale; }
    else if (token == "ZH" || token == "SAFEZ") {
        variable = token == "ZH" ? "Z_HOME_POSITION" : "SAFE_Z_HEIGHT";
        value = (ctx.material.surface_z() + ctx.material.clearance_z) * scale;
    }
    else if (token == "F") { variable = "FEED_RATE"; value = ctx.feed * scale; }
    else if (token == "FC") { variable = "CUT_RATE"; value = ctx.path ? ctx.path->feed_rate * scale : 0; }
    else if (token == "FP") { variable = "PLUNGE_RATE"; value = ctx.path ? ctx.path->plunge_rate * scale : 0; }
    else if (token == "S") { variable = "SPINDLE_SPEED"; value = ctx.path ? ctx.path->spindle_speed : 0; }
    else if (token == "N") {
        variable = "LINE_NUMBER";
        ctx.line_number += p.line_increment;
        value = ctx.line_number;
    }
    else if (token == "T") { variable = "TOOL_NUMBER"; value = ctx.path ? ctx.path->tool_number : 1; }
    else if (token == "I" || token == "J" || token == "IA" || token == "JA") {
        // Os percursos do CNC-CAM são linearizados; centros de arco não são gerados.
        return {};
    }
    else if (token == "XLENGTH") { variable = "X_LENGTH"; value = ctx.material.width * scale; }
    else if (token == "YLENGTH") { variable = "Y_LENGTH"; value = ctx.material.height * scale; }
    else if (token == "ZLENGTH") { variable = "Z_LENGTH"; value = ctx.material.thickness * scale; }
    else if (token == "DWELL") { variable = "DWELL_TIME"; value = ctx.feed; }
    else has_number = false;

    if (has_number) {
        const auto prefix = p.variable_prefix.find(variable);
        const auto decimals = p.variable_decimals.find(variable);
        std::string letter = prefix == p.variable_prefix.end() ? token : prefix->second;
        const int digits = decimals == p.variable_decimals.end() ? (token == "N" || token == "T" || token == "S" ? 0 : 3)
                                                                    : decimals->second;
        return letter + fixed_number(value, digits);
    }

    if (token == "TP_FILENAME" || token == "FILE_NAME" || token == "PATHNAME" || token == "FILE_PATHNAME")
        return ascii_safe(ctx.job_name);
    if (token == "TOOLPATH_NAME" || token == "TOOLNAME" || token == "TOOL_NAME")
        return ctx.path ? ascii_safe(ctx.path->name) : std::string{};
    if (token == "TOOLPATH_NOTES" || token == "TOOL_NOTES" || token == "FILE_NOTES") {
        if (!ctx.path) return {};
        std::string notes;
        for (const auto& note : ctx.path->notes) {
            if (!notes.empty()) notes += " | ";
            notes += ascii_safe(note);
        }
        return notes;
    }
    if (token == "TOOLS_USED") return ctx.path ? "T" + std::to_string(ctx.path->tool_number) : std::string{};
    if (token == "TOOLPATHS_OUTPUT") return ctx.path ? ascii_safe(ctx.path->name) : std::string{};
    if (token == "DATE") return QDateTime::currentDateTime().toString("dd/MM/yyyy").toStdString();
    if (token == "TIME") return QDateTime::currentDateTime().toString("HH:mm").toStdString();
    if (token == "Z_ORIGIN") return ctx.material.zero_z == "top" ? "TOP" : "BOTTOM";
    if (token == "XY_ORIGIN") return "LOWER_LEFT";
    if (token == "X_ORIGIN_POS") return fixed_number(ctx.material.origin_xy().x * scale, 3);
    if (token == "Y_ORIGIN_POS") return fixed_number(ctx.material.origin_xy().y * scale, 3);
    if (token == "WRAP_DIA" || token == "Radius" || token == "ANGLE" ||
        token == "XMAX" || token == "XMIN" || token == "YMAX" || token == "YMIN" ||
        token == "ZMAX" || token == "ZMIN" || token == "ArcStartX" || token == "ArcStartY" ||
        token == "ArcMidX" || token == "ArcMidY" || token == "ArcMidXI" || token == "ArcMidYI")
        throw std::runtime_error("Este pós Aspire exige limites de arco ou eixo rotativo que o gerador atual não oferece: [" + token + "].");
    if (token.size() <= 3 && std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) {
        const int code = std::stoi(token);
        return code >= 0 && code <= 127 ? std::string(1, static_cast<char>(code)) : std::string{};
    }
    throw std::runtime_error("O pós Aspire usa a variável não suportada [" + token + "].");
}

std::string render_vectric_line(const std::string& source, VectricRenderContext& ctx) {
    static const std::regex token_pattern(R"(\[([A-Za-z0-9_]+)\])");
    std::string result;
    std::sregex_iterator it(source.begin(), source.end(), token_pattern), end;
    size_t offset = 0;
    for (; it != end; ++it) {
        const auto& match = *it;
        result.append(source, offset, static_cast<size_t>(match.position()) - offset);
        result += vectric_variable(match[1].str(), ctx);
        offset = static_cast<size_t>(match.position() + match.length());
    }
    result.append(source, offset, std::string::npos);
    return result;
}

void render_vectric_section(std::string& out, const ParsedVectricPost& post, const char* section,
                            VectricRenderContext& ctx, bool required = false) {
    const auto found = post.sections.find(section);
    if (found == post.sections.end()) {
        if (required) throw std::runtime_error(std::string("O pós Aspire não possui a seção ") + section + ".");
        return;
    }
    for (const auto& line : found->second) {
        std::string rendered = render_vectric_line(line, ctx);
        const bool is_feed_block = std::string_view(section) == "FIRST_FEED_MOVE" ||
                                   std::string_view(section) == "FEED_MOVE";
        const bool has_feed_token = line.find("[F]") != std::string::npos ||
                                    line.find("[FC]") != std::string::npos ||
                                    line.find("[FP]") != std::string::npos;
        static const std::regex linear_feed_code(R"(G0?1(?=\s|[XYZ]))");
        if (is_feed_block && !has_feed_token && std::regex_search(rendered, linear_feed_code)) {
            const std::string variable = ctx.post.variable_prefix.contains("FEED_RATE")
                ? "FEED_RATE" : "CUT_RATE";
            const auto prefix = ctx.post.variable_prefix.find(variable);
            const auto decimals = ctx.post.variable_decimals.find(variable);
            const std::string letter = prefix == ctx.post.variable_prefix.end() || prefix->second.empty()
                ? "F" : prefix->second;
            const int digits = decimals == ctx.post.variable_decimals.end() ? 3 : decimals->second;
            rendered += letter + fixed_number(ctx.feed * (ctx.inches ? 1.0 / 25.4 : 1.0), digits);
        }
        if (!rendered.empty()) out += rendered + "\r\n";
    }
}

std::string generate_vectric_gcode(const std::vector<const Toolpath*>& toolpaths,
                                   const cad::MaterialSetup& material,
                                   const GCodeSettings& settings, const std::string& job_name) {
    if (settings.vectric_post_file.empty())
        throw std::runtime_error("Selecione um pós-processador .pp do Aspire.");
    const ParsedVectricPost post = read_vectric_post(settings.vectric_post_file);
    if (post.units != "MM" && post.units != "INCHES")
        throw std::runtime_error("O pós Aspire deve definir UNITS como MM ou INCHES.");
    std::vector<const Toolpath*> visible;
    for (const auto* path : toolpaths) if (path && path->visible && !path->moves.empty()) visible.push_back(path);
    if (visible.empty()) throw std::runtime_error("Não há percursos calculados para gerar o arquivo.");
    std::string out;
    int line_number = post.line_start;
    VectricRenderContext context{post, visible.front(), material, job_name};
    context.line_number = line_number;
    context.inches = post.units == "INCHES";
    render_vectric_section(out, post, "HEADER", context, true);
    line_number = context.line_number;
    const Toolpath* previous = visible.front();
    bool first_feed = true;
    for (size_t index = 0; index < visible.size(); ++index) {
        const auto* path = visible[index];
        context.path = path;
        if (index > 0) {
            if (path->tool_number != previous->tool_number && post.sections.contains("TOOLCHANGE")) {
                render_vectric_section(out, post, "TOOLCHANGE", context);
                first_feed = true;
            } else if (post.sections.contains("NEW_SEGMENT")) {
                render_vectric_section(out, post, "NEW_SEGMENT", context);
            }
        }
        for (const auto& move : path->moves) {
            context.x = move.x; context.y = move.y; context.z = move.z;
            context.feed = move.kind == MoveKind::Plunge ? path->plunge_rate : path->feed_rate;
            context.line_number = line_number;
            if (move.kind == MoveKind::Dwell) {
                context.feed = move.seconds;
                render_vectric_section(out, post, "DWELL", context, true);
            } else if (move.kind == MoveKind::Rapid) {
                render_vectric_section(out, post, "RAPID_MOVE", context, true);
            } else {
                const char* section = first_feed && post.sections.contains("FIRST_FEED_MOVE")
                    ? "FIRST_FEED_MOVE" : "FEED_MOVE";
                render_vectric_section(out, post, section, context, true);
                first_feed = false;
            }
            line_number = context.line_number;
        }
        previous = path;
    }
    context.path = previous;
    context.line_number = line_number;
    render_vectric_section(out, post, "FOOTER", context, true);
    if (out.empty()) throw std::runtime_error("O pós Aspire não gerou linhas de G-code.");
    return out;
}

std::string fmt(double value, int decimals) {
    std::string text = std::format("{:.{}f}", value, decimals);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        while (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text.empty() ? "0" : text;
}

std::string now_text(const char* pattern) {
    return QDateTime::currentDateTime().toString(pattern).toStdString();
}

struct State {
    std::optional<double> x, y, z, f;
    std::optional<int> tool;
};

std::string z_label(const cad::MaterialSetup& m) { return m.zero_z == "top" ? "face superior" : "base"; }

void header(Writer& w, const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
            const GCodeSettings& s, const std::string& job_name) {
    w.comment("Gerado por CNC-CAM");
    if (!job_name.empty()) w.comment("Trabalho: " + job_name);
    w.comment("Data: " + now_text("dd/MM/yyyy HH:mm"));
    w.comment(std::string("Pos-processador: ") + post_label(s.post));
    w.comment(std::format("Material: {:g} x {:g} x {:g} mm", material.width, material.height, material.thickness));
    w.comment("Zero Z na " + z_label(material) + " do material");
    const double total = py_sum(toolpaths, [](const Toolpath* t) { return t->estimated_seconds(); });
    w.comment(std::format("Tempo estimado: {:.0f} min", total / 60));
    w.blank();

    w.raw("G21");  // milimetros - nunca implicito
    w.raw("G90");  // coordenadas absolutas
    w.raw("G17");  // plano XY
    if (!is_richauto(s.post)) w.raw("G94");  // RichAuto usa avanço mm/min sem G94 no perfil
    if (is_richauto(s.post)) w.raw("G54");
    if (s.post != PostProcessor::Grbl) {
        w.raw("G40");  // sem compensacao de raio: o offset ja esta na geometria
        w.raw("G49");  // sem compensacao de comprimento
    }

    if (!s.tool_change && !toolpaths.empty()) {
        w.raw(s.spindle_on + " S" + std::to_string(toolpaths[0]->spindle_speed));
        if (!s.coolant_on.empty()) w.raw(s.coolant_on);
    }

    const double clearance = material.surface_z() + material.clearance_z;
    w.raw("G0 Z" + fmt(clearance, s.decimals));
}

void emit_moves(Writer& w, const Toolpath& tp, const GCodeSettings& s, State& state) {
    const int d = s.decimals;
    const double tol = std::pow(10.0, -d) / 2;
    for (const Move& m : tp.moves) {
        if (m.kind == MoveKind::Dwell) {
            w.raw("G4 P" + fmt(m.seconds, 2));
            continue;
        }

        std::vector<std::string> parts;
        parts.push_back(m.kind == MoveKind::Rapid ? "G0" : "G1");

        auto axis = [&](char name, std::optional<double>& slot, double value) {
            if (!slot || std::fabs(*slot - value) > tol) {
                parts.push_back(std::string(1, name) + fmt(value, d));
                slot = value;
            }
        };
        axis('X', state.x, m.x);
        axis('Y', state.y, m.y);
        axis('Z', state.z, m.z);

        if (parts.size() == 1) continue;

        if (m.kind != MoveKind::Rapid) {
            const double feed = m.kind == MoveKind::Plunge ? tp.plunge_rate : tp.feed_rate;
            if (!state.f || std::fabs(*state.f - feed) > 1e-6) {
                parts.push_back("F" + fmt(feed, 1));
                state.f = feed;
            }
        }

        std::string line;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) line += " ";
            line += parts[i];
        }
        w.raw(line);
    }
}

void footer(Writer& w, const cad::MaterialSetup& material, const GCodeSettings& s, const State& state) {
    w.blank();
    const double clearance = material.surface_z() + material.clearance_z;
    const bool already_up = state.z && std::fabs(*state.z - clearance) < 1e-6;
    if (!already_up) w.raw("G0 Z" + fmt(clearance, s.decimals));
    if (!s.coolant_off.empty()) w.raw(s.coolant_off);
    w.raw(s.spindle_off);
    if (s.home_at_end) w.raw("G0 X" + fmt(material.home_x, s.decimals) + " Y" + fmt(material.home_y, s.decimals));
    w.raw(s.end_command());
}

std::vector<const Toolpath*> pointers(const std::vector<Toolpath>& tps) {
    std::vector<const Toolpath*> out;
    for (const Toolpath& t : tps) out.push_back(&t);
    return out;
}

}  // namespace

std::string ascii_safe(const std::string& text) {
    std::string replaced = text;
    for (const auto& [src, dst] : punctuation()) {
        const std::string from = src;
        size_t pos = 0;
        while ((pos = replaced.find(from, pos)) != std::string::npos) {
            replaced.replace(pos, from.size(), dst);
            pos += std::string(dst).size();
        }
    }
    const QString normalized = QString::fromStdString(replaced).normalized(QString::NormalizationForm_KD);
    std::string plain;
    for (const QChar ch : normalized) {
        if (ch.unicode() < 128) plain.push_back(static_cast<char>(ch.unicode()));
    }
    for (char& c : plain) {
        if (c == '(') c = '[';
        else if (c == ')') c = ']';
    }
    return plain;
}

std::vector<VectricPostFile> installed_vectric_post_files() {
    std::vector<VectricPostFile> posts;
    const std::filesystem::path root("C:/ProgramData/Vectric/Aspire");
    std::error_code error;
    if (!std::filesystem::exists(root, error)) return posts;
    for (std::filesystem::recursive_directory_iterator it(root,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!it->is_regular_file(error) || it->path().extension() != ".pp") continue;
        try {
            const auto parsed = read_vectric_post(it->path().string());
            posts.push_back({parsed.name, it->path().string(), parsed.extension});
        } catch (...) {
            // Um arquivo inválido não impede carregar os demais pós instalados.
        }
    }
    std::sort(posts.begin(), posts.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return posts;
}

std::string generate_gcode(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                           const GCodeSettings& s, const std::string& job_name) {
    if (s.post == PostProcessor::VectricAspire)
        return generate_vectric_gcode(toolpaths, material, s, job_name);
    Writer w(s);
    std::vector<const Toolpath*> visible;
    for (const Toolpath* t : toolpaths) {
        if (t->visible && !t->moves.empty()) visible.push_back(t);
    }

    header(w, visible, material, s, job_name);

    State state;
    for (const Toolpath* tp : visible) {
        w.blank();
        w.comment("Percurso: " + tp->name);
        w.comment(std::format("Ferramenta: T{} {}", tp->tool_number, tp->tool_name));
        for (const std::string& note : tp->notes) w.comment("Aviso: " + note);

        if (s.tool_change && state.tool != tp->tool_number) {
            if (state.tool) w.raw(s.spindle_off);
            w.raw(std::format("T{} M6", tp->tool_number));
            w.raw(std::format("{} S{}", s.spindle_on, tp->spindle_speed));
            if (!s.coolant_on.empty()) w.raw(s.coolant_on);
            state.tool = tp->tool_number;
            state.f.reset();
        }

        emit_moves(w, *tp, s, state);
    }

    footer(w, material, s, state);
    return w.text();
}

std::string generate_gcode(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                           const GCodeSettings& s, const std::string& job_name) {
    return generate_gcode(pointers(toolpaths), material, s, job_name);
}

std::string setup_sheet(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                        const std::string& job_name) {
    std::vector<std::string> lines = {
        "FOLHA DE TRABALHO — CNC-CAM",
        std::string(58, '='),
        "Trabalho: " + (job_name.empty() ? std::string("Sem título") : job_name),
        "Data: " + now_text("dd/MM/yyyy HH:mm"),
        "",
        std::format("Material: {:g} × {:g} × {:g} mm", material.width, material.height, material.thickness),
        std::format("Zero XY: {} / {}   Zero Z: {}", material.zero_x, material.zero_y, z_label(material)),
        "",
        "PERCURSOS",
        std::string(58, '-'),
    };
    double total = 0.0;
    int i = 1;
    for (const Toolpath* tp : toolpaths) {
        const double seconds = tp->estimated_seconds();
        total += seconds;
        const auto [cut, rapid] = tp->lengths();
        const auto [z0, z1] = tp->depth_range();
        lines.push_back(std::format("{}. {}", i++, tp->name));
        lines.push_back(std::format("   Ferramenta ....... T{}  {}", tp->tool_number, tp->tool_name));
        lines.push_back(std::format("   Rotação .......... {} rpm", tp->spindle_speed));
        lines.push_back(std::format("   Avanço ........... {:g} mm/min  (mergulho {:g})", tp->feed_rate, tp->plunge_rate));
        lines.push_back(std::format("   Profundidade ..... de {:.2f} a {:.2f} mm", z1, z0));
        lines.push_back(std::format("   Percurso ......... corte {:.2f} m · rápido {:.2f} m", cut / 1000, rapid / 1000));
        lines.push_back(std::format("   Tempo estimado ... {:.0f} min", seconds / 60));
        for (const std::string& note : tp->notes) lines.push_back("   ⚠ " + note);
        lines.push_back("");
    }
    lines.push_back(std::string(58, '-'));
    lines.push_back(std::format("TEMPO TOTAL ESTIMADO: {:.0f} min", total / 60));

    std::string out;
    for (size_t k = 0; k < lines.size(); ++k) {
        if (k) out += "\n";
        out += lines[k];
    }
    return out + "\n";
}

std::string setup_sheet(const std::vector<Toolpath>& toolpaths, const cad::MaterialSetup& material,
                        const std::string& job_name) {
    return setup_sheet(pointers(toolpaths), material, job_name);
}

}  // namespace cnc::cam
