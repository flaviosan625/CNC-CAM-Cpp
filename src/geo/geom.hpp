// Camada fina sobre o GEOS C API, com a mesma semantica do shapely 2.x.
//
// O app em Python usa shapely, que e uma casca do GEOS. Usar o mesmo GEOS (3.14.1)
// aqui da o mesmo resultado nas booleanas, no buffer e no offset.
//
// Convencao de erro: onde o shapely lanca excecao (ValueError, GEOSException) e o
// Python captura com try/except, estas funcoes devolvem uma Geom nula. Quem chama
// testa `if (!g)`. A mensagem do GEOS fica em geo::last_error().
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geo/types.hpp"

// Declaracao antecipada: quem inclui este header nao precisa do geos_c.h.
typedef struct GEOSGeom_t GEOSGeometry;

namespace cnc::geo {

// Mensagem do ultimo erro do GEOS nesta thread ("" se nao houve).
const std::string& last_error();

// Estilos de juncao/ponta, com os mesmos valores numericos do shapely.
enum class Join { Round = 1, Mitre = 2, Bevel = 3 };
enum class Cap { Round = 1, Flat = 2, Square = 3 };

// Dono de uma geometria do GEOS. Copiar = copia profunda.
class Geom {
public:
    Geom() = default;
    explicit Geom(GEOSGeometry* owned) : p_(owned) {}
    Geom(const Geom& other);
    Geom(Geom&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    Geom& operator=(const Geom& other);
    Geom& operator=(Geom&& other) noexcept;
    ~Geom();

    explicit operator bool() const { return p_ != nullptr; }
    bool null() const { return p_ == nullptr; }
    const GEOSGeometry* get() const { return p_; }
    GEOSGeometry* get() { return p_; }
    GEOSGeometry* release() {
        GEOSGeometry* g = p_;
        p_ = nullptr;
        return g;
    }

    // -- o que e --
    // "Point", "LineString", "LinearRing", "Polygon", "MultiPoint",
    // "MultiLineString", "MultiPolygon", "GeometryCollection"
    std::string geom_type() const;
    bool is_polygon() const { return geom_type() == "Polygon"; }
    bool is_line() const { return geom_type() == "LineString"; }
    bool is_empty() const;
    bool is_valid() const;
    bool is_closed() const;  // LineString/LinearRing fechado

    // -- medidas --
    double area() const;
    double length() const;
    BBox bounds() const;  // (minx, miny, maxx, maxy); so para nao vazia
    Pt centroid() const;
    Pt representative_point() const;  // sempre dentro da geometria

    // -- coordenadas --
    // LineString / LinearRing / Point: a sequencia, como shapely .coords
    Points coords() const;
    // Polygon: anel externo COM o ponto de fechamento (como .exterior.coords)
    Points exterior_coords() const;
    // Polygon: aneis internos COM o ponto de fechamento
    std::vector<Points> interior_coords() const;
    // Multi* / GeometryCollection: as partes (copias). Geometria simples: ela mesma.
    std::vector<Geom> geoms() const;
    // Polygon: so o anel externo, como Polygon (.exterior vira poligono sem furos)
    Geom shell_polygon() const;
    // Polygon: os aneis como LinearRing (shapely .exterior e .interiors)
    Geom exterior_ring() const;
    std::vector<Geom> interior_rings() const;

    // -- predicados --
    bool contains(const Geom& other) const;
    bool within(const Geom& other) const;
    bool intersects(const Geom& other) const;
    bool covers(const Geom& other) const;
    bool equals(const Geom& other) const;
    double distance(const Geom& other) const;

    // -- operacoes --
    // buffer() com os defaults do shapely 2.x (quad_segs=16, round, round, 5.0).
    Geom buffer(double distance, int quad_segs = 16, Join join = Join::Round,
                Cap cap = Cap::Round, double mitre_limit = 5.0) const;
    Geom intersection(const Geom& other) const;
    Geom difference(const Geom& other) const;
    Geom symmetric_difference(const Geom& other) const;
    Geom union_with(const Geom& other) const;
    Geom convex_hull() const;
    Geom envelope() const;
    Geom boundary() const;
    Geom simplify(double tolerance, bool preserve_topology = true) const;
    Geom normalized() const;
    Geom reversed() const;
    Geom make_valid() const;
    // offset_curve (so LineString): distancia positiva = esquerda, negativa = direita
    Geom offset_curve(double distance, int quad_segs = 16, Join join = Join::Round,
                      double mitre_limit = 5.0) const;
    // Ponto a `distance` ao longo da linha (sem normalizar)
    Pt interpolate(double distance) const;
    // Ponto a `fraction` (0..1) do comprimento: interpolate(f, normalized=True)
    Pt interpolate_normalized(double fraction) const;
    // Distancia ao longo da linha ate o ponto mais proximo de p
    double project(Pt p) const;
    // shapely.affinity.rotate(geom, angle, origin=(x, y), use_radians=False). A conta e a do
    // shapely: angulo * pi / 180 e x' = a*x + b*y + xoff, na mesma ordem.
    Geom rotated(double angle_deg, Pt origin) const;

private:
    GEOSGeometry* p_ = nullptr;
};

// -- construcao (devolvem Geom nula se a entrada for invalida) --------------------
Geom make_point(Pt p);
Geom make_linestring(const Points& pts);  // precisa de >= 2 pontos
// Anel fechado automaticamente, como o shapely. Precisa de >= 3 pontos distintos.
Geom make_polygon(const Points& shell, const std::vector<Points>& holes = {});
Geom make_multipolygon(const std::vector<Geom>& polygons);
Geom make_multilinestring(const std::vector<Geom>& lines);
Geom make_multipoint(const Points& pts);
Geom make_collection(const std::vector<Geom>& parts);

// -- funcoes de modulo (shapely.ops / shapely.*) ----------------------------------
// shapely.unary_union: uniao de todas de uma vez.
Geom unary_union(const std::vector<Geom>& geoms);
// shapely.geometry.polygon.orient(polygon, sign): exterior CCW para sign=+1, furos ao contrario.
Geom orient(const Geom& polygon, double sign = 1.0);
// shapely.line_merge
Geom line_merge(const Geom& lines);
// shapely.maximum_inscribed_circle -> (centro, raio). raio < 0 se falhar.
std::pair<Pt, double> maximum_inscribed_circle(const Geom& polygon, double tolerance = 0.0);
// shapely.voronoi_polygons: celulas (ou arestas se only_edges). `extend_to` (opcional) e o
// envelope em que o diagrama e cortado.
Geom voronoi_polygons(const Geom& g, double tolerance = 0.0, bool only_edges = false,
                      const Geom& extend_to = {});
// shapely.ops.nearest_points: (ponto em a, ponto em b)
std::pair<Pt, Pt> nearest_points(const Geom& a, const Geom& b);

// shapely.geometry.polygon.signed_area do anel (positivo = anti-horario).
double ring_signed_area(const Points& closed_ring);

// shapely.strtree.STRtree sobre o STRtree do proprio GEOS (capacidade de no 10, como o
// shapely). Geometrias vazias nao entram. query() devolve os indices cujo envelope toca o
// envelope de `g`, na ordem em que a arvore os visita (como o shapely, sem ordenar).
class STRtree {
public:
    explicit STRtree(const std::vector<Geom>& geoms, size_t node_capacity = 10);
    ~STRtree();
    STRtree(const STRtree&) = delete;
    STRtree& operator=(const STRtree&) = delete;

    std::vector<int> query(const Geom& g) const;
    // Indice da geometria mais proxima de `g`, ou -1 se a arvore estiver vazia.
    int nearest(const Geom& g) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Indice espacial por caixa envolvente (substitui o STRtree.query do shapely).
class BoxIndex {
public:
    void add(const BBox& box) { boxes_.push_back(box); }
    // Indices, em ordem crescente, cujas caixas tocam `box` (bordas incluidas).
    std::vector<int> query(const BBox& box) const;
    size_t size() const { return boxes_.size(); }

private:
    std::vector<BBox> boxes_;
};

}  // namespace cnc::geo
