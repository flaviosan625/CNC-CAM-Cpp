#include <algorithm>
#include <array>
#include <cmath>

#include <doctest.h>

#include "cam/model.hpp"
#include "cam/tools.hpp"
#include "sim/heightmap.hpp"
#include "sim/playback.hpp"

using namespace cnc;

namespace {

struct Fixture {
    sim::ToolpathSimulation simulation;
    cam::Toolpath toolpath;
};

Fixture make_simulation(const cam::Tool* selected = nullptr) {
    cam::Toolpath toolpath;
    toolpath.name = "Perfil externo";
    toolpath.kind = cam::ToolpathType::Profile;
    toolpath.feed_rate = 1000;
    toolpath.plunge_rate = 300;
    toolpath.moves = {
        {cam::MoveKind::Rapid, 2, 2, 5},
        {cam::MoveKind::Plunge, 2, 2, -2},
        {cam::MoveKind::Cut, 7, 2, -2},
    };
    cam::Tool tool = selected ? *selected : cam::Tool{};
    sim::HeightmapConfig config{10, 10, 5, 1, 0.0};
    sim::ToolpathSimulation simulation({{toolpath, tool}}, config, -5);
    return {std::move(simulation), std::move(toolpath)};
}

}  // namespace

TEST_CASE("simulacao: estimativa usa avanços do percurso") {
    auto fixture = make_simulation();
    CHECK(fixture.simulation.total_seconds == doctest::Approx(fixture.toolpath.estimated_seconds()));
    CHECK(fixture.simulation.total_seconds == doctest::Approx(1.7));
}

TEST_CASE("simulacao: estimativa soma rapidos e pausas") {
    cam::Toolpath toolpath;
    toolpath.name = "Furacao";
    toolpath.kind = cam::ToolpathType::Drill;
    toolpath.moves = {
        {cam::MoveKind::Cut, 0, 0, 0},
        {cam::MoveKind::Rapid, 10, 0, 0},
        {cam::MoveKind::Dwell, 10, 0, 0, 2},
        {cam::MoveKind::Rapid, 20, 0, 0},
    };
    sim::ToolpathSimulation simulation({{toolpath, cam::Tool{}}}, {10, 10, 5, 1, 0.0}, -5);
    CHECK(toolpath.estimated_seconds() == doctest::Approx(2.15));
    CHECK(simulation.total_seconds == doctest::Approx(toolpath.estimated_seconds()));
}

TEST_CASE("simulacao: remove material apenas durante corte e termina") {
    auto fixture = make_simulation();
    auto& simulation = fixture.simulation;
    CHECK(std::all_of(simulation.heightmap.values.begin(), simulation.heightmap.values.end(),
                      [](float value) { return value == 0.0f; }));
    simulation.advance(1.5);
    CHECK(simulation.heightmap.at(2, 4) < 0);
    CHECK(simulation.heightmap.at(9, 9) == 0);
    CHECK_FALSE(simulation.finished());
    CHECK(simulation.advance(simulation.remaining_seconds()));
    CHECK(simulation.finished());
    CHECK(simulation.progress() == doctest::Approx(1.0));
}

TEST_CASE("simulacao: reiniciar restaura material e cursor") {
    auto fixture = make_simulation();
    auto& simulation = fixture.simulation;
    simulation.advance(simulation.total_seconds);
    simulation.reset();
    CHECK(std::all_of(simulation.heightmap.values.begin(), simulation.heightmap.values.end(),
                      [](float value) { return value == 0.0f; }));
    CHECK(simulation.progress() == doctest::Approx(0.0));
    CHECK_FALSE(simulation.finished());
}

TEST_CASE("simulacao: caminho muda de cor conforme a fresa avanca") {
    auto fixture = make_simulation();
    const auto future = fixture.simulation.path_vertices();
    const auto has_color = [](const std::vector<float>& vertices, std::array<float, 3> color) {
        for (size_t i = 3; i + 2 < vertices.size(); i += 6) {
            if (std::fabs(vertices[i] - color[0]) < 1e-6 &&
                std::fabs(vertices[i + 1] - color[1]) < 1e-6 &&
                std::fabs(vertices[i + 2] - color[2]) < 1e-6) return true;
        }
        return false;
    };
    CHECK(has_color(future, {0.31f, 0.76f, 0.97f}));
    fixture.simulation.advance(1.5);
    const auto current = fixture.simulation.path_vertices();
    CHECK(has_color(current, {0.20f, 0.88f, 0.52f}));
    CHECK(has_color(current, {1.0f, 0.58f, 0.16f}));
}

TEST_CASE("simulacao: malha tem indices validos") {
    auto fixture = make_simulation();
    const auto mesh = fixture.simulation.stock_mesh({0.8f, 0.7f, 0.5f});
    CHECK(mesh.vertices.size() == 121 * 6);
    CHECK(mesh.indices.size() == 600);
    CHECK(*std::max_element(mesh.indices.begin(), mesh.indices.end()) < mesh.vertices.size() / 6);
}

TEST_CASE("simulacao: geometrias das ferramentas diferem") {
    const sim::HeightmapConfig config{10, 10, 5, 1, 0.0};
    const std::array<double, 3> point{5.0, 5.0, -2.0};
    auto run = [&](cam::Tool tool) {
        auto heightmap = sim::create_heightmap(config);
        sim::remove_tool_segment(heightmap, point, point, tool, config, -5);
        return heightmap;
    };
    auto ball = run(cam::Tool{.tool_type = cam::ToolType::BallNose, .diameter = 4});
    auto vbit = run(cam::Tool{.tool_type = cam::ToolType::VBit, .diameter = 4, .v_angle = 90});
    auto drill = run(cam::Tool{.tool_type = cam::ToolType::Drill, .diameter = 4});
    CHECK(ball.at(5, 5) == -2);
    CHECK(ball.at(5, 7) == 0);
    CHECK(vbit.at(5, 5) == -2);
    CHECK(vbit.at(5, 6) > vbit.at(5, 5));
    CHECK(drill.at(5, 6) > drill.at(5, 5));
}
