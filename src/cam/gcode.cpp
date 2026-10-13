// Porte de src/cam/gcode.py.
#include "cam/gcode.hpp"

#include <cmath>
#include <format>
#include <map>
#include <optional>

#include <QDateTime>
#include <QString>

namespace cnc::cam {

const char* post_label(PostProcessor p) {
    switch (p) {
        case PostProcessor::Grbl: return "GRBL (Arduino / ESP32)";
        case PostProcessor::Mach3: return "Mach3 / Mach4";
        case PostProcessor::LinuxCnc: return "LinuxCNC";
    }
    return "";
}

std::string GCodeSettings::end_command() const {
    if (!program_end.empty()) return program_end;
    return post == PostProcessor::Grbl ? "M2" : "M30";
}

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
    w.raw("G94");  // avanco por minuto
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

std::string generate_gcode(const std::vector<const Toolpath*>& toolpaths, const cad::MaterialSetup& material,
                           const GCodeSettings& s, const std::string& job_name) {
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
