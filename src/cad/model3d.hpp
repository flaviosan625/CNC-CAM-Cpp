#pragma once

#include <string>
#include <vector>

#include "cad/mesh.hpp"
#include "cad/relief.hpp"

namespace cnc::cad::model3d {

using Mesh = cad::Mesh;

enum class Profile { Rectangle, Circle, Triangle, Hexagon };
enum class EndCondition {
    Blind, ThroughAll, ThroughAllBoth, MidPlane, UpToNext,
    UpToVertex, UpToSurface, OffsetFromSurface, UpToBody
};

struct Extrusion {
    std::string name;
    Profile profile = Profile::Rectangle;
    double width = 100.0;
    double height = 60.0;
    double start_z = 0.0;
    double depth = 18.0;
    double second_depth = 0.0;
    double target_z = 0.0;
    double offset = 0.0;
    double draft_degrees = 0.0;
    EndCondition end = EndCondition::Blind;
    bool reverse = false;
    bool second_direction = false;
    bool suppressed = false;
};

struct Part {
    std::string name = "Peça 1";
    std::vector<Extrusion> features;
    std::vector<relief::ReliefComponent> relief_components;
};

Mesh build_mesh(const Part& part);
bool save_part(const Part& part, const std::string& path, std::string* error = nullptr);
bool load_part(Part& part, const std::string& path, std::string* error = nullptr);

} // namespace cnc::cad::model3d
