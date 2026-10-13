#include "cam/tools.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

#include "cam/presets.hpp"
#include "utils/user_config.hpp"

namespace cnc::cam {

const char* tool_type_label(ToolType t) {
    switch (t) {
        case ToolType::EndMill: return "Fresa de topo";
        case ToolType::BallNose: return "Fresa esférica";
        case ToolType::VBit: return "Fresa em V";
        case ToolType::Engraving: return "Ponta de gravação";
        case ToolType::Radiused: return "Topo com raio";
        case ToolType::TaperedBall: return "Esférica cônica";
        case ToolType::Drill: return "Broca";
    }
    return "";
}

bool is_milling_type(ToolType t) { return t != ToolType::Drill; }

double Tool::chip_load() const {
    const double denom = static_cast<double>(spindle_speed) * num_flutes;
    return denom != 0.0 ? feed_rate / denom : 0.0;
}

double Tool::flat_width_at(double depth) const {
    if (tool_type != ToolType::VBit && tool_type != ToolType::Engraving) return diameter;
    const double half = radians(v_angle / 2.0);
    const double width = tip_diameter + 2.0 * depth * std::tan(half);
    return std::min(width, diameter);
}

double Tool::max_v_depth() const {
    if (tool_type != ToolType::VBit && tool_type != ToolType::Engraving) {
        return std::numeric_limits<double>::infinity();
    }
    const double half = radians(v_angle / 2.0);
    if (half <= 0) return std::numeric_limits<double>::infinity();
    return (diameter - tip_diameter) / (2.0 * std::tan(half));
}

namespace {
// f"{v:g}".replace(".", ",")
std::string fmt_comma(double v) {
    std::string s = std::format("{:g}", v);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}
}  // namespace

std::string Tool::describe() const {
    return std::format("{} · ⌀{} mm · {}", name, fmt_comma(diameter), tool_type_label(tool_type));
}

Json Tool::to_json() const {
    Json d = Json::object();
    d["name"] = name;
    d["tool_type"] = to_str(tool_type);
    d["diameter"] = diameter;
    d["num_flutes"] = num_flutes;
    d["v_angle"] = v_angle;
    d["tip_diameter"] = tip_diameter;
    d["corner_radius"] = corner_radius;
    d["pass_depth"] = pass_depth;
    d["stepover"] = stepover;
    d["clearance_stepover"] = clearance_stepover;
    d["final_stepover"] = final_stepover;
    d["spindle_speed"] = spindle_speed;
    d["feed_rate"] = feed_rate;
    d["plunge_rate"] = plunge_rate;
    d["tool_number"] = tool_number;
    d["notes"] = notes;
    return d;
}

Tool Tool::from_json(const Json& d) {
    Tool t;
    // tipo desconhecido volta para fresa de topo, como o Python
    if (auto raw = d.find("tool_type"); raw != d.end() && raw->is_string()) {
        t.tool_type = from_str<ToolType>(raw->get<std::string>()).value_or(ToolType::EndMill);
    }
    t.name = jget<std::string>(d, "name", t.name);
    t.diameter = jget<double>(d, "diameter", t.diameter);
    t.num_flutes = jget<int>(d, "num_flutes", t.num_flutes);
    t.v_angle = jget<double>(d, "v_angle", t.v_angle);
    t.tip_diameter = jget<double>(d, "tip_diameter", t.tip_diameter);
    t.corner_radius = jget<double>(d, "corner_radius", t.corner_radius);
    t.pass_depth = jget<double>(d, "pass_depth", t.pass_depth);
    t.stepover = jget<double>(d, "stepover", t.stepover);
    t.clearance_stepover = jget<double>(d, "clearance_stepover", t.clearance_stepover);
    t.final_stepover = jget<double>(d, "final_stepover", t.final_stepover);
    t.spindle_speed = jget<int>(d, "spindle_speed", t.spindle_speed);
    t.feed_rate = jget<double>(d, "feed_rate", t.feed_rate);
    t.plunge_rate = jget<double>(d, "plunge_rate", t.plunge_rate);
    t.tool_number = jget<int>(d, "tool_number", t.tool_number);
    t.notes = jget<std::string>(d, "notes", t.notes);
    return t;
}

// ======================================================================
// Biblioteca padrao
// ======================================================================

namespace {
Tool mill(const char* name, double dia, double pass_depth, double stepover, double feed,
          double plunge, int rpm, int flutes, int number) {
    Tool t;
    t.name = name;
    t.tool_type = ToolType::EndMill;
    t.diameter = dia;
    t.num_flutes = flutes;
    t.pass_depth = pass_depth;
    t.stepover = stepover;
    t.feed_rate = feed;
    t.plunge_rate = plunge;
    t.spindle_speed = rpm;
    t.tool_number = number;
    return t;
}
}  // namespace

std::vector<Tool> default_tools() {
    // As tres primeiras sao as fresas da casa, com os avancos que ja estao em producao;
    // o resto e biblioteca generica.
    std::vector<Tool> out = house_tools();
    out.push_back(mill("Fresa de topo 3 mm", 3.0, 1.5, 1.2, 800, 250, 18000, 2, 10));
    out.push_back(mill("Fresa de topo 4 mm", 4.0, 2.0, 1.6, 1000, 300, 18000, 2, 11));
    out.push_back(mill("Fresa de topo 6 mm", 6.0, 3.0, 2.4, 1200, 350, 18000, 2, 12));
    out.push_back(mill("Fresa de topo 1/4\"", 6.35, 3.0, 2.5, 1200, 350, 18000, 2, 13));
    out.push_back(mill("Fresa de topo 8 mm", 8.0, 4.0, 3.2, 1400, 400, 16000, 2, 14));

    Tool ball;
    ball.name = "Fresa esférica 6 mm";
    ball.tool_type = ToolType::BallNose;
    ball.diameter = 6.0;
    ball.pass_depth = 2.0;
    ball.stepover = 0.8;
    ball.feed_rate = 1500;
    ball.plunge_rate = 400;
    ball.spindle_speed = 18000;
    ball.tool_number = 15;
    out.push_back(ball);

    auto vbit = [](const char* name, double angle, int number) {
        Tool t;
        t.name = name;
        t.tool_type = ToolType::VBit;
        t.diameter = 12.0;
        t.v_angle = angle;
        t.pass_depth = 3.0;
        t.stepover = 1.0;
        t.clearance_stepover = 3.0;
        t.final_stepover = 0.5;
        t.feed_rate = 1200;
        t.plunge_rate = 300;
        t.spindle_speed = 18000;
        t.tool_number = number;
        return t;
    };
    out.push_back(vbit("V-bit 60°", 60.0, 16));
    out.push_back(vbit("V-bit 90°", 90.0, 17));

    auto drill = [](const char* name, double dia, double pass_depth, int number) {
        Tool t;
        t.name = name;
        t.tool_type = ToolType::Drill;
        t.diameter = dia;
        t.pass_depth = pass_depth;
        t.feed_rate = 300;
        t.plunge_rate = 200;
        t.spindle_speed = 8000;
        t.tool_number = number;
        return t;
    };
    out.push_back(drill("Broca 3 mm", 3.0, 3.0, 18));
    out.push_back(drill("Broca 5 mm", 5.0, 4.0, 19));
    return out;
}

// ======================================================================
// Banco de ferramentas (persistente)
// ======================================================================

std::filesystem::path db_path() { return user_config::config_file("ferramentas.json"); }

ToolDatabase::ToolDatabase() : path(db_path()) {
    for (Tool& t : default_tools()) tools.push_back(std::make_shared<Tool>(std::move(t)));
}

ToolDatabase::ToolDatabase(std::filesystem::path file) : path(std::move(file)) {
    for (Tool& t : default_tools()) tools.push_back(std::make_shared<Tool>(std::move(t)));
}

ToolDatabase ToolDatabase::load(const std::filesystem::path& file) {
    const std::filesystem::path target = file.empty() ? db_path() : file;
    ToolDatabase db(target);
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) return db;
    try {
        const Json data = Json::parse(read_text_file(target));
        std::vector<ToolPtr> loaded;
        if (auto it = data.find("tools"); it != data.end()) {
            for (const Json& d : *it) loaded.push_back(std::make_shared<Tool>(Tool::from_json(d)));
        }
        if (!loaded.empty()) db.tools = std::move(loaded);
    } catch (const std::exception&) {
        return ToolDatabase(target);  // arquivo ilegivel: volta ao padrao
    }
    return db;
}

void ToolDatabase::save() const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Json payload = Json::object();
    payload["version"] = 1;
    Json arr = Json::array();
    for (const ToolPtr& t : tools) arr.push_back(t->to_json());
    payload["tools"] = std::move(arr);
    write_text_file(path, dumps_pretty(payload));
}

ToolDatabase::ToolPtr ToolDatabase::find(const std::string& name) const {
    for (const ToolPtr& t : tools) {
        if (t->name == name) return t;
    }
    return nullptr;
}

ToolDatabase::ToolPtr ToolDatabase::add(Tool tool) {
    const std::string base = tool.name;
    int n = 2;
    while (find(tool.name)) {
        tool.name = std::format("{} ({})", base, n);
        ++n;
    }
    tools.push_back(std::make_shared<Tool>(std::move(tool)));
    return tools.back();
}

void ToolDatabase::remove(const ToolPtr& tool) {
    tools.erase(std::remove(tools.begin(), tools.end(), tool), tools.end());
}

std::vector<ToolDatabase::ToolPtr> ToolDatabase::by_type(ToolType kind) const {
    std::vector<ToolPtr> out;
    for (const ToolPtr& t : tools) {
        if (t->tool_type == kind) out.push_back(t);
    }
    return out;
}

}  // namespace cnc::cam
