#include "cad/material.hpp"

namespace cnc::cad {

Pt MaterialSetup::origin_xy() const {
    double x = 0.0, y = 0.0;
    if (zero_x == "center") x = width / 2;
    else if (zero_x == "right") x = width;
    if (zero_y == "center") y = height / 2;
    else if (zero_y == "top") y = height;
    if (use_origin_offset) {
        x += origin_offset_x;
        y += origin_offset_y;
    }
    return {x, y};
}

std::optional<BBox> MaterialSetup::margin_rect() const {
    const double m = margin;
    if (m <= 0 || 2 * m >= width || 2 * m >= height) return std::nullopt;
    return BBox{m, m, width - m, height - m};
}

int MaterialSetup::curve_segments() const {
    if (model_resolution == "high") return 128;
    if (model_resolution == "very-high") return 256;
    return 64;
}

// asdict(MaterialSetup): campos na ordem em que o dataclass os declara.
Json MaterialSetup::to_json() const {
    Json d = Json::object();
    d["width"] = width;
    d["height"] = height;
    d["thickness"] = thickness;
    d["material_profile"] = material_profile;
    d["zero_x"] = zero_x;
    d["zero_y"] = zero_y;
    d["zero_z"] = zero_z;
    d["units"] = units;
    d["use_origin_offset"] = use_origin_offset;
    d["origin_offset_x"] = origin_offset_x;
    d["origin_offset_y"] = origin_offset_y;
    d["scale_design"] = scale_design;
    d["model_resolution"] = model_resolution;
    d["appearance"] = appearance;
    d["solid_color"] = solid_color;
    d["margin"] = margin;
    d["clearance_z"] = clearance_z;
    d["plunge_gap"] = plunge_gap;
    d["home_x"] = home_x;
    d["home_y"] = home_y;
    return d;
}

MaterialSetup MaterialSetup::from_json(const Json& j, const std::string& fallback_units) {
    MaterialSetup m;
    m.width = jget<double>(j, "width", m.width);
    m.height = jget<double>(j, "height", m.height);
    m.thickness = jget<double>(j, "thickness", m.thickness);
    m.material_profile = jget<std::string>(j, "material_profile", m.material_profile);
    m.zero_x = jget<std::string>(j, "zero_x", m.zero_x);
    m.zero_y = jget<std::string>(j, "zero_y", m.zero_y);
    m.zero_z = jget<std::string>(j, "zero_z", m.zero_z);
    m.units = jget<std::string>(j, "units", fallback_units);
    m.use_origin_offset = jget<bool>(j, "use_origin_offset", m.use_origin_offset);
    m.origin_offset_x = jget<double>(j, "origin_offset_x", m.origin_offset_x);
    m.origin_offset_y = jget<double>(j, "origin_offset_y", m.origin_offset_y);
    m.scale_design = jget<bool>(j, "scale_design", m.scale_design);
    m.model_resolution = jget<std::string>(j, "model_resolution", m.model_resolution);
    m.appearance = jget<std::string>(j, "appearance", m.appearance);
    m.solid_color = jget<std::string>(j, "solid_color", m.solid_color);
    m.margin = jget<double>(j, "margin", m.margin);
    m.clearance_z = jget<double>(j, "clearance_z", m.clearance_z);
    m.plunge_gap = jget<double>(j, "plunge_gap", m.plunge_gap);
    m.home_x = jget<double>(j, "home_x", m.home_x);
    m.home_y = jget<double>(j, "home_y", m.home_y);
    return m;
}

}  // namespace cnc::cad
