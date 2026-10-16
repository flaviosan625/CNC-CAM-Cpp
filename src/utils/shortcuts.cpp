#include "utils/shortcuts.hpp"

#include <QString>
#include <Qt>

#include "utils/json_util.hpp"

namespace cnc::shortcuts {

const Table& defaults() {
    static const Table table = {
        {"V", "select"}, {"L", "line"},       {"P", "polyline"},   {"R", "rectangle"},
        {"C", "circle"}, {"E", "ellipse"},    {"A", "arc"},        {"T", "scale"},
        {"M", "move"},   {"G", "group"},      {"U", "ungroup"},    {"N", "node_edit"},
        {"F", "zoom_fit"}, {"H", "pan"},      {"DELETE", "delete"}, {"BACKSPACE", "delete"},
    };
    return table;
}

const std::set<std::string>& reserved() {
    static const std::set<std::string> r = {"LEFT", "RIGHT", "UP", "DOWN", "ESCAPE", "TAB", "RETURN", "ENTER"};
    return r;
}

std::filesystem::path path() { return user_config::config_file("atalhos.json"); }

namespace {
std::string upper(const std::string& s) { return QString::fromStdString(s).toUpper().toStdString(); }
}  // namespace

Table load() {
    Table table = defaults();
    const std::filesystem::path file = path();
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return table;
    try {
        const Json data = Json::parse(read_text_file(file));
        auto it = data.find("atalhos");
        if (it == data.end() || !it->is_object()) return table;
        Table limpo;
        for (auto kv = it->begin(); kv != it->end(); ++kv) {
            if (kv.key().empty() || !kv.value().is_string() || kv.value().get<std::string>().empty()) continue;
            const std::string key = upper(kv.key());
            if (reserved().count(key)) continue;
            limpo[key] = kv.value().get<std::string>();
        }
        return limpo.empty() ? table : limpo;
    } catch (const std::exception&) {
        return table;
    }
}

void save(const Table& table) {
    const std::filesystem::path file = path();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    Json atalhos = Json::object();
    for (const auto& [k, v] : table) atalhos[upper(k)] = v;
    Json payload = Json::object();
    payload["version"] = 1;
    payload["atalhos"] = std::move(atalhos);
    write_text_file(file, dumps_pretty(payload));
}

std::optional<std::string> key_name(int key) {
    if (key == Qt::Key_Delete) return "DELETE";
    if (key == Qt::Key_Backspace) return "BACKSPACE";
    if (key == Qt::Key_Space) return "SPACE";
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return std::string(1, static_cast<char>(key));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return std::string(1, static_cast<char>(key));
    return std::nullopt;
}

std::optional<std::string> action_for(int key, const Table* table) {
    const auto nome = key_name(key);
    if (!nome) return std::nullopt;
    const Table loaded = table ? Table() : load();
    const Table& t = table ? *table : loaded;
    auto it = t.find(*nome);
    if (it == t.end()) return std::nullopt;
    return it->second;
}

}  // namespace cnc::shortcuts
