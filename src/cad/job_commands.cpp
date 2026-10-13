#include "cad/job_commands.hpp"

#include <cmath>

namespace cnc::cad {

std::vector<EntityPtr> scaled_entities(const std::vector<EntityPtr>& entities, double sx, double sy,
                                       int segments, double cx, double cy) {
    std::vector<EntityPtr> result;
    for (const EntityPtr& original : entities) {
        EntityPtr entity = original->copy();
        if (auto* circle = as<Circle>(entity); circle && !isclose(std::fabs(sx), std::fabs(sy))) {
            auto ellipse = std::make_shared<Ellipse>();
            ellipse->cx = circle->cx;
            ellipse->cy = circle->cy;
            ellipse->rx = circle->radius;
            ellipse->ry = circle->radius;
            ellipse->id = entity->id;
            ellipse->layer = entity->layer;
            ellipse->color = entity->color;
            ellipse->selected = entity->selected;
            ellipse->group = entity->group;
            ellipse->scale(sx, sy, cx, cy);
            result.push_back(ellipse);
            continue;
        }
        // Essas entidades nao representam uma transformacao nao uniforme exatamente. Preservamos o
        // contorno em vez de deformar os parametros.
        const bool nonuniform = !isclose(sx, sy);
        bool outline = false;
        if (nonuniform) {
            if (is_a<Circle>(entity) || is_a<Arc>(entity) || is_a<Text>(entity)) {
                outline = true;
            } else if (auto* el = as<Ellipse>(entity)) {
                outline = std::fabs(el->rotation) > 1e-9;
            } else if (auto* r = as<Rectangle>(entity)) {
                outline = std::fabs(r->rotation) > 1e-9 || r->corner_radius > 0;
            }
        }
        if (outline) {
            std::vector<Points> paths;
            if (auto* text = as<Text>(entity)) {
                paths = text->outline_polygons();
            } else {
                paths.push_back(entity->to_points(segments));
            }
            for (size_t index = 0; index < paths.size(); ++index) {
                auto poly = std::make_shared<Polyline>();
                poly->points = paths[index];
                poly->is_closed = entity->closed();
                poly->layer = entity->layer;
                poly->color = entity->color;
                poly->selected = entity->selected;
                poly->group = entity->group;
                if (index == 0) poly->id = entity->id;
                poly->scale(sx, sy, cx, cy);
                result.push_back(poly);
            }
        } else {
            entity->scale(sx, sy, cx, cy);
            result.push_back(entity);
        }
    }
    return result;
}

JobSetupCommand::JobSetupCommand(Document& document, const MaterialSetup& material)
    : document_(document),
      before_material_(document.material),
      after_material_(material),
      before_entities_(document.entities),
      after_entities_(document.entities) {
    if (material.scale_design) {
        const double sx = material.width / document.material.width;
        const double sy = material.height / document.material.height;
        if (!(isclose(sx, 1) && isclose(sy, 1))) {
            after_entities_ = scaled_entities(document.entities, sx, sy, material.curve_segments());
        }
    }
}

void JobSetupCommand::apply(const MaterialSetup& material, const std::vector<EntityPtr>& entities) {
    document_.material = material;
    document_.units = material.units;
    document_.entities = entities;
    document_.modified = true;
}

}  // namespace cnc::cad
