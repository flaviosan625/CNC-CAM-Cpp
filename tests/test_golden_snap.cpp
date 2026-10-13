// Snap: o ponto que o ima escolhe tem de ser o do app em Python.
#include "cad/document.hpp"
#include "golden.hpp"
#include "viewport/snapping.hpp"

using namespace cnc;
using namespace cnc::vp;
using testing::near;

namespace {

SnapKind kind_of(const std::string& s) {
    static const std::map<std::string, SnapKind> table = {
        {"node", SnapKind::Node},         {"center", SnapKind::Center},   {"midpoint", SnapKind::Midpoint},
        {"intersection", SnapKind::Intersection}, {"on_span", SnapKind::OnSpan}, {"grid", SnapKind::Grid},
        {"material", SnapKind::Material}};
    return table.at(s);
}

const char* kind_name(SnapKind k) {
    switch (k) {
        case SnapKind::Node: return "node";
        case SnapKind::Center: return "center";
        case SnapKind::Midpoint: return "midpoint";
        case SnapKind::Intersection: return "intersection";
        case SnapKind::OnSpan: return "on_span";
        case SnapKind::Grid: return "grid";
        case SnapKind::Material: return "material";
    }
    return "";
}

// Compara um SnapResult (ou ausencia) com o JSON do Python.
void check_result(const std::optional<SnapResult>& got, const Json& want, const std::string& ctx) {
    INFO(ctx);
    if (want.is_null()) {
        CHECK_MESSAGE(!got.has_value(), "o Python nao achou ponto, o C++ achou");
        return;
    }
    REQUIRE_MESSAGE(got.has_value(), "o Python achou ponto, o C++ nao");
    CHECK(near(got->x, want.at("x").get<double>()));
    CHECK(near(got->y, want.at("y").get<double>()));
    CHECK(std::string(kind_name(got->kind)) == want.at("kind").get<std::string>());
    CHECK(near(got->distance, want.at("distance").get<double>()));
    if (want.contains("entity")) {
        if (want.at("entity").is_null()) {
            CHECK(!got->entity);
        } else {
            REQUIRE(got->entity);
            CHECK(got->entity->id == want.at("entity").get<std::string>());
        }
    }
}

}  // namespace

TEST_CASE("golden: snap igual ao Python") {
    if (!testing::golden_exists("snap.json")) return;
    const Json golden = testing::load_golden("snap.json").at("cases").at(0);
    cad::Document doc;
    doc.from_json(golden.at("document"));
    const Json& data = golden.at("data");
    std::map<std::string, cad::EntityPtr> by_id;
    for (const auto& e : doc.entities) by_id[e->id] = e;

    SUBCASE("pontos notaveis das entidades") {
        for (auto it = data.at("entity_points").begin(); it != data.at("entity_points").end(); ++it) {
            INFO("entidade " << it.key());
            const auto got = entity_points(*by_id.at(it.key()));
            REQUIRE(got.size() == it.value().size());
            for (size_t i = 0; i < got.size(); ++i) {
                CHECK(near(got[i].first.x, it.value()[i][0][0].get<double>()));
                CHECK(near(got[i].first.y, it.value()[i][0][1].get<double>()));
                CHECK(std::string(kind_name(got[i].second)) == it.value()[i][1].get<std::string>());
            }
        }
    }

    SUBCASE("ponto mais proximo sobre o contorno") {
        for (auto it = data.at("closest_on_entity").begin(); it != data.at("closest_on_entity").end(); ++it) {
            for (const Json& c : it.value()) {
                INFO("entidade " << it.key() << " consulta " << c.at("q").dump());
                const auto got = closest_on_entity(*by_id.at(it.key()), c.at("q")[0].get<double>(), c.at("q")[1].get<double>());
                if (c.at("out").is_null()) {
                    CHECK(!got.has_value());
                } else {
                    REQUIRE(got.has_value());
                    CHECK(near(got->first.x, c.at("out")[0][0].get<double>()));
                    CHECK(near(got->first.y, c.at("out")[0][1].get<double>()));
                    CHECK(near(got->second, c.at("out")[1].get<double>()));
                }
            }
        }
    }

    SUBCASE("intersecoes entre vetores") {
        for (const Json& c : data.at("segment_intersections")) {
            const auto got = segment_intersections(*by_id.at(c.at("pair")[0].get<std::string>()),
                                                   *by_id.at(c.at("pair")[1].get<std::string>()));
            INFO("par " << c.at("pair").dump());
            REQUIRE(got.size() == c.at("out").size());
            for (size_t i = 0; i < got.size(); ++i) {
                CHECK(near(got[i].x, c.at("out")[i][0].get<double>()));
                CHECK(near(got[i].y, c.at("out")[i][1].get<double>()));
            }
        }
    }

    SUBCASE("snap do cursor, por configuracao") {
        for (auto it = data.at("snap").begin(); it != data.at("snap").end(); ++it) {
            SnapEngine eng;
            eng.document = &doc;
            // as variantes foram criadas no Python com estes ajustes
            SnapSettings s;
            const std::string& n = it.key();
            if (n == "so_nos") { s.centers = s.midpoints = s.intersections = s.on_span = false; }
            if (n == "com_grelha") { s.grid = true; s.grid_spacing = 10.0; }
            if (n == "sem_material") s.material = false;
            if (n == "raio_grande") s.radius_px = 40.0;
            if (n == "desligado") s.enabled = false;
            if (n == "sem_geometria") { s.geometry = false; s.grid = true; s.grid_spacing = 25.0; }
            eng.settings = s;
            for (const Json& c : it.value()) {
                std::set<std::string> exclude;
                if (c.contains("exclude")) for (const Json& x : c.at("exclude")) exclude.insert(x.get<std::string>());
                const auto got = eng.snap(c.at("q")[0].get<double>(), c.at("q")[1].get<double>(), c.at("ppm").get<double>(), exclude);
                check_result(got, c.at("out"), "config " + n + " consulta " + c.at("q").dump() + " ppm " + c.at("ppm").dump());
            }
        }
    }

    SUBCASE("ima ao mover") {
        SnapEngine eng;
        eng.document = &doc;
        auto run = [&](const char* key, const std::vector<cad::EntityPtr>& moving) {
            for (const Json& c : data.at(key)) {
                std::optional<Pt> anchor;
                if (c.contains("anchor") && !c.at("anchor").is_null()) {
                    anchor = Pt{c.at("anchor")[0].get<double>(), c.at("anchor")[1].get<double>()};
                }
                const double ppm = c.contains("ppm") ? c.at("ppm").get<double>() : 2.0;
                const MovingSnap r = eng.snap_moving(moving, c.at("dx").get<double>(), c.at("dy").get<double>(), ppm, anchor);
                INFO(key << " " << c.dump());
                CHECK(near(r.dx, c.at("out")[0].get<double>()));
                CHECK(near(r.dy, c.at("out")[1].get<double>()));
                check_result(r.hit, c.at("out")[2], std::string(key));
            }
        };
        run("snap_moving_circle", {by_id.at("c1")});
        run("snap_moving_group", {by_id.at("r2"), by_id.at("e1")});
    }

    SUBCASE("ajustes ida e volta") {
        SnapSettings s;
        s.radius_px = 7.0;
        s.grid = true;
        CHECK(s.to_json() == data.at("settings_roundtrip"));
        CHECK(SnapSettings::from_json(data.at("settings_roundtrip")).to_json() == data.at("settings_roundtrip"));
    }
}
