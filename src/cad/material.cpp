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

const std::vector<SheetPreset>& standard_sheet_presets() {
    static const std::vector<SheetPreset> kPresets = {
        {"MDF", "MDF — Chapa padrão (1850 × 2750 mm)", 1850.0, 2750.0, 15.0, 6.0, "Padrão de fábrica nacional (Arauco, Duratex, Berneck)"},
        {"PVC", "PVC Expandido (1220 × 2440 mm)", 1220.0, 2440.0, 10.0, 4.0, "Chapa inteira padrão comunicação visual e corte rápido"},
        {"Acrílico", "Acrílico Cristal / Cast (1000 × 2000 mm)", 1000.0, 2000.0, 4.0, 6.0, "Passada curta para acabamento limpo sem derretimento"},
        {"ACM", "ACM Alumínio Composto (1220 × 2440 mm)", 1220.0, 2440.0, 3.0, 4.0, "Painel compósito padrão para fachadas e totens"},
        {"PS", "PS Poliestireno (1000 × 2000 mm)", 1000.0, 2000.0, 2.0, 2.0, "Chapa fina para corte com fresa 2mm"},
        {"Policarbonato", "Policarbonato Compacto (1000 × 2000 mm)", 1000.0, 2000.0, 4.0, 4.0, "Alta resistência ao impacto"},
        {"Compensado", "Compensado Naval / Pinus (1220 × 2440 mm)", 1220.0, 2440.0, 15.0, 6.0, "Madeira multilaminada padrão marcenaria"},
        {"Alumínio", "Chapa de Alumínio (1000 × 2000 mm)", 1000.0, 2000.0, 2.0, 4.0, "Liga naval / estrutural usinável"},
    };
    return kPresets;
}

namespace {
std::string clean_key(const std::string& text) {
    std::string out;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) out.push_back(static_cast<char>(std::tolower(uc)));
    }
    return out;
}
}  // namespace

const SheetPreset* find_sheet_preset(const std::string& material_name) {
    const std::string key = clean_key(material_name);
    if (key.empty()) return nullptr;

    const auto& presets = standard_sheet_presets();
    // 1. Exact or starts-with match
    for (const auto& p : presets) {
        const std::string pkey = clean_key(p.name);
        if (key == pkey || key.find(pkey) != std::string::npos || pkey.find(key) != std::string::npos) {
            return &p;
        }
    }
    // 2. Specific aliases
    if (key.find("acril") != std::string::npos) {
        for (const auto& p : presets) if (p.name == "Acrílico") return &p;
    }
    if (key.find("madeira") != std::string::npos || key.find("compensad") != std::string::npos) {
        for (const auto& p : presets) if (p.name == "Compensado") return &p;
    }
    if (key.find("alumin") != std::string::npos) {
        for (const auto& p : presets) if (p.name == "Alumínio") return &p;
    }
    return nullptr;
}

}  // namespace cnc::cad
