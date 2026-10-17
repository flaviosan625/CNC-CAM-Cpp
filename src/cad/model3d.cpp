#include "cad/model3d.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

namespace cnc::cad::model3d {
namespace {
using json = nlohmann::json;
constexpr double pi = 3.14159265358979323846;

std::vector<std::pair<double, double>> profile_points(const Extrusion& f) {
    const double w = std::max(0.01, f.width), h = std::max(0.01, f.height);
    if (f.profile == Profile::Circle || f.profile == Profile::Hexagon) {
        const int n = f.profile == Profile::Circle ? 48 : 6;
        std::vector<std::pair<double, double>> p;
        p.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * pi * i / n;
            p.emplace_back(w * 0.5 * std::cos(a), h * 0.5 * std::sin(a));
        }
        return p;
    }
    if (f.profile == Profile::Triangle)
        return {{-w * .5, -h / 3.0}, {w * .5, -h / 3.0}, {0.0, h * 2.0 / 3.0}};
    return {{-w*.5,-h*.5},{w*.5,-h*.5},{w*.5,h*.5},{-w*.5,h*.5}};
}

double resolved_depth(const Part& part, const Extrusion& f) {
    if (f.end == EndCondition::UpToVertex || f.end == EndCondition::UpToSurface ||
        f.end == EndCondition::UpToBody) return std::abs(f.target_z - f.start_z);
    if (f.end == EndCondition::OffsetFromSurface) return std::abs(f.target_z + f.offset - f.start_z);
    if (f.end == EndCondition::ThroughAll || f.end == EndCondition::ThroughAllBoth ||
        f.end == EndCondition::UpToNext) {
        double next = std::numeric_limits<double>::infinity();
        double farthest = 0.0;
        for (const auto& other : part.features) {
            if (&other >= &f || other.suppressed) continue;
            const double oz = other.start_z + std::max(other.depth, other.second_depth);
            farthest = std::max(farthest, std::abs(oz - f.start_z));
            if (oz > f.start_z + 1e-6) next = std::min(next, oz - f.start_z);
        }
        if (std::isfinite(next) && f.end == EndCondition::UpToNext) return next;
        return std::max(f.depth, farthest);
    }
    return std::max(0.01, f.depth);
}

void append_solid(Mesh& mesh, const Extrusion& f, double z0, double z1, double taper) {
    auto p = profile_points(f);
    const size_t n = p.size();
    if (n < 3 || std::abs(z1-z0) < 1e-7) return;
    const double cx = f.profile == Profile::Circle || f.profile == Profile::Hexagon ? 0.0 : 0.0;
    const double cy = 0.0;
    const double s0 = std::max(0.01, 1.0 - std::tan(taper * pi / 180.0) * 0.0);
    const double s1 = std::max(0.01, 1.0 - std::tan(taper * pi / 180.0) * std::abs(z1-z0) / std::max(f.width, f.height));
    const unsigned int base = static_cast<unsigned int>(mesh.vertices.size()/6);
    auto put = [&](double x, double y, double z, float r, float g, float b) {
        mesh.vertices.insert(mesh.vertices.end(), {static_cast<float>(x),static_cast<float>(y),static_cast<float>(z),r,g,b});
    };
    for (int layer=0; layer<2; ++layer) {
        const double scale = layer ? s1 : s0;
        const double z = layer ? z1 : z0;
        const float shade = layer ? 0.78f : 0.58f;
        for (const auto& [x,y] : p) put(cx+x*scale,cy+y*scale,z,0.29f*shade,0.62f*shade,0.88f*shade);
    }
    const unsigned int top = base + static_cast<unsigned int>(n);
    for (unsigned int i=1; i+1<n; ++i) {
        mesh.indices.insert(mesh.indices.end(), {base,base+i,base+i+1});
        mesh.indices.insert(mesh.indices.end(), {top,top+i+1,top+i});
    }
    for (unsigned int i=0; i<n; ++i) {
        const unsigned int j=(i+1)%static_cast<unsigned int>(n);
        mesh.indices.insert(mesh.indices.end(), {base+i,base+j,top+j,base+i,top+j,top+i});
    }
}

const char* profile_name(Profile p) {
    switch(p){case Profile::Rectangle:return "retangulo";case Profile::Circle:return "circulo";case Profile::Triangle:return "triangulo";case Profile::Hexagon:return "hexagono";} return "retangulo";
}
Profile parse_profile(const std::string& s) {
    if(s=="circulo")return Profile::Circle;if(s=="triangulo")return Profile::Triangle;if(s=="hexagono")return Profile::Hexagon;return Profile::Rectangle;
}
const char* end_name(EndCondition e) {
    switch(e){case EndCondition::Blind:return "cego";case EndCondition::ThroughAll:return "atraves_de_tudo";case EndCondition::ThroughAllBoth:return "atraves_de_tudo_ambos_lados";case EndCondition::MidPlane:return "plano_medio";case EndCondition::UpToNext:return "ate_proximo";case EndCondition::UpToVertex:return "ate_vertice";case EndCondition::UpToSurface:return "ate_superficie";case EndCondition::OffsetFromSurface:return "offset_da_superficie";case EndCondition::UpToBody:return "ate_corpo";} return "cego";
}
EndCondition parse_end(const std::string& s) {
    if(s=="atraves_de_tudo")return EndCondition::ThroughAll;if(s=="atraves_de_tudo_ambos_lados")return EndCondition::ThroughAllBoth;if(s=="plano_medio")return EndCondition::MidPlane;if(s=="ate_proximo")return EndCondition::UpToNext;if(s=="ate_vertice")return EndCondition::UpToVertex;if(s=="ate_superficie")return EndCondition::UpToSurface;if(s=="offset_da_superficie")return EndCondition::OffsetFromSurface;if(s=="ate_corpo")return EndCondition::UpToBody;if(s=="ate_cota")return EndCondition::UpToSurface;if(s=="offset_da_cota")return EndCondition::OffsetFromSurface;return EndCondition::Blind;
}
} // namespace

Mesh build_mesh(const Part& part) {
    Mesh mesh;
    double max_w = 100.0, max_h = 60.0, top_z = 0.0;
    bool has_extrusions = false;

    for (const auto& f : part.features) {
        if (f.suppressed) continue;
        has_extrusions = true;
        max_w = std::max(max_w, f.width);
        max_h = std::max(max_h, f.height);
        const double d = resolved_depth(part, f) * (f.reverse ? -1.0 : 1.0);
        top_z = std::max(top_z, f.start_z + (d > 0.0 ? d : 0.0));
        if (f.end == EndCondition::MidPlane || f.end == EndCondition::ThroughAllBoth) append_solid(mesh,f,f.start_z-d*.5,f.start_z+d*.5,f.draft_degrees);
        else {
            append_solid(mesh,f,f.start_z,f.start_z+d,f.draft_degrees);
            if (f.second_direction && f.second_depth > 0.0)
                append_solid(mesh,f,f.start_z,f.start_z-f.second_depth,f.draft_degrees);
        }
    }

    if (!part.relief_components.empty()) {
        for (const auto& rc : part.relief_components) {
            if (!rc.visible) continue;
            max_w = std::max(max_w, rc.pos_x + rc.width);
            max_h = std::max(max_h, rc.pos_y + rc.height);
        }
        relief::LocalGrid composite;
        relief::ReliefEngine::compose(part.relief_components, max_w, max_h, 0.5, composite);
        if (!composite.empty()) {
            const auto relief_mesh = relief::ReliefEngine::generate_mesh(composite, top_z);
            const unsigned int v_offset = static_cast<unsigned int>(mesh.vertices.size() / 6);
            mesh.vertices.insert(mesh.vertices.end(), relief_mesh.vertices.begin(), relief_mesh.vertices.end());
            for (auto idx : relief_mesh.indices) {
                mesh.indices.push_back(idx + v_offset);
            }
        }
    }

    return mesh;
}

bool save_part(const Part& part, const std::string& path, std::string* error) {
    try {
        json j; j["format"]="cnccam-part-v1"; j["name"]=part.name; j["features"]=json::array();
        for(const auto& f:part.features) j["features"].push_back({{"name",f.name},{"profile",profile_name(f.profile)},{"width",f.width},{"height",f.height},{"start_z",f.start_z},{"depth",f.depth},{"second_depth",f.second_depth},{"target_z",f.target_z},{"offset",f.offset},{"draft",f.draft_degrees},{"end",end_name(f.end)},{"reverse",f.reverse},{"second_direction",f.second_direction},{"suppressed",f.suppressed}});
        j["relief_components"] = json::array();
        for (const auto& rc : part.relief_components) {
            j["relief_components"].push_back(rc.to_json());
        }
        std::ofstream out(path,std::ios::binary); if(!out){if(error)*error="Não foi possível criar o arquivo.";return false;} out<<j.dump(2); return static_cast<bool>(out);
    } catch(const std::exception& e){if(error)*error=e.what();return false;}
}

bool load_part(Part& part, const std::string& path, std::string* error) {
    try {
        std::ifstream in(path,std::ios::binary); if(!in){if(error)*error="Não foi possível abrir o arquivo.";return false;}
        json j; in>>j; if(j.value("format",std::string{})!="cnccam-part-v1"){if(error)*error="Formato de peça desconhecido.";return false;}
        Part candidate; candidate.name=j.value("name",std::string("Peça 1"));
        for(const auto& x:j.at("features")){
            Extrusion f; f.name=x.value("name",std::string("Extrusão")); f.profile=parse_profile(x.value("profile",std::string("retangulo")));
            f.width=x.value("width",100.0);f.height=x.value("height",60.0);f.start_z=x.value("start_z",0.0);f.depth=x.value("depth",18.0);f.second_depth=x.value("second_depth",0.0);f.target_z=x.value("target_z",0.0);f.offset=x.value("offset",0.0);f.draft_degrees=x.value("draft",0.0);f.end=parse_end(x.value("end",std::string("cego")));f.reverse=x.value("reverse",false);f.second_direction=x.value("second_direction",false);f.suppressed=x.value("suppressed",false);candidate.features.push_back(std::move(f));
        }
        if (j.contains("relief_components") && j["relief_components"].is_array()) {
            for (const auto& rc_json : j["relief_components"]) {
                candidate.relief_components.push_back(relief::ReliefComponent::from_json(rc_json));
            }
        }
        part=std::move(candidate);return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
} // namespace cnc::cad::model3d
