// Porte de src/cam/presets.py.
#include "cam/presets.hpp"

#include <cmath>
#include <format>

#include <QString>

#include "cam/profile.hpp"

namespace cnc::cam {

Tool house_tool(double diameter, double pass_depth) {
    Tool t;
    t.name = std::format("Topo Raso ({:g} mm)", diameter);
    t.tool_type = ToolType::EndMill;
    t.diameter = diameter;
    t.num_flutes = 2;
    t.pass_depth = pass_depth;
    t.stepover = kPassoLateralMm;
    t.spindle_speed = kRotacaoRpm;
    t.feed_rate = kAvancoMmMin;
    t.plunge_rate = kMergulhoMmMin;
    // Numero da ferramenta por diametro, para a troca automatica.
    t.tool_number = diameter == 2.0 ? 2 : diameter == 4.0 ? 4 : diameter == 6.0 ? 6 : 1;
    return t;
}

std::vector<Tool> house_tools() {
    return {house_tool(2.0, 3.0), house_tool(4.0, 11.0), house_tool(6.0, 6.0)};
}

std::string MaterialPreset::name() const {
    return std::format("Corte {} {:g}", material, thickness);
}

int MaterialPreset::passes() const {
    return std::max(1, static_cast<int>(std::ceil(cut_depth() / pass_depth)));
}

ProfileSettings MaterialPreset::settings() const {
    ProfileSettings s;
    s.name = name();
    s.start_depth = 0.0;
    s.cut_depth = cut_depth();
    // Passo exato: reproduz na virgula o numero de passadas que o gadget faz hoje.
    s.maintain_exact_step = true;
    s.side = ProfileSide::Outside;
    s.direction = CutDirection::Conventional;
    s.allowance = 0.0;
    s.ramp = RampType::Smooth;
    s.ramp_distance = kRampaMm;
    // O corte automatico da casa nao usa pontes: a peca e retirada com a chapa ainda
    // presa na mesa.
    s.tabs = false;
    s.separate_interior = true;
    s.order = MachineOrder::Shortest;
    return s;
}

std::string MaterialPreset::describe() const {
    std::vector<std::string> parts = {
        std::format("chapa {:g} mm → corta {:g} mm (1 mm passante)", thickness, cut_depth()),
        std::format("fresa ⌀{:g} mm, passada {:g} mm = {} passe(s)", diameter, pass_depth, passes()),
        std::format("{:g} mm/min · {} rpm · convencional · rampa {:g} mm", kAvancoMmMin, kRotacaoRpm,
                    kRampaMm),
    };
    if (!note.empty()) parts.push_back(note);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += " · ";
        out += parts[i];
    }
    return out;
}

const std::vector<MaterialPreset>& house_presets() {
    static const std::string acrilico =
        "passada curta: calor derrete a borda e corte forçado trinca";
    static const std::vector<MaterialPreset> presets = {
        {"PVC", 10, 4.0, 11.0, ""},
        {"PVC", 20, 4.0, 11.0, ""},
        {"MDF", 6, 6.0, 7.0, ""},
        {"MDF", 9, 6.0, 6.0, ""},
        {"MDF", 15, 6.0, 6.0, ""},
        {"Acrílico", 4, 6.0, 3.0, acrilico},
        {"Acrílico", 6, 6.0, 3.0, acrilico},
        {"Acrílico", 8, 6.0, 3.0, acrilico},
        {"PS", 2, 2.0, 3.0, "chapa e fresa mais finas da casa: um passe só"},
    };
    return presets;
}

namespace {
// unicodedata.normalize("NFKD") + encode("ascii", "ignore") + strip + upper
std::string normalize_name(const std::string& text) {
    const QString nfkd = QString::fromStdString(text).normalized(QString::NormalizationForm_KD);
    std::string out;
    for (const QChar ch : nfkd) {
        if (ch.unicode() < 128) out.push_back(static_cast<char>(ch.unicode()));
    }
    const QString trimmed = QString::fromStdString(out).trimmed().toUpper();
    return trimmed.toStdString();
}
}  // namespace

std::optional<MaterialPreset> find_preset(const std::string& material, double thickness) {
    const std::string alvo = normalize_name(material);
    for (const MaterialPreset& p : house_presets()) {
        if (normalize_name(p.material) == alvo && std::fabs(p.thickness - thickness) < 0.01) return p;
    }
    return std::nullopt;
}

std::vector<MaterialPreset> presets_for_thickness(double thickness) {
    std::vector<MaterialPreset> out;
    for (const MaterialPreset& p : house_presets()) {
        if (std::fabs(p.thickness - thickness) < 0.01) out.push_back(p);
    }
    return out;
}

}  // namespace cnc::cam
