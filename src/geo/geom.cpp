#include "geo/geom.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <geos_c.h>

namespace cnc::geo {

namespace {

thread_local std::string t_error;

void on_error(const char* message, void*) { t_error = message ? message : "erro do GEOS"; }
void on_notice(const char*, void*) {}

// Um contexto GEOS por thread. NUNCA e finalizado de proposito: geometrias
// globais/estaticas podem ser destruidas depois dos thread_local da thread
// principal, e destruir uma geometria com o contexto ja liberado e uso apos
// liberacao. O vazamento e de uma estrutura pequena por thread, no fim do processo.
GEOSContextHandle_t H() {
    thread_local GEOSContextHandle_t h = [] {
        GEOSContextHandle_t c = GEOS_init_r();
        GEOSContext_setErrorMessageHandler_r(c, on_error, nullptr);
        GEOSContext_setNoticeMessageHandler_r(c, on_notice, nullptr);
        return c;
    }();
    return h;
}

// Sequencia de coordenadas -> pontos
Points seq_to_points(const GEOSCoordSequence* cs) {
    Points out;
    if (!cs) return out;
    unsigned int n = 0;
    if (!GEOSCoordSeq_getSize_r(H(), cs, &n)) return out;
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        double x = 0, y = 0;
        GEOSCoordSeq_getXY_r(H(), cs, i, &x, &y);
        out.push_back({x, y});
    }
    return out;
}

// Pontos -> sequencia (dono: quem chamar). `close` repete o primeiro ponto no fim.
GEOSCoordSequence* points_to_seq(const Points& pts, bool close) {
    const size_t extra = (close && !pts.empty() && pts.front() != pts.back()) ? 1 : 0;
    GEOSCoordSequence* cs =
        GEOSCoordSeq_create_r(H(), static_cast<unsigned int>(pts.size() + extra), 2);
    if (!cs) return nullptr;
    for (size_t i = 0; i < pts.size(); ++i) {
        GEOSCoordSeq_setXY_r(H(), cs, static_cast<unsigned int>(i), pts[i].x, pts[i].y);
    }
    if (extra) {
        GEOSCoordSeq_setXY_r(H(), cs, static_cast<unsigned int>(pts.size()), pts.front().x,
                             pts.front().y);
    }
    return cs;
}

bool pred(char r) { return r == 1; }

}  // namespace

const std::string& last_error() { return t_error; }

// ----------------------------------------------------------------------
// Geom
// ----------------------------------------------------------------------

Geom::Geom(const Geom& other) : p_(other.p_ ? GEOSGeom_clone_r(H(), other.p_) : nullptr) {}

Geom& Geom::operator=(const Geom& other) {
    if (this != &other) {
        GEOSGeometry* copy = other.p_ ? GEOSGeom_clone_r(H(), other.p_) : nullptr;
        if (p_) GEOSGeom_destroy_r(H(), p_);
        p_ = copy;
    }
    return *this;
}

Geom& Geom::operator=(Geom&& other) noexcept {
    if (this != &other) {
        if (p_) GEOSGeom_destroy_r(H(), p_);
        p_ = other.p_;
        other.p_ = nullptr;
    }
    return *this;
}

Geom::~Geom() {
    if (p_) GEOSGeom_destroy_r(H(), p_);
}

std::string Geom::geom_type() const {
    if (!p_) return "";
    switch (GEOSGeomTypeId_r(H(), p_)) {
        case GEOS_POINT: return "Point";
        case GEOS_LINESTRING: return "LineString";
        case GEOS_LINEARRING: return "LinearRing";
        case GEOS_POLYGON: return "Polygon";
        case GEOS_MULTIPOINT: return "MultiPoint";
        case GEOS_MULTILINESTRING: return "MultiLineString";
        case GEOS_MULTIPOLYGON: return "MultiPolygon";
        case GEOS_GEOMETRYCOLLECTION: return "GeometryCollection";
        default: return "";
    }
}

bool Geom::is_empty() const { return !p_ || pred(GEOSisEmpty_r(H(), p_)); }
bool Geom::is_valid() const { return p_ && pred(GEOSisValid_r(H(), p_)); }
bool Geom::is_closed() const { return p_ && pred(GEOSisClosed_r(H(), p_)); }

double Geom::area() const {
    double a = 0.0;
    if (p_) GEOSArea_r(H(), p_, &a);
    return a;
}

double Geom::length() const {
    double l = 0.0;
    if (p_) GEOSLength_r(H(), p_, &l);
    return l;
}

BBox Geom::bounds() const {
    BBox b;
    if (!p_) return b;
    GEOSGeom_getXMin_r(H(), p_, &b.x0);
    GEOSGeom_getYMin_r(H(), p_, &b.y0);
    GEOSGeom_getXMax_r(H(), p_, &b.x1);
    GEOSGeom_getYMax_r(H(), p_, &b.y1);
    return b;
}

Pt Geom::centroid() const {
    Pt out;
    if (!p_) return out;
    GEOSGeometry* c = GEOSGetCentroid_r(H(), p_);
    if (c) {
        GEOSGeomGetX_r(H(), c, &out.x);
        GEOSGeomGetY_r(H(), c, &out.y);
        GEOSGeom_destroy_r(H(), c);
    }
    return out;
}

Pt Geom::representative_point() const {
    Pt out;
    if (!p_) return out;
    GEOSGeometry* c = GEOSPointOnSurface_r(H(), p_);
    if (c) {
        GEOSGeomGetX_r(H(), c, &out.x);
        GEOSGeomGetY_r(H(), c, &out.y);
        GEOSGeom_destroy_r(H(), c);
    }
    return out;
}

Points Geom::coords() const {
    if (!p_) return {};
    return seq_to_points(GEOSGeom_getCoordSeq_r(H(), p_));
}

Points Geom::exterior_coords() const {
    if (!p_) return {};
    const GEOSGeometry* ring = GEOSGetExteriorRing_r(H(), p_);
    return ring ? seq_to_points(GEOSGeom_getCoordSeq_r(H(), ring)) : Points{};
}

std::vector<Points> Geom::interior_coords() const {
    std::vector<Points> out;
    if (!p_) return out;
    const int n = GEOSGetNumInteriorRings_r(H(), p_);
    for (int i = 0; i < n; ++i) {
        const GEOSGeometry* ring = GEOSGetInteriorRingN_r(H(), p_, i);
        if (ring) out.push_back(seq_to_points(GEOSGeom_getCoordSeq_r(H(), ring)));
    }
    return out;
}

std::vector<Geom> Geom::geoms() const {
    std::vector<Geom> out;
    if (!p_) return out;
    const int id = GEOSGeomTypeId_r(H(), p_);
    if (id < GEOS_MULTIPOINT) {
        out.push_back(*this);
        return out;
    }
    const int n = GEOSGetNumGeometries_r(H(), p_);
    for (int i = 0; i < n; ++i) {
        const GEOSGeometry* part = GEOSGetGeometryN_r(H(), p_, i);
        if (part) out.emplace_back(GEOSGeom_clone_r(H(), part));
    }
    return out;
}

Geom Geom::exterior_ring() const {
    if (!p_) return {};
    const GEOSGeometry* ring = GEOSGetExteriorRing_r(H(), p_);
    return ring ? Geom(GEOSGeom_clone_r(H(), ring)) : Geom{};
}

std::vector<Geom> Geom::interior_rings() const {
    std::vector<Geom> out;
    if (!p_) return out;
    const int n = GEOSGetNumInteriorRings_r(H(), p_);
    for (int i = 0; i < n; ++i) {
        const GEOSGeometry* ring = GEOSGetInteriorRingN_r(H(), p_, i);
        if (ring) out.emplace_back(GEOSGeom_clone_r(H(), ring));
    }
    return out;
}

Geom Geom::shell_polygon() const {
    if (!p_) return {};
    const GEOSGeometry* ring = GEOSGetExteriorRing_r(H(), p_);
    if (!ring) return {};
    GEOSGeometry* copy = GEOSGeom_clone_r(H(), ring);
    return Geom(GEOSGeom_createPolygon_r(H(), copy, nullptr, 0));
}

bool Geom::contains(const Geom& o) const { return p_ && o.p_ && pred(GEOSContains_r(H(), p_, o.p_)); }
bool Geom::within(const Geom& o) const { return p_ && o.p_ && pred(GEOSWithin_r(H(), p_, o.p_)); }
bool Geom::intersects(const Geom& o) const { return p_ && o.p_ && pred(GEOSIntersects_r(H(), p_, o.p_)); }
bool Geom::covers(const Geom& o) const { return p_ && o.p_ && pred(GEOSCovers_r(H(), p_, o.p_)); }
bool Geom::equals(const Geom& o) const { return p_ && o.p_ && pred(GEOSEquals_r(H(), p_, o.p_)); }

double Geom::distance(const Geom& o) const {
    double d = std::numeric_limits<double>::quiet_NaN();
    if (p_ && o.p_) GEOSDistance_r(H(), p_, o.p_, &d);
    return d;
}

Geom Geom::buffer(double distance, int quad_segs, Join join, Cap cap, double mitre_limit) const {
    if (!p_) return {};
    return Geom(GEOSBufferWithStyle_r(H(), p_, distance, quad_segs, static_cast<int>(cap),
                                      static_cast<int>(join), mitre_limit));
}

Geom Geom::intersection(const Geom& o) const {
    return (p_ && o.p_) ? Geom(GEOSIntersection_r(H(), p_, o.p_)) : Geom{};
}
Geom Geom::difference(const Geom& o) const {
    return (p_ && o.p_) ? Geom(GEOSDifference_r(H(), p_, o.p_)) : Geom{};
}
Geom Geom::symmetric_difference(const Geom& o) const {
    return (p_ && o.p_) ? Geom(GEOSSymDifference_r(H(), p_, o.p_)) : Geom{};
}
Geom Geom::union_with(const Geom& o) const {
    return (p_ && o.p_) ? Geom(GEOSUnion_r(H(), p_, o.p_)) : Geom{};
}
Geom Geom::convex_hull() const { return p_ ? Geom(GEOSConvexHull_r(H(), p_)) : Geom{}; }
Geom Geom::envelope() const { return p_ ? Geom(GEOSEnvelope_r(H(), p_)) : Geom{}; }
Geom Geom::boundary() const { return p_ ? Geom(GEOSBoundary_r(H(), p_)) : Geom{}; }
Geom Geom::simplify(double tolerance, bool preserve_topology) const {
    if (!p_) return {};
    return Geom(preserve_topology ? GEOSTopologyPreserveSimplify_r(H(), p_, tolerance)
                                  : GEOSSimplify_r(H(), p_, tolerance));
}
Geom Geom::normalized() const {
    if (!p_) return {};
    Geom c(*this);
    GEOSNormalize_r(H(), c.p_);
    return c;
}
Geom Geom::reversed() const { return p_ ? Geom(GEOSReverse_r(H(), p_)) : Geom{}; }
Geom Geom::make_valid() const { return p_ ? Geom(GEOSMakeValid_r(H(), p_)) : Geom{}; }

Geom Geom::offset_curve(double distance, int quad_segs, Join join, double mitre_limit) const {
    if (!p_) return {};
    return Geom(GEOSOffsetCurve_r(H(), p_, distance, quad_segs, static_cast<int>(join), mitre_limit));
}

Pt Geom::interpolate(double distance) const {
    Pt out;
    if (!p_) return out;
    GEOSGeometry* g = GEOSInterpolate_r(H(), p_, distance);
    if (g) {
        GEOSGeomGetX_r(H(), g, &out.x);
        GEOSGeomGetY_r(H(), g, &out.y);
        GEOSGeom_destroy_r(H(), g);
    }
    return out;
}

Pt Geom::interpolate_normalized(double fraction) const {
    Pt out;
    if (!p_) return out;
    GEOSGeometry* g = GEOSInterpolateNormalized_r(H(), p_, fraction);
    if (g) {
        GEOSGeomGetX_r(H(), g, &out.x);
        GEOSGeomGetY_r(H(), g, &out.y);
        GEOSGeom_destroy_r(H(), g);
    }
    return out;
}

double Geom::project(Pt p) const {
    if (!p_) return 0.0;
    Geom pt = make_point(p);
    return GEOSProject_r(H(), p_, pt.get());
}

namespace {
struct Affine2D {
    double a, b, d, e, xoff, yoff;
};
int apply_affine(double* x, double* y, void* user) {
    const Affine2D* m = static_cast<const Affine2D*>(user);
    const double px = *x, py = *y;
    *x = m->a * px + m->b * py + m->xoff;
    *y = m->d * px + m->e * py + m->yoff;
    return 1;
}
}  // namespace

Geom Geom::rotated(double angle_deg, Pt origin) const {
    if (!p_ || is_empty()) return *this;
    const double angle = angle_deg * kPi / 180.0;
    double cosp = std::cos(angle), sinp = std::sin(angle);
    if (std::fabs(cosp) < 2.5e-16) cosp = 0.0;
    if (std::fabs(sinp) < 2.5e-16) sinp = 0.0;
    const double x0 = origin.x, y0 = origin.y;
    Affine2D m{cosp, -sinp, sinp, cosp, x0 - x0 * cosp + y0 * sinp, y0 - x0 * sinp - y0 * cosp};
    return Geom(GEOSGeom_transformXY_r(H(), p_, apply_affine, &m));
}

// ----------------------------------------------------------------------
// Construcao
// ----------------------------------------------------------------------

Geom make_point(Pt p) {
    GEOSCoordSequence* cs = points_to_seq({p}, false);
    return cs ? Geom(GEOSGeom_createPoint_r(H(), cs)) : Geom{};
}

Geom make_linestring(const Points& pts) {
    if (pts.size() < 2) return {};
    GEOSCoordSequence* cs = points_to_seq(pts, false);
    return cs ? Geom(GEOSGeom_createLineString_r(H(), cs)) : Geom{};
}

Geom make_polygon(const Points& shell, const std::vector<Points>& holes) {
    // um anel fechado precisa de 4 coordenadas (3 distintas + o fechamento)
    auto ring = [](const Points& pts) -> GEOSGeometry* {
        if (pts.size() < 3) return nullptr;
        GEOSCoordSequence* cs = points_to_seq(pts, true);
        if (!cs) return nullptr;
        unsigned int n = 0;
        GEOSCoordSeq_getSize_r(H(), cs, &n);
        if (n < 4) {
            GEOSCoordSeq_destroy_r(H(), cs);
            return nullptr;
        }
        return GEOSGeom_createLinearRing_r(H(), cs);
    };
    GEOSGeometry* outer = ring(shell);
    if (!outer) return {};
    std::vector<GEOSGeometry*> inner;
    for (const Points& h : holes) {
        GEOSGeometry* r = ring(h);
        if (!r) {
            for (GEOSGeometry* g : inner) GEOSGeom_destroy_r(H(), g);
            GEOSGeom_destroy_r(H(), outer);
            return {};
        }
        inner.push_back(r);
    }
    return Geom(GEOSGeom_createPolygon_r(H(), outer, inner.empty() ? nullptr : inner.data(),
                                         static_cast<unsigned int>(inner.size())));
}

namespace {
Geom make_multi(int type, const std::vector<Geom>& parts) {
    std::vector<GEOSGeometry*> raw;
    raw.reserve(parts.size());
    for (const Geom& g : parts) {
        if (g) raw.push_back(GEOSGeom_clone_r(H(), g.get()));
    }
    return Geom(GEOSGeom_createCollection_r(H(), type, raw.empty() ? nullptr : raw.data(),
                                            static_cast<unsigned int>(raw.size())));
}
}  // namespace

Geom make_multipolygon(const std::vector<Geom>& polygons) { return make_multi(GEOS_MULTIPOLYGON, polygons); }
Geom make_multilinestring(const std::vector<Geom>& lines) { return make_multi(GEOS_MULTILINESTRING, lines); }
Geom make_collection(const std::vector<Geom>& parts) { return make_multi(GEOS_GEOMETRYCOLLECTION, parts); }

Geom make_multipoint(const Points& pts) {
    std::vector<Geom> parts;
    for (const Pt& p : pts) parts.push_back(make_point(p));
    return make_multi(GEOS_MULTIPOINT, parts);
}

// ----------------------------------------------------------------------
// Funcoes de modulo
// ----------------------------------------------------------------------

Geom unary_union(const std::vector<Geom>& geoms) {
    Geom collection = make_collection(geoms);
    if (!collection) return {};
    return Geom(GEOSUnaryUnion_r(H(), collection.get()));
}

double ring_signed_area(const Points& ring) {
    // shapely.geometry.polygon.signed_area: soma xs[i] * (ys[i+1] - ys[i-1]) com
    // o segundo ponto repetido no fim, dividida por 2.
    const size_t n = ring.size();
    if (n < 2) return 0.0;
    std::vector<double> xs, ys;
    xs.reserve(n + 1);
    ys.reserve(n + 1);
    for (const Pt& p : ring) {
        xs.push_back(p.x);
        ys.push_back(p.y);
    }
    xs.push_back(xs[1]);
    ys.push_back(ys[1]);
    double total = 0.0;
    for (size_t i = 1; i < n; ++i) total += xs[i] * (ys[i + 1] - ys[i - 1]);
    return total / 2.0;
}

Geom orient(const Geom& polygon, double sign) {
    if (!polygon) return {};
    Points shell = polygon.exterior_coords();
    if (!(ring_signed_area(shell) / sign >= 0.0)) std::reverse(shell.begin(), shell.end());
    std::vector<Points> holes = polygon.interior_coords();
    for (Points& h : holes) {
        if (!(ring_signed_area(h) / sign <= 0.0)) std::reverse(h.begin(), h.end());
    }
    return make_polygon(shell, holes);
}

Geom line_merge(const Geom& lines) {
    return lines ? Geom(GEOSLineMerge_r(H(), lines.get())) : Geom{};
}

std::pair<Pt, double> maximum_inscribed_circle(const Geom& polygon, double tolerance) {
    if (!polygon) return {{}, -1.0};
    GEOSGeometry* line = GEOSMaximumInscribedCircle_r(H(), polygon.get(), tolerance);
    if (!line) return {{}, -1.0};
    Geom g(line);
    const Points pts = g.coords();
    if (pts.size() < 2) return {{}, -1.0};
    return {pts[0], dist(pts.front(), pts.back())};
}

Geom voronoi_polygons(const Geom& g, double tolerance, bool only_edges, const Geom& extend_to) {
    return g ? Geom(GEOSVoronoiDiagram_r(H(), g.get(), extend_to ? extend_to.get() : nullptr, tolerance,
                                         only_edges ? 1 : 0))
             : Geom{};
}

struct STRtree::Impl {
    GEOSSTRtree* tree = nullptr;
    std::vector<Geom> geoms;  // donas das geometrias que a arvore referencia
    std::unordered_map<const void*, int> index_of;
};

STRtree::STRtree(const std::vector<Geom>& geoms, size_t node_capacity) : impl_(std::make_unique<Impl>()) {
    impl_->geoms = geoms;
    impl_->tree = GEOSSTRtree_create_r(H(), node_capacity);
    for (size_t i = 0; i < impl_->geoms.size(); ++i) {
        const Geom& g = impl_->geoms[i];
        if (!g || g.is_empty()) continue;
        impl_->index_of[g.get()] = static_cast<int>(i);
        GEOSSTRtree_insert_r(H(), impl_->tree, g.get(), const_cast<GEOSGeometry*>(g.get()));
    }
}

STRtree::~STRtree() {
    if (impl_ && impl_->tree) GEOSSTRtree_destroy_r(H(), impl_->tree);
}

std::vector<int> STRtree::query(const Geom& g) const {
    struct Ctx {
        const Impl* impl;
        std::vector<int>* out;
    };
    std::vector<int> out;
    if (!g || g.is_empty()) return out;
    Ctx ctx{impl_.get(), &out};
    GEOSSTRtree_query_r(
        H(), impl_->tree, g.get(),
        [](void* item, void* user) {
            Ctx* c = static_cast<Ctx*>(user);
            auto it = c->impl->index_of.find(item);
            if (it != c->impl->index_of.end()) c->out->push_back(it->second);
        },
        &ctx);
    return out;
}

int STRtree::nearest(const Geom& g) const {
    if (!g || g.is_empty() || impl_->index_of.empty()) return -1;
    auto distance = [](const void* a, const void* b, double* d, void*) -> int {
        return GEOSDistance_r(H(), static_cast<const GEOSGeometry*>(a), static_cast<const GEOSGeometry*>(b), d);
    };
    const void* item = GEOSSTRtree_nearest_generic_r(H(), impl_->tree, g.get(), g.get(), distance, nullptr);
    if (!item) return -1;
    auto it = impl_->index_of.find(item);
    return it == impl_->index_of.end() ? -1 : it->second;
}

std::pair<Pt, Pt> nearest_points(const Geom& a, const Geom& b) {
    std::pair<Pt, Pt> out;
    if (!a || !b) return out;
    GEOSCoordSequence* cs = GEOSNearestPoints_r(H(), a.get(), b.get());
    if (!cs) return out;
    const Points pts = seq_to_points(cs);
    GEOSCoordSeq_destroy_r(H(), cs);
    if (pts.size() >= 2) out = {pts[0], pts[1]};
    return out;
}

std::vector<int> BoxIndex::query(const BBox& q) const {
    std::vector<int> out;
    for (size_t i = 0; i < boxes_.size(); ++i) {
        const BBox& b = boxes_[i];
        if (b.x0 <= q.x1 && q.x0 <= b.x1 && b.y0 <= q.y1 && q.y0 <= b.y1) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

}  // namespace cnc::geo
