#include "cad/entity_json.hpp"

namespace cnc::cad {

namespace {

Json opt_string(const std::optional<std::string>& v) {
    return v ? Json(*v) : Json(nullptr);
}

std::optional<std::string> opt_string_from(const Json& d, const char* key) {
    auto it = d.find(key);
    if (it == d.end() || it->is_null()) return std::nullopt;
    return it->get<std::string>();
}

}  // namespace

Json dimension_style_to_json(const DimensionStyle& s) {
    Json d = Json::object();
    d["line_color"] = opt_string(s.line_color);
    d["extension_color"] = opt_string(s.extension_color);
    d["text_color"] = opt_string(s.text_color);
    d["text_height"] = s.text_height;
    d["arrow_size"] = s.arrow_size;
    d["line_width"] = s.line_width;
    d["extension_gap"] = s.extension_gap;
    d["extension_overshoot"] = s.extension_overshoot;
    d["text_gap"] = s.text_gap;
    d["arrow_style"] = s.arrow_style;
    d["arrow_fit"] = s.arrow_fit;
    d["precision"] = s.precision;
    d["show_unit"] = s.show_unit;
    d["alternate_units"] = s.alternate_units;
    d["alternate_precision"] = s.alternate_precision;
    d["tolerance_mode"] = s.tolerance_mode;
    d["tolerance_upper"] = s.tolerance_upper;
    d["tolerance_lower"] = s.tolerance_lower;
    return d;
}

DimensionStyle dimension_style_from_json(const Json& data) {
    DimensionStyle s;
    if (!data.is_object()) return s;
    s.line_color = opt_string_from(data, "line_color");
    s.extension_color = opt_string_from(data, "extension_color");
    s.text_color = opt_string_from(data, "text_color");
    s.text_height = jget<double>(data, "text_height", s.text_height);
    s.arrow_size = jget<double>(data, "arrow_size", s.arrow_size);
    s.line_width = jget<double>(data, "line_width", s.line_width);
    s.extension_gap = jget<double>(data, "extension_gap", s.extension_gap);
    s.extension_overshoot = jget<double>(data, "extension_overshoot", s.extension_overshoot);
    s.text_gap = jget<double>(data, "text_gap", s.text_gap);
    s.arrow_style = jget<std::string>(data, "arrow_style", s.arrow_style);
    s.arrow_fit = jget<std::string>(data, "arrow_fit", s.arrow_fit);
    s.precision = jget<int>(data, "precision", s.precision);
    s.show_unit = jget<bool>(data, "show_unit", s.show_unit);
    s.alternate_units = jget<bool>(data, "alternate_units", s.alternate_units);
    s.alternate_precision = jget<int>(data, "alternate_precision", s.alternate_precision);
    s.tolerance_mode = jget<std::string>(data, "tolerance_mode", s.tolerance_mode);
    s.tolerance_upper = jget<double>(data, "tolerance_upper", s.tolerance_upper);
    s.tolerance_lower = jget<double>(data, "tolerance_lower", s.tolerance_lower);
    return s;
}

Json entity_to_json(const Entity& e) {
    Json d = Json::object();
    d["type"] = e.type_name();
    switch (e.kind()) {
        case EntityKind::ConstructionLine: {
            const auto& l = static_cast<const ConstructionLine&>(e);
            d["x1"] = l.x1; d["y1"] = l.y1; d["x2"] = l.x2; d["y2"] = l.y2;
            d["machinable"] = l.machinable;
            break;
        }
        case EntityKind::DrillPoint: {
            const auto& p = static_cast<const DrillPoint&>(e);
            d["x"] = p.x; d["y"] = p.y;
            break;
        }
        case EntityKind::Line: {
            const auto& l = static_cast<const Line&>(e);
            d["x1"] = l.x1; d["y1"] = l.y1; d["x2"] = l.x2; d["y2"] = l.y2;
            break;
        }
        case EntityKind::Circle: {
            const auto& c = static_cast<const Circle&>(e);
            d["cx"] = c.cx; d["cy"] = c.cy; d["radius"] = c.radius;
            break;
        }
        case EntityKind::Ellipse: {
            const auto& c = static_cast<const Ellipse&>(e);
            d["cx"] = c.cx; d["cy"] = c.cy; d["rx"] = c.rx; d["ry"] = c.ry;
            d["rotation"] = c.rotation;
            break;
        }
        case EntityKind::Arc: {
            const auto& a = static_cast<const Arc&>(e);
            d["cx"] = a.cx; d["cy"] = a.cy; d["radius"] = a.radius;
            d["start_angle"] = a.start_angle; d["end_angle"] = a.end_angle;
            break;
        }
        case EntityKind::Rectangle: {
            const auto& r = static_cast<const Rectangle&>(e);
            d["x"] = r.x; d["y"] = r.y; d["width"] = r.width; d["height"] = r.height;
            d["rotation"] = r.rotation; d["corner_radius"] = r.corner_radius;
            break;
        }
        case EntityKind::Text: {
            const auto& t = static_cast<const Text&>(e);
            d["x"] = t.x; d["y"] = t.y; d["content"] = t.content; d["height"] = t.height;
            d["font_family"] = t.font_family; d["bold"] = t.bold; d["italic"] = t.italic;
            d["align"] = t.align; d["rotation"] = t.rotation;
            break;
        }
        case EntityKind::Dimension: {
            const auto& m = static_cast<const Dimension&>(e);
            d["x1"] = m.x1; d["y1"] = m.y1; d["x2"] = m.x2; d["y2"] = m.y2;
            d["x3"] = m.x3; d["y3"] = m.y3; d["offset"] = m.offset; d["kind"] = m.kind_name;
            d["source_id"] = opt_string(m.source_id);
            d["style"] = dimension_style_to_json(m.style);
            break;
        }
        case EntityKind::Polyline: {
            const auto& p = static_cast<const Polyline&>(e);
            d["points"] = points_to_json(p.points);
            d["is_closed"] = p.is_closed;
            break;
        }
    }
    // O id vai para o arquivo: e por ele que os percursos reencontram seus vetores ao
    // reabrir o projeto.
    d["id"] = e.id;
    d["layer"] = e.layer;
    d["color"] = rgb_to_json(e.color);
    d["group"] = opt_string(e.group);
    return d;
}

EntityPtr entity_from_json(const Json& d, const std::string& default_layer) {
    try {
        const std::string type = d.at("type").get<std::string>();
        EntityPtr e;
        if (type == "Line") {
            auto l = std::make_shared<Line>();
            l->x1 = d.at("x1").get<double>(); l->y1 = d.at("y1").get<double>();
            l->x2 = d.at("x2").get<double>(); l->y2 = d.at("y2").get<double>();
            e = l;
        } else if (type == "ConstructionLine") {
            auto l = std::make_shared<ConstructionLine>();
            l->x1 = d.at("x1").get<double>(); l->y1 = d.at("y1").get<double>();
            l->x2 = d.at("x2").get<double>(); l->y2 = d.at("y2").get<double>();
            l->machinable = jget<bool>(d, "machinable", false);
            e = l;
        } else if (type == "DrillPoint") {
            auto p = std::make_shared<DrillPoint>();
            p->x = d.at("x").get<double>(); p->y = d.at("y").get<double>();
            e = p;
        } else if (type == "Circle") {
            auto c = std::make_shared<Circle>();
            c->cx = d.at("cx").get<double>(); c->cy = d.at("cy").get<double>();
            c->radius = d.at("radius").get<double>();
            e = c;
        } else if (type == "Ellipse") {
            auto c = std::make_shared<Ellipse>();
            c->cx = d.at("cx").get<double>(); c->cy = d.at("cy").get<double>();
            c->rx = d.at("rx").get<double>(); c->ry = d.at("ry").get<double>();
            c->rotation = jget<double>(d, "rotation", 0.0);
            e = c;
        } else if (type == "Arc") {
            auto a = std::make_shared<Arc>();
            a->cx = d.at("cx").get<double>(); a->cy = d.at("cy").get<double>();
            a->radius = d.at("radius").get<double>();
            a->start_angle = d.at("start_angle").get<double>();
            a->end_angle = d.at("end_angle").get<double>();
            e = a;
        } else if (type == "Rectangle") {
            auto r = std::make_shared<Rectangle>();
            r->x = d.at("x").get<double>(); r->y = d.at("y").get<double>();
            r->width = d.at("width").get<double>(); r->height = d.at("height").get<double>();
            r->rotation = jget<double>(d, "rotation", 0.0);
            r->corner_radius = jget<double>(d, "corner_radius", 0.0);
            e = r;
        } else if (type == "Text") {
            auto t = std::make_shared<Text>();
            t->x = d.at("x").get<double>(); t->y = d.at("y").get<double>();
            t->content = jget<std::string>(d, "content", "");
            t->height = jget<double>(d, "height", 20.0);
            t->font_family = jget<std::string>(d, "font_family", "Arial");
            t->bold = jget<bool>(d, "bold", false);
            t->italic = jget<bool>(d, "italic", false);
            t->align = jget<std::string>(d, "align", "left");
            t->rotation = jget<double>(d, "rotation", 0.0);
            e = t;
        } else if (type == "Dimension") {
            auto m = std::make_shared<Dimension>();
            m->x1 = d.at("x1").get<double>(); m->y1 = d.at("y1").get<double>();
            m->x2 = d.at("x2").get<double>(); m->y2 = d.at("y2").get<double>();
            m->x3 = jget<double>(d, "x3", 0.0); m->y3 = jget<double>(d, "y3", 0.0);
            m->offset = jget<double>(d, "offset", 15.0);
            m->kind_name = jget<std::string>(d, "kind", "aligned");
            m->source_id = opt_string_from(d, "source_id");
            m->style = dimension_style_from_json(d.contains("style") ? d.at("style") : Json());
            e = m;
        } else if (type == "Polyline") {
            auto p = std::make_shared<Polyline>();
            p->points = points_from_json(d.at("points"));
            // is_closed, com "closed" como nome antigo
            p->is_closed = jget<bool>(d, "is_closed", jget<bool>(d, "closed", false));
            e = p;
        } else {
            return nullptr;  // tipo desconhecido
        }

        if (auto it = d.find("id"); it != d.end() && it->is_string() && !it->get<std::string>().empty()) {
            e->id = it->get<std::string>();
        }
        e->layer = jget<std::string>(d, "layer", default_layer);
        e->color = rgb_from_json(d.contains("color") ? d.at("color") : Json(), kDefaultColor);
        e->group = opt_string_from(d, "group");
        return e;
    } catch (const nlohmann::json::exception&) {
        return nullptr;  // KeyError / TypeError no Python: a entidade e ignorada
    }
}

}  // namespace cnc::cad
