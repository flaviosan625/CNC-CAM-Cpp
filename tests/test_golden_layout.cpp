// Otimizador de disposicao: o plano (qual peca vai para onde) tem de ser o do Python.
#include "cad/document.hpp"
#include "cad/layout.hpp"
#include "golden.hpp"

using namespace cnc;
using testing::near;

namespace {

cad::LayoutOptions options_from(const Json& j) {
    cad::LayoutOptions o;
    o.tool_diameter = j.at("tool_diameter").get<double>();
    o.clearance = j.at("clearance").get<double>();
    o.margin = j.at("margin").get<double>();
    o.rotate = j.at("rotate").get<bool>();
    o.rotation_step = j.at("rotation_step").get<double>();
    o.mirror = j.at("mirror").get<bool>();
    o.origin = j.at("origin").get<std::string>();
    o.direction = j.at("direction").get<std::string>();
    if (!j.at("area").is_null()) {
        const Json& a = j.at("area");
        o.area = BBox{a[0].get<double>(), a[1].get<double>(), a[2].get<double>(), a[3].get<double>()};
    }
    return o;
}

}  // namespace

TEST_CASE("golden: disposicao na chapa igual ao Python") {
    if (!testing::golden_exists("layout.json")) return;
    const Json golden = testing::load_golden("layout.json");
    int checked = 0;
    for (const Json& c : golden.at("cases")) {
        const std::string name = c.at("name").get<std::string>();
        INFO("caso: " << name);
        cad::Document doc;
        doc.from_json(c.at("document"));
        const cad::LayoutOptions options = options_from(c.at("options"));

        // -- otimizador avancado
        const auto [placements, res] = cad::plan_layout_advanced(doc.entities, doc.material, options);
        const Json& adv = c.at("advanced");
        const Json& want = adv.at("placements");
        REQUIRE_MESSAGE(placements.size() == want.size(), "numero de pecas posicionadas");
        for (size_t i = 0; i < want.size(); ++i) {
            INFO("posicionamento " << i);
            std::vector<std::string> ids;
            for (const auto& e : placements[i].entities) ids.push_back(e->id);
            CHECK(ids == want[i].at("ids").get<std::vector<std::string>>());
            CHECK(near(placements[i].anchor_x, want[i].at("anchor_x").get<double>()));
            CHECK(near(placements[i].anchor_y, want[i].at("anchor_y").get<double>()));
            CHECK(near(placements[i].angle, want[i].at("angle").get<double>()));
            CHECK(placements[i].mirrored == want[i].at("mirrored").get<bool>());
            CHECK(near(placements[i].dx, want[i].at("dx").get<double>()));
            CHECK(near(placements[i].dy, want[i].at("dy").get<double>()));
        }
        const Json& wr = adv.at("resultado");
        CHECK(res.movidos == wr.at("movidos").get<int>());
        CHECK(res.nao_couberam == wr.at("nao_couberam").get<int>());
        CHECK(res.faixas == wr.at("faixas").get<int>());
        CHECK(res.nao_couberam_ids == wr.at("ids").get<std::vector<std::string>>());

        // -- pecas rejeitadas, em colunas fora da chapa
        const std::set<std::string> unfit(res.nao_couberam_ids.begin(), res.nao_couberam_ids.end());
        const auto outside = cad::plan_unfit_outside(doc.entities, unfit, doc.material, 8.0);
        const Json& wo = c.at("unfit_outside");
        CHECK(outside.size() == wo.size());
        for (auto it = wo.begin(); it != wo.end(); ++it) {
            auto found = outside.find(it.key());
            REQUIRE_MESSAGE(found != outside.end(), it.key());
            CHECK(near(found->second.first, it.value()[0].get<double>()));
            CHECK(near(found->second.second, it.value()[1].get<double>()));
        }

        // -- faixas simples
        const auto [movs, res2] = cad::plan_layout(doc.entities, doc.material, 8.0);
        const Json& simple = c.at("simple");
        const Json& wm = simple.at("movimentos");
        REQUIRE(movs.size() == wm.size());
        for (size_t i = 0; i < wm.size(); ++i) {
            CHECK(std::get<0>(movs[i])->id == wm[i][0].get<std::string>());
            CHECK(near(std::get<1>(movs[i]), wm[i][1].get<double>()));
            CHECK(near(std::get<2>(movs[i]), wm[i][2].get<double>()));
        }
        CHECK(res2.movidos == simple.at("resultado").at("movidos").get<int>());
        CHECK(res2.nao_couberam == simple.at("resultado").at("nao_couberam").get<int>());
        CHECK(res2.faixas == simple.at("resultado").at("faixas").get<int>());
        ++checked;
    }
    CHECK(checked >= 15);
}
