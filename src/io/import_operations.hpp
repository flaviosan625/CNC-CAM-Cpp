#pragma once

#include <string>
#include <vector>

#include "cad/entities.hpp"

namespace cnc::io {

// Suggestions for imported vectors, not toolpaths. A pocket/recess cannot be
// inferred from a 2D outline and must be assigned by the operator.
enum class ImportedOperation { CutInternal, CutExternal, Open, Review };

struct ImportedOperationAssignment {
    std::string entity_id;
    ImportedOperation operation = ImportedOperation::Review;
    std::string layer_name;
    std::string reason;
};

const char* imported_operation_layer_name(ImportedOperation operation) noexcept;

// Returns one assignment per non-null entity, in input order. The function does
// not mutate geometry, entity layers, selection, document state or toolpaths.
// Nested valid contours use even/odd containment parity. Intersecting, touching,
// duplicate or invalid contours remain for review instead of guessing a cut side.
std::vector<ImportedOperationAssignment> classify_imported_operations(
    const std::vector<cad::EntityPtr>& entities, double tolerance = 0.01);

}  // namespace cnc::io
