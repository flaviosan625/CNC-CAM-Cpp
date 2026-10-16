#include "io/import_operations.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "cam/nesting.hpp"

namespace cnc::io {

const char* imported_operation_layer_name(ImportedOperation operation) noexcept {
    switch (operation) {
    case ImportedOperation::CutInternal: return "CORTE_INTERNO";
    case ImportedOperation::CutExternal: return "CORTE_EXTERNO";
    case ImportedOperation::Open: return "VETORES_ABERTOS";
    case ImportedOperation::Review: return "REVISAR";
    }
    return "REVISAR";
}

std::vector<ImportedOperationAssignment> classify_imported_operations(
    const std::vector<cad::EntityPtr>& entities, double tolerance) {
    if (!std::isfinite(tolerance) || tolerance <= 0.0) tolerance = cam::kDefaultTolerance;

    std::vector<ImportedOperationAssignment> assignments;
    assignments.reserve(entities.size());
    std::vector<cad::EntityPtr> valid_closed;
    valid_closed.reserve(entities.size());
    std::vector<geo::Geom> invalid_closed_envelopes;
    std::unordered_map<const cad::Entity*, size_t> assignment_index;
    assignment_index.reserve(entities.size());

    const auto set_operation = [&](size_t index, ImportedOperation operation,
                                   const std::string& reason = {}) {
        auto& assignment = assignments[index];
        assignment.operation = operation;
        assignment.layer_name = imported_operation_layer_name(operation);
        assignment.reason = reason;
    };

    for (const auto& entity : entities) {
        if (!entity) continue;
        const size_t index = assignments.size();
        assignments.push_back({entity->id, ImportedOperation::Review, "REVISAR", {}});
        assignment_index.emplace(entity.get(), index);

        if (entity->kind() == cad::EntityKind::Text) {
            set_operation(index, ImportedOperation::Review,
                          "Texto: converter em vetores para separar os contornos de corte.");
            continue;
        }
        if (entity->kind() == cad::EntityKind::Dimension ||
            entity->kind() == cad::EntityKind::DrillPoint ||
            entity->kind() == cad::EntityKind::ConstructionLine) {
            set_operation(index, ImportedOperation::Review,
                          "Elemento auxiliar ou ponto: definir a operacao de usinagem.");
            continue;
        }

        const auto entity_bounds = entity->bounding_box();
        if (!std::isfinite(entity_bounds.x0) || !std::isfinite(entity_bounds.y0) ||
            !std::isfinite(entity_bounds.x1) || !std::isfinite(entity_bounds.y1)) {
            set_operation(index, ImportedOperation::Review, "Vetor com coordenadas invalidas.");
            continue;
        }
        const Points points = entity->to_points(cam::entity_segments(*entity, tolerance));
        if (points.size() < 2 || !std::all_of(points.begin(), points.end(), [](Pt point) {
                return std::isfinite(point.x) && std::isfinite(point.y);
            })) {
            set_operation(index, ImportedOperation::Review, "Vetor vazio ou com coordenadas invalidas.");
            continue;
        }
        if (!entity->closed()) {
            const auto line = geo::make_linestring(points);
            if (!line || line.length() <= 1e-9) {
                set_operation(index, ImportedOperation::Review, "Vetor sem comprimento utilizavel.");
            } else {
                set_operation(index, ImportedOperation::Open,
                              "Vetor aberto: escolher a operacao ou fechar o contorno.");
            }
            continue;
        }

        // cam::split_entities repairs self-intersections for CAM. Import setup
        // must preserve these as review items rather than silently repairing and
        // assigning a potentially different region to an automatic cut.
        const auto raw_polygon = geo::make_polygon(points);
        if (!raw_polygon || !raw_polygon.is_valid() || raw_polygon.area() <= 1e-9) {
            set_operation(index, ImportedOperation::Review,
                          "Contorno fechado invalido, auto-intersectado ou sem area.");
            if (entity_bounds.x1 > entity_bounds.x0 && entity_bounds.y1 > entity_bounds.y0) {
                invalid_closed_envelopes.push_back(geo::make_polygon({
                    {entity_bounds.x0, entity_bounds.y0}, {entity_bounds.x1, entity_bounds.y0},
                    {entity_bounds.x1, entity_bounds.y1}, {entity_bounds.x0, entity_bounds.y1}}));
            }
            continue;
        }
        valid_closed.push_back(entity);
    }

    auto split = cam::split_entities(valid_closed, tolerance);
    std::vector<geo::Geom> polygons;
    std::vector<geo::Geom> boundaries;
    std::vector<BBox> bounds;
    std::vector<size_t> owners;
    polygons.reserve(split.closed.size());
    boundaries.reserve(split.closed.size());
    bounds.reserve(split.closed.size());
    owners.reserve(split.closed.size());
    for (auto& contour : split.closed) {
        const size_t index = assignment_index.at(contour->entity.get());
        owners.push_back(index);
        boundaries.push_back(contour->polygon.exterior_ring());
        bounds.push_back(contour->polygon.bounds());
        polygons.push_back(std::move(contour->polygon));
        set_operation(index, ImportedOperation::CutExternal);
    }

    if (polygons.empty()) return assignments;

    // Use the GEOS spatial tree: BoxIndex currently scans every box and makes a
    // page containing thousands of separate pieces quadratic on import.
    geo::STRtree tree(polygons);
    std::vector<int> levels(polygons.size(), 0);
    std::vector<bool> needs_review(polygons.size(), false);
    // A valid outline inside/crossing an invalid outline also has an unknown
    // cut side; do not classify it as external merely because repair was skipped.
    for (const auto& invalid_envelope : invalid_closed_envelopes) {
        for (const int candidate : tree.query(invalid_envelope)) {
            needs_review[static_cast<size_t>(candidate)] = true;
        }
    }
    for (size_t i = 0; i < polygons.size(); ++i) {
        const auto& box = bounds[i];
        const auto nearby = geo::make_polygon({
            {box.x0 - tolerance, box.y0 - tolerance},
            {box.x1 + tolerance, box.y0 - tolerance},
            {box.x1 + tolerance, box.y1 + tolerance},
            {box.x0 - tolerance, box.y1 + tolerance}});
        for (const int candidate : tree.query(nearby)) {
            const size_t j = static_cast<size_t>(candidate);
            if (j <= i) continue;
            // Boundary clearance also catches tangent/duplicate outlines and
            // contours so close that discretization could change containment.
            if (!boundaries[i] || !boundaries[j] ||
                boundaries[i].distance(boundaries[j]) <= tolerance) {
                needs_review[i] = true;
                needs_review[j] = true;
                continue;
            }
            // Full-polygon containment, never bounding boxes or one probe:
            // concave shapes and partial overlaps must not be called holes.
            if (polygons[i].contains(polygons[j])) {
                ++levels[j];
            } else if (polygons[j].contains(polygons[i])) {
                ++levels[i];
            } else if (polygons[i].intersects(polygons[j])) {
                needs_review[i] = true;
                needs_review[j] = true;
            }
        }
    }

    // Ambiguous outlines cannot safely determine the parity of their children.
    // Mark descendants too, leaving unrelated parts of a large drawing usable.
    const auto direct_review = needs_review;
    for (size_t i = 0; i < polygons.size(); ++i) {
        if (!direct_review[i]) continue;
        for (const int candidate : tree.query(polygons[i])) {
            const size_t j = static_cast<size_t>(candidate);
            if (i == j || needs_review[j]) continue;
            if (polygons[i].contains(polygons[j])) needs_review[j] = true;
        }
    }

    for (size_t i = 0; i < polygons.size(); ++i) {
        if (needs_review[i]) {
            set_operation(owners[i], ImportedOperation::Review,
                          "Contornos sobrepostos, encostados ou dependentes de contornos ambiguos.");
        } else {
            set_operation(owners[i], levels[i] % 2 == 1 ? ImportedOperation::CutInternal
                                                       : ImportedOperation::CutExternal);
        }
    }
    return assignments;
}

}  // namespace cnc::io
