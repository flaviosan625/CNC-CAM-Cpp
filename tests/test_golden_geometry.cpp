// cad/geometry: booleanas, offset, filete, aparar, estender e curva iguais ao Python.
#include <map>

#include "cad/document.hpp"
#include "cad/geometry.hpp"
#include "golden.hpp"

using namespace cnc;
using testing::near;

namespace {

using ById = std::map<std::string, cad::EntityPtr>;

std::vector<cad::EntityPtr> pick(const ById& by_id, const Json& ids) {
    std::vector<cad::EntityPtr> out;
    for (const Json& id : ids) out.push_back(by_id.at(id.get<std::string>()));
    return out;
}

void check_points(const Points& got, const Json& want, const std::string& ctx) {
    INFO(ctx);
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < got.size(); ++i) {
        CHECK(near(got[i].x, want[i][0].get<double>()));
        CHECK(near(got[i].y, want[i][1].get<double>()));
    }
}

void check_entities(const std::vector<cad::EntityPtr>& got, const Json& want, const std::string& ctx) {
    INFO(ctx);
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < got.size(); ++i) {
        const auto* poly = dynamic_cast<const cad::Polyline*>(got[i].get());
        REQUIRE(poly != nullptr);
        CHECK(poly->is_closed == want[i].at("closed").get<bool>());
        CHECK(poly->layer == want[i].at("layer").get<std::string>());
        for (int k = 0; k < 3; ++k) CHECK(near(poly->color[static_cast<size_t>(k)], want[i].at("color")[static_cast<size_t>(k)].get<double>()));
        check_points(poly->points, want[i].at("points"), ctx + " polilinha " + std::to_string(i));
    }
}

}  // namespace

TEST_CASE("golden: cad/geometry igual ao Python") {
    if (!testing::golden_exists("geometry.json")) return;
    const Json golden = testing::load_golden("geometry.json").at("cases").at(0);
    cad::Document doc;
    doc.from_json(golden.at("document"));
    const Json& data = golden.at("data");
    ById by_id;
    for (const auto& e : doc.entities) by_id[e->id] = e;

    SUBCASE("entidade -> GEOS -> entidades") {
        for (auto it = data.at("to_shapely").begin(); it != data.at("to_shapely").end(); ++it) {
            INFO("entidade " << it.key());
            const geo::Geom g = cad::to_shapely(*by_id.at(it.key()));
            if (it.value().is_null()) {
                CHECK(!g);
                continue;
            }
            REQUIRE(g);
            CHECK(g.geom_type() == it.value().at("type").get<std::string>());
            CHECK(g.is_empty() == it.value().at("empty").get<bool>());
            CHECK(near(g.area(), it.value().at("area").get<double>()));
            CHECK(near(g.length(), it.value().at("length").get<double>()));
            check_entities(cad::from_shapely(g), it.value().at("out"), "from_shapely " + it.key());
        }
    }

    SUBCASE("booleanas") {
        for (const Json& c : data.at("boolean")) {
            const auto ents = pick(by_id, c.at("ids"));
            check_entities(cad::boolean_union(ents), c.at("union"), "uniao " + c.at("ids").dump());
            check_entities(cad::boolean_intersect(ents), c.at("intersect"), "intersecao " + c.at("ids").dump());
        }
        for (const Json& c : data.at("subtract")) {
            check_entities(cad::boolean_subtract(pick(by_id, c.at("base")), pick(by_id, c.at("cut"))), c.at("out"),
                           "subtracao " + c.at("base").dump() + " - " + c.at("cut").dump());
        }
    }

    SUBCASE("offset") {
        for (const Json& c : data.at("offset")) {
            const std::string ctx = "offset " + c.at("id").get<std::string>() + " d=" + c.at("dist").dump() +
                                    " fora=" + c.at("outward").dump() + " " + c.at("join").get<std::string>();
            check_entities(cad::offset_entity(*by_id.at(c.at("id").get<std::string>()), c.at("dist").get<double>(),
                                              c.at("outward").get<bool>(), c.at("join").get<std::string>()),
                           c.at("out"), ctx);
        }
    }

    SUBCASE("filete") {
        for (const Json& c : data.at("fillet")) {
            Points pts;
            for (const Json& p : c.at("pts")) pts.push_back({p[0].get<double>(), p[1].get<double>()});
            const Points got = cad::fillet_corner(pts, c.at("idx").get<int>(), c.at("radius").get<double>(),
                                                  c.at("seg").get<int>(), c.at("closed").get<bool>());
            check_points(got, c.at("out"), "filete idx=" + c.at("idx").dump() + " r=" + c.at("radius").dump());
        }
    }

    SUBCASE("curva suave") {
        for (const Json& c : data.at("fit_curve")) {
            Points pts;
            for (const Json& p : c.at("pts")) pts.push_back({p[0].get<double>(), p[1].get<double>()});
            check_points(cad::fit_curve(pts, c.at("closed").get<bool>(), c.at("smooth").get<double>(), c.at("subdiv").get<int>()),
                         c.at("out"), "curva " + c.at("pts").dump());
        }
    }

    SUBCASE("unir vetores abertos") {
        for (const Json& c : data.at("join_open")) {
            check_entities(cad::join_open_vectors(pick(by_id, c.at("ids")), c.at("tol").get<double>()), c.at("out"),
                           "unir " + c.at("ids").dump());
        }
    }

    SUBCASE("estender linha") {
        for (const Json& c : data.at("extend")) {
            const auto src = std::dynamic_pointer_cast<cad::Line>(by_id.at(c.at("line").get<std::string>()));
            REQUIRE(src);
            cad::Line line;
            line.x1 = src->x1; line.y1 = src->y1; line.x2 = src->x2; line.y2 = src->y2;
            std::optional<std::string> endpoint;
            if (!c.at("endpoint").is_null()) endpoint = c.at("endpoint").get<std::string>();
            INFO("estender " << c.at("line").dump() << " em " << c.at("targets").dump() << " ponta " << c.at("endpoint").dump());
            CHECK(cad::extend_line_to(line, pick(by_id, c.at("targets")), endpoint) == c.at("ok").get<bool>());
            const Json& out = c.at("out");
            CHECK(near(line.x1, out[0].get<double>()));
            CHECK(near(line.y1, out[1].get<double>()));
            CHECK(near(line.x2, out[2].get<double>()));
            CHECK(near(line.y2, out[3].get<double>()));
        }
    }

    SUBCASE("aparar no clique") {
        for (const Json& c : data.at("trim")) {
            check_entities(cad::trim_at_point(*by_id.at(c.at("id").get<std::string>()), c.at("x").get<double>(),
                                              c.at("y").get<double>(), pick(by_id, c.at("others"))),
                           c.at("out"), "aparar " + c.at("id").dump() + " em (" + c.at("x").dump() + ", " + c.at("y").dump() + ")");
        }
    }

    SUBCASE("contorno / casco") {
        for (const Json& c : data.at("boundary")) {
            check_entities(cad::vector_boundary(pick(by_id, c.at("ids")), c.at("offset").get<double>(), c.at("rubber").get<bool>()),
                           c.at("out"), "contorno " + c.at("ids").dump() + " off=" + c.at("offset").dump());
        }
    }
}
