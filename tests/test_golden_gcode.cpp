// G-code e folha de trabalho: o texto tem de sair igual ao do app em Python.
#include <regex>

#include "cad/document.hpp"
#include "cam/gcode.hpp"
#include "golden.hpp"

using namespace cnc;

namespace {
// A data muda a cada execucao: mascara como o script Python fez.
std::string mask(const std::string& text) {
    static const std::regex re(R"(Data: \d{2}/\d{2}/\d{4} \d{2}:\d{2})");
    return std::regex_replace(text, re, "Data: *");
}

cam::GCodeSettings settings_from(const Json& j) {
    cam::GCodeSettings s;
    s.post = from_str<cam::PostProcessor>(j.at("post").get<std::string>()).value_or(cam::PostProcessor::Grbl);
    s.decimals = j.at("decimals").get<int>();
    s.line_numbers = j.at("line_numbers").get<bool>();
    s.line_increment = j.at("line_increment").get<int>();
    s.spindle_on = j.at("spindle_on").get<std::string>();
    s.spindle_off = j.at("spindle_off").get<std::string>();
    s.coolant_on = j.at("coolant_on").get<std::string>();
    s.coolant_off = j.at("coolant_off").get<std::string>();
    s.program_end = j.at("program_end").get<std::string>();
    s.tool_change = j.at("tool_change").get<bool>();
    s.comments = j.at("comments").get<bool>();
    s.home_at_end = j.at("home_at_end").get<bool>();
    return s;
}

// Primeira linha em que os textos divergem, para a mensagem de erro.
std::string first_diff(const std::string& got, const std::string& want) {
    size_t line = 1, start = 0;
    for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        if (got[i] != want[i]) {
            const size_t a = got.rfind('\n', i) == std::string::npos ? 0 : got.rfind('\n', i) + 1;
            const size_t b = want.rfind('\n', i) == std::string::npos ? 0 : want.rfind('\n', i) + 1;
            return "linha " + std::to_string(line) + ": C++ [" + got.substr(a, got.find('\n', i) - a) +
                   "] x Python [" + want.substr(b, want.find('\n', i) - b) + "]";
        }
        if (got[i] == '\n') {
            ++line;
            start = i + 1;
        }
    }
    (void)start;
    return "tamanhos diferentes: " + std::to_string(got.size()) + " x " + std::to_string(want.size());
}
}  // namespace

TEST_CASE("golden: G-code e folha de trabalho iguais ao Python") {
    if (!testing::golden_exists("gcode.json")) return;
    const Json golden = testing::load_golden("gcode.json");
    int checked = 0;
    for (const Json& c : golden.at("cases")) {
        const std::string name = c.at("name").get<std::string>();
        INFO("caso: " << name);
        cad::Document doc;
        doc.from_json(c.at("document"));
        doc.recalculate_toolpaths();

        std::vector<const cam::Toolpath*> tps = cam::calculated_toolpaths(doc.toolpaths);
        const Json& g = c.at("gcode");
        const std::string job_name = g.at("job_name").get<std::string>();

        const std::string text = mask(cam::generate_gcode(tps, doc.material, settings_from(g.at("settings")), job_name));
        const std::string want = g.at("text").get<std::string>();
        CHECK_MESSAGE(text == want, first_diff(text, want));

        const std::string sheet = mask(cam::setup_sheet(tps, doc.material, job_name));
        const std::string want_sheet = g.at("sheet").get<std::string>();
        CHECK_MESSAGE(sheet == want_sheet, first_diff(sheet, want_sheet));
        ++checked;
    }
    CHECK(checked >= 8);
}
