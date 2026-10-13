// Funcoes utilitarias do CAM/CAD conferidas contra o app em Python (golden "misc").
#include <regex>

#include "cad/doc_vars.hpp"
#include "cad/document.hpp"
#include "cam/job.hpp"
#include "cam/model.hpp"
#include "cam/presets.hpp"
#include "cam/profile.hpp"
#include "cam/setup_sheet.hpp"
#include "cam/templates.hpp"
#include "cam/tools.hpp"
#include "cam/vectric_tools.hpp"
#include "golden.hpp"

using namespace cnc;
using namespace cnc::cam;
using testing::near;

namespace {

const Json& misc() {
    static const Json data = [] {
        if (!testing::golden_exists("misc.json")) return Json();
        return testing::load_golden("misc.json").at("cases").at(0).at("data");
    }();
    return data;
}

Points pts_of(const Json& arr) { return points_from_json(arr); }

bool same_points(const Points& a, const Points& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!near(a[i].x, b[i].x) || !near(a[i].y, b[i].y)) return false;
    }
    return true;
}

bool same_doubles(const std::vector<double>& a, const Json& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!near(a[i], b[i].get<double>())) return false;
    }
    return true;
}

std::string mask_date(const std::string& text) {
    static const std::regex re(R"(\d{2}/\d{2}/\d{4} \d{2}:\d{2})");
    return std::regex_replace(text, re, "DATA");
}

}  // namespace

#define NEED_MISC()                  \
    if (misc().is_null()) return;    \
    const Json& m = misc()

TEST_CASE("misc: compute_passes e fmt_time") {
    NEED_MISC();
    for (const Json& c : m.at("compute_passes")) {
        const auto& a = c.at("args");
        INFO("args: " << a.dump());
        CHECK(same_doubles(compute_passes(a[0].get<double>(), a[1].get<double>(), a[2].get<bool>(), a[3].get<double>()),
                           c.at("out")));
    }
    for (const Json& c : m.at("fmt_time")) {
        INFO("minutos: " << c.at("minutes").get<double>());
        CHECK(fmt_time(c.at("minutes").get<double>()) == c.at("out").get<std::string>());
    }
}

TEST_CASE("misc: utilidades de polilinha") {
    NEED_MISC();
    const Json& u = m.at("polyline_utils");
    const Points sq = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const Points cw(sq.rbegin(), sq.rend());
    const Points path = {{0, 0}, {10, 0}, {10, 5}, {3, 5}};
    CHECK(near(signed_area(sq), u.at("signed_area")[0].get<double>()));
    CHECK(near(signed_area(cw), u.at("signed_area")[1].get<double>()));
    CHECK(is_ccw(sq) == u.at("is_ccw")[0].get<bool>());
    CHECK(is_ccw(cw) == u.at("is_ccw")[1].get<bool>());
    CHECK(same_points(close_ring(sq), pts_of(u.at("close_ring"))));
    CHECK(near(path_length(path), u.at("path_length").get<double>()));

    const std::vector<Pt> starts = {{9, 9}, {0.1, 9.5}, {5, -2}};
    for (size_t i = 0; i < starts.size(); ++i) {
        CHECK(same_points(rotate_to_start(close_ring(sq), starts[i]), pts_of(u.at("rotate_to_start")[i])));
    }
    CHECK(same_points(resample(path, {1.0, 12.0, 17.5, 40.0, 0.0}), pts_of(u.at("resample"))));
    const std::vector<double> dists = {0.0, 4.0, 10.0, 16.0, 99.0};
    for (size_t i = 0; i < dists.size(); ++i) {
        const Pt p = point_at(path, dists[i]);
        CHECK(near(p.x, u.at("point_at")[i][0].get<double>()));
        CHECK(near(p.y, u.at("point_at")[i][1].get<double>()));
    }
    const std::vector<std::pair<double, double>> ranges = {{0, 6}, {5, 15}, {12, 22}, {0, 22}};
    for (size_t i = 0; i < ranges.size(); ++i) {
        CHECK(same_points(slice_path(path, ranges[i].first, ranges[i].second), pts_of(u.at("slice_path")[i])));
    }
    const Pt t0 = tangent_at_start(path), t1 = tangent_at_start({{1, 1}, {1, 1}, {1, 3}});
    CHECK(near(t0.x, u.at("tangent_at_start")[0][0].get<double>()));
    CHECK(near(t1.y, u.at("tangent_at_start")[1][1].get<double>()));
    for (const Json& c : u.at("travel_ccw")) {
        CHECK(travel_ccw(c[0].get<bool>(), from_str<CutDirection>(c[1].get<std::string>()).value()) == c[2].get<bool>());
    }
}

TEST_CASE("misc: ordem de usinagem") {
    NEED_MISC();
    std::vector<Points> loops;
    for (auto [x, y] : std::vector<std::pair<double, double>>{{100, 10}, {10, 100}, {50, 50}, {10, 10}, {110, 110}, {60, 5}, {50, 50.0}}) {
        loops.push_back({{x, y}, {x + 5, y}, {x + 5, y + 5}});
    }
    for (const Json& c : m.at("order_loops")) {
        const auto order = from_str<MachineOrder>(c.at("order").get<std::string>()).value();
        INFO("ordem: " << c.at("order").get<std::string>() << " faixa " << c.at("band").get<double>());
        CHECK(order_loops(loops, order, c.at("band").get<double>()) == c.at("out").get<std::vector<int>>());
    }
    for (const Json& c : m.at("best_order")) {
        std::vector<MachineOrder> orders;
        for (const Json& o : c.at("orders")) orders.push_back(from_str<MachineOrder>(o.get<std::string>()).value());
        const auto [seq, winner] = best_order(loops, orders);
        CHECK(seq == c.at("out").get<std::vector<int>>());
        CHECK(std::string(to_str(winner)) == c.at("winner").get<std::string>());
    }
    std::vector<int> all(loops.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int>(i);
    CHECK(near(travel_length(loops, all), m.at("travel_length")[0].get<double>()));
    CHECK(near(travel_length(loops, {3, 0, 2}), m.at("travel_length")[1].get<double>()));
}

TEST_CASE("misc: estatisticas do percurso") {
    NEED_MISC();
    Toolpath tp;
    tp.name = "t";
    tp.feed_rate = 1000;
    tp.plunge_rate = 300;
    tp.moves = {{MoveKind::Rapid, 0, 0, 5}, {MoveKind::Plunge, 0, 0, -3}, {MoveKind::Cut, 50, 0, -3},
                {MoveKind::Dwell, 50, 0, -3, 0.5}, {MoveKind::Cut, 50, 40, -3}, {MoveKind::Rapid, 50, 40, 5}};
    const Json& s = m.at("toolpath_stats");
    const BBox b = *tp.bounds();
    CHECK(near(b.x1, s.at("bounds")[2].get<double>()));
    CHECK(near(tp.depth_range().first, s.at("depth_range")[0].get<double>()));
    CHECK(near(tp.lengths().first, s.at("lengths")[0].get<double>()));
    CHECK(near(tp.lengths().second, s.at("lengths")[1].get<double>()));
    CHECK(near(tp.estimated_seconds(), s.at("seconds").get<double>()));
    CHECK(near(tp.estimated_seconds(20000.0), s.at("seconds_fast").get<double>()));
    CHECK(tp.summary() == s.at("summary").get<std::string>());
}

TEST_CASE("misc: banco de ferramentas padrao e metodos da Tool") {
    NEED_MISC();
    const std::vector<Tool> tools = default_tools();
    const Json& want = m.at("default_tools");
    REQUIRE(tools.size() == want.size());
    for (size_t i = 0; i < tools.size(); ++i) CHECK_MESSAGE(tools[i].to_json() == want[i], tools[i].name);

    for (const Json& c : m.at("tool_methods")) {
        const Tool t = Tool::from_json(c.at("tool"));
        INFO("ferramenta: " << t.name);
        CHECK(near(t.radius(), c.at("radius").get<double>()));
        CHECK(near(t.stepover_percent(), c.at("stepover_percent").get<double>()));
        CHECK(near(t.chip_load(), c.at("chip_load").get<double>()));
        CHECK(t.needs_tails() == c.at("needs_tails").get<bool>());
        CHECK(t.describe() == c.at("describe").get<std::string>());
        if (c.at("max_v_depth").is_null()) {
            CHECK(std::isinf(t.max_v_depth()));
        } else {
            CHECK(near(t.max_v_depth(), c.at("max_v_depth").get<double>()));
        }
        const std::vector<double> depths = {0.0, 0.5, 1.0, 3.0, 10.0};
        for (size_t i = 0; i < depths.size(); ++i) CHECK(near(t.flat_width_at(depths[i]), c.at("flat_width")[i].get<double>()));
    }

    ToolDatabase db;
    std::vector<std::string> names;
    for (const auto& t : db.tools) names.push_back(t->name);
    CHECK(names == m.at("db_names").get<std::vector<std::string>>());
    Tool dup;
    dup.name = db.tools[0]->name;
    CHECK(db.add(dup)->name == m.at("db_add_duplicate_name").get<std::string>());
}

TEST_CASE("misc: presets da casa e modelos") {
    NEED_MISC();
    const auto& presets = house_presets();
    const Json& want = m.at("house_presets");
    REQUIRE(presets.size() == want.size());
    for (size_t i = 0; i < presets.size(); ++i) {
        const MaterialPreset& p = presets[i];
        INFO("preset: " << p.name());
        CHECK(p.name() == want[i].at("name").get<std::string>());
        CHECK(p.describe() == want[i].at("describe").get<std::string>());
        CHECK(near(p.cut_depth(), want[i].at("cut_depth").get<double>()));
        CHECK(p.passes() == want[i].at("passes").get<int>());
        CHECK(p.tool().to_json() == want[i].at("tool"));
    }
    for (const Json& c : m.at("find_preset")) {
        const auto found = find_preset(c.at("q")[0].get<std::string>(), c.at("q")[1].get<double>());
        if (c.at("found").is_null()) {
            CHECK_FALSE(found);
        } else {
            REQUIRE(found);
            CHECK(found->name() == c.at("found").get<std::string>());
        }
    }
    for (const Json& c : m.at("for_thickness")) {
        std::vector<std::string> names;
        for (const auto& p : presets_for_thickness(c[0].get<double>())) names.push_back(p.name());
        CHECK(names == c[1].get<std::vector<std::string>>());
    }

    TemplateLibrary lib{std::filesystem::path("nao-grava.json")};
    seed(lib);
    const Json& tpls = m.at("house_templates");
    REQUIRE(lib.templates.size() == tpls.size());
    for (size_t i = 0; i < tpls.size(); ++i) {
        INFO("modelo: " << lib.templates[i]->name);
        CHECK(lib.templates[i]->to_json() == tpls[i]);
        const Json& meth = m.at("template_methods")[i];
        CHECK(lib.templates[i]->label() == meth.at("label").get<std::string>());
        CHECK(lib.templates[i]->describe() == meth.at("describe").get<std::string>());
        const std::vector<double> thick = {15.0, 18.0, 12.0};
        for (size_t k = 0; k < thick.size(); ++k) {
            cad::MaterialSetup mat;
            mat.thickness = thick[k];
            CHECK(lib.templates[i]->thickness_warning(mat) == meth.at("warning")[k].get<std::string>());
        }
        // ida e volta pelo JSON
        CHECK(ToolpathTemplate::from_json(tpls[i]).to_json() == tpls[i]);
    }
}

TEST_CASE("misc: variaveis do documento") {
    NEED_MISC();
    cad::Document doc;
    doc.material.thickness = 15.0;
    doc.material.width = 1220.5;
    doc.material.height = 2440.0;
    doc.variables.set("CLIENTE", "Joana Ñ");
    doc.variables.set("ESPESSURA", "99");
    const cad::OrderedStrMap vals = cad::all_variables(doc);
    const std::string date = *vals.get("DATA");

    const Json& want = m.at("doc_vars");
    for (auto it = want.at("values").begin(); it != want.at("values").end(); ++it) {
        const std::string* v = vals.get(it.key());
        REQUIRE(v);
        CHECK_MESSAGE(*v == it.value().get<std::string>(), it.key());
    }
    const std::vector<std::string> textos = {"Peça |CLIENTE| - |ESPESSURA| mm", "|trabalho| e |LARGURA|x|ALTURA|",
                                             "|MATERIAL| |DESCONHECIDO| ||", "sem nada", "|cliente|"};
    for (size_t i = 0; i < textos.size(); ++i) {
        std::string got = cad::expand(textos[i], vals);
        for (size_t p; (p = got.find(date)) != std::string::npos;) got.replace(p, date.size(), "DATA");
        CHECK_MESSAGE(got == want.at("expand")[i].get<std::string>(), textos[i]);
        CHECK(cad::tokens_in(textos[i]) == want.at("tokens")[i].get<std::vector<std::string>>());
    }
}

TEST_CASE("misc: importador do banco do Aspire 8.5 (arquivo real)") {
    NEED_MISC();
    if (!m.contains("vectric")) return;  // a maquina que gerou nao tinha o Aspire
    const std::filesystem::path db_file = default_aspire_tool_db();
    if (!std::filesystem::exists(db_file)) return;
    const Json& v = m.at("vectric");

    const VectricReadResult read = read_vectric_tools(db_file);
    CHECK(read.found == v.at("found").get<int>());
    CHECK(read.unsupported == v.at("unsupported").get<std::vector<std::string>>());
    REQUIRE(read.tools.size() == v.at("tools").size());
    for (size_t i = 0; i < read.tools.size(); ++i) {
        INFO("ferramenta importada: " << read.tools[i].name);
        CHECK(read.tools[i].to_json() == v.at("tools")[i]);
    }

    ToolDatabase base;
    const VectricImportReport rep = import_aspire_tools(base, db_file);
    CHECK(rep.found == v.at("report").at("found").get<int>());
    CHECK(rep.duplicates == v.at("report").at("duplicates").get<int>());
    REQUIRE(rep.imported.size() == v.at("report").at("imported").size());
    REQUIRE(base.tools.size() == v.at("db_after").size());
    for (size_t i = 0; i < base.tools.size(); ++i) CHECK(base.tools[i]->to_json() == v.at("db_after")[i]);
    MESSAGE("Aspire: " << read.tools.size() << " ferramentas conferidas com o Python");
}

TEST_CASE("misc: folha de trabalho em HTML") {
    NEED_MISC();
    cad::Document doc;
    doc.material.width = 600.0;  // o make_doc do script Python usa 600 x 400
    doc.material.height = 400.0;
    doc.material.thickness = 15.0;
    auto peca = std::make_shared<cad::Rectangle>();
    peca->id = "peca";
    peca->x = 0; peca->y = 0; peca->width = 100; peca->height = 60;
    auto furo = std::make_shared<cad::Circle>();
    furo->id = "furo";
    furo->cx = 50; furo->cy = 30; furo->radius = 10;
    doc.entities = {peca, furo};

    auto job1 = std::make_shared<ToolpathJob>();
    job1->id = "job1";
    job1->kind = ToolpathType::Profile;
    job1->tool.name = "Topo <6> & \"x\"";
    job1->tool.diameter = 6.0;
    job1->tool.tool_number = 3;
    ProfileSettings s1;
    s1.cut_depth = 16.0;
    s1.name = "Corte A&B";
    job1->settings = s1;
    job1->name = "Corte A&B";
    job1->entity_ids = {"peca", "furo"};
    auto job2 = std::make_shared<ToolpathJob>();
    job2->id = "job2";
    job2->kind = ToolpathType::Profile;
    job2->tool.name = "Fresa 2";
    job2->tool.diameter = 2.0;
    job2->tool.tool_number = 2;
    ProfileSettings s2;
    s2.cut_depth = 4.0;
    s2.name = "Gravar";
    job2->settings = s2;
    job2->name = "Gravar";
    job2->entity_ids = {"peca"};
    doc.toolpaths = {job1, job2};
    doc.recalculate_toolpaths();

    const auto tps = calculated_toolpaths(doc.toolpaths);
    const std::string html = mask_date(setup_sheet_html(tps, doc.material, "Peça <1> & cia"));
    const std::string want = m.at("setup_sheet_html").get<std::string>();
    CHECK_MESSAGE(html == want, testing::text_diff("html", html, want));
    const std::string vazio = mask_date(setup_sheet_html(std::vector<const Toolpath*>{}, doc.material, ""));
    const std::string want_vazio = m.at("setup_sheet_html_empty").get<std::string>();
    CHECK_MESSAGE(vazio == want_vazio, testing::text_diff("html_vazio", vazio, want_vazio));
}
