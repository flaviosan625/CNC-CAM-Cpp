#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cad/document.hpp"
#include "cad/entities.hpp"
#include "utils/commands.hpp"

namespace cnc::cad {

// 1. Concentric:
// Alinha centros de círculos, arcos ou centros de bounding box à entidade primária (sel.front()).
bool align_concentric(const std::vector<EntityPtr>& selection);

// 2. Equal radius / size:
// Iguala raio (círculos/arcos) ou dimensões (retângulos/bounding box) à entidade primária.
bool make_equal_size(const std::vector<EntityPtr>& selection);

// 3. Align horizontal center:
// Alinha o centro horizontal (eixo X) de todas as entidades ao centro da primária.
bool align_center_horizontal(const std::vector<EntityPtr>& selection);

// 4. Align vertical center:
// Alinha o centro vertical (eixo Y) de todas as entidades ao centro da primária.
bool align_center_vertical(const std::vector<EntityPtr>& selection);

// 5. Tangent:
// Ajusta a posição das entidades para que fiquem tangentes entre si (círculo com linha, círculos entre si, etc).
bool make_tangent(const std::vector<EntityPtr>& selection);

// 6. Quick group or join:
// Une vetores abertos cujas extremidades se tocam; se já estiverem fechados ou não unirem, agrupa as entidades.
bool quick_group_or_join(Document& doc, const std::vector<EntityPtr>& selection,
                         UndoStack* stack = nullptr, double tolerance = 0.5);

// -- Funções de aplicação com UndoStack --
void apply_concentric_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection);
void apply_equal_size_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection);
void apply_align_center_h_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection);
void apply_align_center_v_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection);
void apply_tangent_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection);
void apply_quick_group_or_join_command(Document& doc, UndoStack* stack, const std::vector<EntityPtr>& selection,
                                       double tolerance = 0.5);

}  // namespace cnc::cad
