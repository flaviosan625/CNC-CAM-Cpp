// Entidades geometricas 2D - representacao interna do CAD.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "geo/types.hpp"

namespace cnc::cad {

inline constexpr Rgb kSelColor{1.0, 0.55, 0.0};
inline constexpr Rgb kDefaultColor{0.1, 0.1, 0.1};

enum class EntityKind {
    Line, ConstructionLine, DrillPoint, Circle, Ellipse, Arc,
    Rectangle, Polyline, Text, Dimension,
};

Pt rot_pt(double x, double y, double cx, double cy, double a);
double seg_dist(double px, double py, double ax, double ay, double bx, double by);

class Entity;
using EntityPtr = std::shared_ptr<Entity>;

// ======================================================================
// Base
// ======================================================================
class Entity {
public:
    std::string id = new_id();
    std::string layer = "Camada 1";
    Rgb color = kDefaultColor;
    bool selected = false;
    std::optional<std::string> group;

    Entity() = default;
    Entity(const Entity&) = default;
    Entity& operator=(const Entity&) = default;
    virtual ~Entity() = default;

    virtual EntityKind kind() const = 0;
    // Nome gravado no campo "type" do arquivo .cnc (igual ao nome da classe Python).
    virtual const char* type_name() const = 0;
    virtual bool closed() const { return false; }

    // -- geometria --
    // Discretiza a entidade em pontos (coordenadas de mundo).
    virtual Points to_points(int seg = 64) const = 0;
    virtual BBox bounding_box() const;
    Pt center() const;

    // -- transformacoes (no proprio objeto) --
    virtual void translate(double dx, double dy) = 0;
    virtual void rotate(double deg, double cx, double cy) = 0;
    virtual void scale(double sx, double sy, double cx, double cy) = 0;
    // Espelha horizontalmente (inverte X) em torno de axis_x.
    void mirror_h(double axis_x) { scale(-1.0, 1.0, axis_x, 0.0); }
    // Espelha verticalmente (inverte Y) em torno de axis_y.
    void mirror_v(double axis_y) { scale(1.0, -1.0, 0.0, axis_y); }

    // Copia exata, com o MESMO id (equivale ao copy.deepcopy do Python).
    virtual EntityPtr copy() const = 0;
    // Copia para uso novo: id novo e sem selecao (Entity.clone do Python).
    EntityPtr clone() const;

    // Menor distancia do ponto (x, y) ao contorno da entidade.
    virtual double distance_to(double x, double y) const;

    // Copia o estado de `other` (do MESMO tipo concreto) para este objeto, mantendo id e selecao.
    // E o que o desfazer por snapshot precisa: o objeto continua o mesmo para quem o referencia.
    void restore_from(const Entity& other) {
        const std::string keep_id = id;
        const bool keep_selected = selected;
        assign_from(other);
        id = keep_id;
        selected = keep_selected;
    }

protected:
    virtual void assign_from(const Entity& other) = 0;
};

// ======================================================================
// Linha
// ======================================================================
class Line : public Entity {
public:
    double x1 = 0.0, y1 = 0.0, x2 = 100.0, y2 = 0.0;

    EntityKind kind() const override { return EntityKind::Line; }
    const char* type_name() const override { return "Line"; }
    Points to_points(int seg = 64) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Line>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Line&>(o); }
public:

    double length() const;
    double angle() const;
};

// Linha auxiliar; so entra no CAM quando o usuario autoriza.
class ConstructionLine : public Line {
public:
    bool machinable = false;

    EntityKind kind() const override { return EntityKind::ConstructionLine; }
    const char* type_name() const override { return "ConstructionLine"; }
    EntityPtr copy() const override { return std::make_shared<ConstructionLine>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const ConstructionLine&>(o); }
public:
};

// Centro pontual usado exclusivamente pelo percurso de furacao.
class DrillPoint : public Entity {
public:
    double x = 0.0, y = 0.0;

    EntityKind kind() const override { return EntityKind::DrillPoint; }
    const char* type_name() const override { return "DrillPoint"; }
    Points to_points(int seg = 64) const override;
    BBox bounding_box() const override { return {x, y, x, y}; }
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<DrillPoint>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const DrillPoint&>(o); }
public:
};

// ======================================================================
// Circulo
// ======================================================================
class Circle : public Entity {
public:
    double cx = 0.0, cy = 0.0, radius = 50.0;

    EntityKind kind() const override { return EntityKind::Circle; }
    const char* type_name() const override { return "Circle"; }
    bool closed() const override { return true; }
    Points to_points(int seg = 64) const override;
    BBox bounding_box() const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Circle>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Circle&>(o); }
public:
};

// ======================================================================
// Elipse
// ======================================================================
class Ellipse : public Entity {
public:
    double cx = 0.0, cy = 0.0, rx = 60.0, ry = 40.0;
    double rotation = 0.0;  // graus

    EntityKind kind() const override { return EntityKind::Ellipse; }
    const char* type_name() const override { return "Ellipse"; }
    bool closed() const override { return true; }
    Points to_points(int seg = 64) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Ellipse>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Ellipse&>(o); }
public:
};

// ======================================================================
// Arco
// ======================================================================
class Arc : public Entity {
public:
    double cx = 0.0, cy = 0.0, radius = 50.0;
    double start_angle = 0.0, end_angle = 90.0;

    EntityKind kind() const override { return EntityKind::Arc; }
    const char* type_name() const override { return "Arc"; }
    Points to_points(int seg = 64) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Arc>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Arc&>(o); }
public:
};

// ======================================================================
// Retangulo
// ======================================================================
class Rectangle : public Entity {
public:
    double x = 0.0, y = 0.0, width = 100.0, height = 60.0;
    double rotation = 0.0, corner_radius = 0.0;

    EntityKind kind() const override { return EntityKind::Rectangle; }
    const char* type_name() const override { return "Rectangle"; }
    bool closed() const override { return true; }
    Points to_points(int seg = 64) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Rectangle>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Rectangle&>(o); }
public:
};

// ======================================================================
// Polilinha (tambem usada para poligonos, estrelas, desenho livre)
// ======================================================================
class Polyline : public Entity {
public:
    Points points;
    bool is_closed = false;

    EntityKind kind() const override { return EntityKind::Polyline; }
    const char* type_name() const override { return "Polyline"; }
    bool closed() const override { return is_closed; }
    Points to_points(int seg = 64) const override { (void)seg; return points; }
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Polyline>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Polyline&>(o); }
public:
};

// ======================================================================
// Texto
// ======================================================================
class Text : public Entity {
public:
    double x = 0.0, y = 0.0;
    std::string content = "Texto";
    double height = 20.0;
    std::string font_family = "Arial";
    bool bold = false;
    bool italic = false;
    std::string align = "left";  // left | center | right
    double rotation = 0.0;

    EntityKind kind() const override { return EntityKind::Text; }
    const char* type_name() const override { return "Text"; }
    bool closed() const override { return true; }

    // Converte o texto em contornos (usa QPainterPath do Qt). Precisa de um
    // QGuiApplication vivo, como o PySide6 do app original.
    const std::vector<Points>& outline_polygons() const;
    void invalidate() { cached_.reset(); }
    // Fixa os contornos (em vez de pedi-los ao Qt) - para testes que precisam de formas conhecidas.
    void set_cached_outlines(std::vector<Points> polys) { cached_ = std::move(polys); }

    Points to_points(int seg = 64) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Text>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Text&>(o); }
public:

private:
    mutable std::optional<std::vector<Points>> cached_;
};

// ======================================================================
// Cota / Dimensao
// ======================================================================

// Aparencia das cotas; medidas graficas sao em milimetros no papel.
struct DimensionStyle {
    std::optional<std::string> line_color;
    std::optional<std::string> extension_color;
    std::optional<std::string> text_color;
    double text_height = 2.5;
    double arrow_size = 2.5;
    double line_width = 0.18;
    double extension_gap = 1.0;
    double extension_overshoot = 1.5;
    double text_gap = 1.0;
    std::string arrow_style = "closed";
    std::string arrow_fit = "auto";
    int precision = 2;
    bool show_unit = true;
    bool alternate_units = false;
    int alternate_precision = 2;
    std::string tolerance_mode = "none";
    double tolerance_upper = 0.0;
    double tolerance_lower = 0.0;
};

using Seg = std::pair<Pt, Pt>;

class Dimension : public Entity {
public:
    double x1 = 0.0, y1 = 0.0, x2 = 100.0, y2 = 0.0;
    double offset = 15.0;
    std::string kind_name = "aligned";  // campo "kind" do Python
    double x3 = 0.0, y3 = 0.0;
    std::optional<std::string> source_id;
    DimensionStyle style;

    EntityKind kind() const override { return EntityKind::Dimension; }
    const char* type_name() const override { return "Dimension"; }

    double measure() const;
    // Extremidades da linha de cota linear no sistema de mundo.
    std::pair<Pt, Pt> dimension_ends() const;
    // (ponta, cauda) de cada marca de extremidade.
    std::vector<std::pair<Pt, Pt>> arrow_placements() const;
    // Tracos independentes da cota, evitando unir extensoes no desenho.
    std::vector<Seg> segments() const;
    Pt label_position() const;
    std::string format_label(const std::string& units = "mm", double unit_factor = 1.0) const;

    Points to_points(int seg = 64) const override;
    double distance_to(double x, double y) const override;
    void translate(double dx, double dy) override;
    void rotate(double deg, double cx, double cy) override;
    void scale(double sx, double sy, double cx, double cy) override;
    EntityPtr copy() const override { return std::make_shared<Dimension>(*this); }
protected:
    void assign_from(const Entity& o) override { *this = static_cast<const Dimension&>(o); }
public:

private:
    double linear_label_width(const std::string& units = "mm", double unit_factor = 1.0) const;
};

// ======================================================================
// Helpers de construcao
// ======================================================================

// O que o Python passa por **kw (layer=..., color=...) nos construtores.
struct EntityStyle {
    std::optional<std::string> layer;
    std::optional<Rgb> color;
};

std::shared_ptr<Polyline> make_polygon(double cx, double cy, double radius, int sides,
                                       double rotation = 0.0, const EntityStyle& style = {});
std::shared_ptr<Polyline> make_star(double cx, double cy, double r_out, int points = 5,
                                    double inner_ratio = 0.4, double rotation = 0.0,
                                    const EntityStyle& style = {});

// Atalhos de tipo, no lugar do isinstance do Python.
template <class T>
inline T* as(const EntityPtr& e) { return dynamic_cast<T*>(e.get()); }
template <class T>
inline const T* as(const Entity& e) { return dynamic_cast<const T*>(&e); }
template <class T>
inline bool is_a(const Entity& e) { return dynamic_cast<const T*>(&e) != nullptr; }
template <class T>
inline bool is_a(const EntityPtr& e) { return dynamic_cast<const T*>(e.get()) != nullptr; }

}  // namespace cnc::cad
