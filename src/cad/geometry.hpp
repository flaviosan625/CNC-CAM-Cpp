// Operacoes geometricas: booleanas, offset, filete, aparar, estender (porte de src/cad/geometry.py).
//
// Usa o GEOS (o mesmo do shapely) para booleanas/offset e matematica direta para os filetes.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cad/entities.hpp"
#include "geo/geom.hpp"

namespace cnc::cad {

inline constexpr int kSeg = 96;

// -- conversao entidade <-> GEOS ----------------------------------------------------------------
// Geom nula = None do Python (entidade sem area/linha aproveitavel).
geo::Geom to_shapely(const Entity& e);
// Converte a geometria de volta para entidades Polyline, herdando camada e cor do modelo.
std::vector<EntityPtr> from_shapely(const geo::Geom& geom, const Entity* template_entity = nullptr);

// -- booleanas -----------------------------------------------------------------------------------
std::vector<EntityPtr> boolean_union(const std::vector<EntityPtr>& entities);
std::vector<EntityPtr> boolean_subtract(const std::vector<EntityPtr>& base, const std::vector<EntityPtr>& cutters);
std::vector<EntityPtr> boolean_intersect(const std::vector<EntityPtr>& entities);

// Offset de uma entidade. distance > 0; outward=false -> para dentro. join: round | mitre | bevel.
std::vector<EntityPtr> offset_entity(const Entity& e, double distance, bool outward = true,
                                     const std::string& join = "round");

// Arredonda o vertice idx de uma polilinha, devolvendo os novos pontos.
Points fillet_corner(const Points& pts, int idx, double radius, int seg = 12, bool closed = true);

// Une polilinhas/linhas abertas cujas extremidades estejam a <= tol.
std::vector<EntityPtr> join_open_vectors(const std::vector<EntityPtr>& entities, double tol = 0.1);

// Une automaticamente vetores abertos agrupando por camada (preserva entidades fechadas, textos e pontos).
std::vector<EntityPtr> join_open_vectors_by_layer(const std::vector<EntityPtr>& entities, double tol = 0.1);

// Estende a extremidade mais proxima da linha ate o primeiro cruzamento ("start", "end" ou nada).
bool extend_line_to(Line& line, const std::vector<EntityPtr>& targets,
                    const std::optional<std::string>& endpoint = std::nullopt);
// Remove o trecho de `e` entre os cruzamentos mais proximos do clique.
std::vector<EntityPtr> trim_at_point(const Entity& e, double x, double y, const std::vector<EntityPtr>& others);

std::vector<EntityPtr> vector_boundary(const std::vector<EntityPtr>& entities, double offset = 0.0,
                                       bool rubber_band = false);

// Suaviza uma polilinha com Catmull-Rom.
Points fit_curve(const Points& pts, bool closed, double smooth = 0.5, int subdiv = 8);

}  // namespace cnc::cad
