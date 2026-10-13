// Porte de DimensionTool (src/tools/draw_tools.py).
#include <algorithm>
#include <cmath>
#include <format>

#include "tools/draw_tools.hpp"

namespace cnc::tools {

namespace {

struct CircleLike {
    double cx, cy, radius;
};

std::optional<CircleLike> circle_like(const cad::Entity& e) {
    if (const auto* c = dynamic_cast<const cad::Circle*>(&e)) return CircleLike{c->cx, c->cy, c->radius};
    if (const auto* a = dynamic_cast<const cad::Arc*>(&e)) return CircleLike{a->cx, a->cy, a->radius};
    return std::nullopt;
}

}  // namespace

bool DimensionTool::is_linear(const std::string& m) {
    return m == "linear" || m == "aligned" || m == "horizontal" || m == "vertical";
}

bool DimensionTool::is_radial(const std::string& m) { return m == "diameter" || m == "radius"; }

void DimensionTool::set_mode(const std::string& value) {
    if (!(is_linear(value) || is_radial(value) || value == "angular")) return;
    if (value != mode_) {
        mode_ = value;
        reset();
        ctx->status(hint());
    }
}

std::string DimensionTool::hint() const {
    if (mode_ == "angular") return "Clique no vértice, nos dois lados do ângulo e na posição do arco";
    if (is_radial(mode_)) return "Clique no círculo ou arco e depois na posição da cota";
    return "Clique nos dois pontos; mova e clique para posicionar a cota";
}

void DimensionTool::deactivate() {
    reset();
    BaseTool::deactivate();
}

void DimensionTool::reset() {
    points_.clear();
    source_.reset();
    source_point_.reset();
    ctx->clear_preview();
}

cad::DimensionStyle DimensionTool::style() const { return ctx->document->dimension_style; }

std::shared_ptr<cad::Dimension> DimensionTool::linear_dimension(const std::optional<Pt>& position) const {
    const double x1 = points_[0].x, y1 = points_[0].y, x2 = points_[1].x, y2 = points_[1].y;
    const double dx = x2 - x1, dy = y2 - y1;
    std::string kind = mode_;
    if (kind == "linear") {
        if (std::fabs(dx) < 1e-9) {
            kind = "vertical";
        } else if (std::fabs(dy) < 1e-9) {
            kind = "horizontal";
        } else if (!position) {
            kind = "horizontal";
        } else {
            const Pt midpoint{(x1 + x2) / 2, (y1 + y2) / 2};
            const double horizontal_distance = std::fabs(position->y - midpoint.y);
            const double vertical_distance = std::fabs(position->x - midpoint.x);
            kind = horizontal_distance >= vertical_distance ? "horizontal" : "vertical";
        }
    }
    double off = offset;
    if (position) {
        if (kind == "horizontal") {
            off = position->y - y1;
        } else if (kind == "vertical") {
            off = position->x - x1;
        } else {
            off = ((position->x - x1) * -dy + (position->y - y1) * dx) / hypot(dx, dy);
        }
    }
    auto d = std::make_shared<cad::Dimension>();
    d->kind_name = kind;
    d->x1 = x1;
    d->y1 = y1;
    d->x2 = x2;
    d->y2 = y2;
    d->offset = off;
    d->style = style();
    return d;
}

std::shared_ptr<cad::Dimension> DimensionTool::angular_dimension(const std::optional<Pt>& position) const {
    const double radius = position ? dist(points_[0], *position) : offset;
    auto d = std::make_shared<cad::Dimension>();
    d->kind_name = "angular";
    d->x1 = points_[0].x;
    d->y1 = points_[0].y;
    d->x2 = points_[1].x;
    d->y2 = points_[1].y;
    d->x3 = points_[2].x;
    d->y3 = points_[2].y;
    d->offset = std::fabs(radius);
    d->style = style();
    return d;
}

std::shared_ptr<cad::Dimension> DimensionTool::radial_dimension(const Pt& position) const {
    const auto source = circle_like(*source_);
    auto d = std::make_shared<cad::Dimension>();
    d->kind_name = mode_;
    d->x1 = source->cx;
    d->y1 = source->cy;
    d->x2 = source_point_->x;
    d->y2 = source_point_->y;
    d->x3 = position.x;
    d->y3 = position.y;
    d->source_id = source_->id;
    d->offset = offset;
    d->style = style();
    return d;
}

void DimensionTool::report(const cad::Dimension& dimension, bool placed) {
    const int precision = std::max(0, std::min(6, dimension.style.precision));
    const double value = dimension.measure();
    std::string result;
    if (dimension.kind_name == "angular") {
        result = std::vformat("{:.{}f}°", std::make_format_args(value, precision));
    } else {
        const cad::MaterialSetup& material = ctx->document->material;
        const char* unit = material.units == "in" ? "pol" : "mm";
        const char* prefix = dimension.kind_name == "diameter" ? "Ø" : dimension.kind_name == "radius" ? "R" : "";
        const double shown = value / material.unit_factor();
        result = std::string(prefix) + std::vformat("{:.{}f} {}", std::make_format_args(shown, precision, unit));
    }
    ctx->status("Cota: " + result + (placed ? "" : " · clique para posicionar"));
}

cad::EntityPtr DimensionTool::pick_source(double wx, double wy) const {
    double tolerance = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * kPickPx;
    cad::EntityPtr closest;
    auto& entities = ctx->document->entities;
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (!circle_like(**it) || !ctx->document->is_editable(**it)) continue;
        const double distance = (*it)->distance_to(wx, wy);
        if (distance <= tolerance) {
            tolerance = distance;
            closest = *it;
        }
    }
    return closest;
}

bool DimensionTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;

    const Pt point{wx, wy};
    if (is_radial(mode_)) {
        if (!source_) {
            const cad::EntityPtr source = pick_source(wx, wy);
            if (!source) {
                ctx->status("Selecione o contorno de um círculo ou arco");
                return true;
            }
            const CircleLike circle = *circle_like(*source);
            if (circle.radius <= 1e-9) {
                ctx->status("O círculo ou arco precisa ter raio maior que zero");
                return true;
            }
            double dx = wx - circle.cx, dy = wy - circle.cy;
            double length = hypot(dx, dy);
            if (length < 1e-9) {
                dx = circle.radius;
                dy = 0.0;
                length = circle.radius;
            }
            source_ = source;
            source_point_ = Pt{circle.cx + circle.radius * dx / length, circle.cy + circle.radius * dy / length};
            const auto dimension = radial_dimension(point);
            ctx->preview_dimension(dimension);
            report(*dimension);
            return true;
        }
        commit(radial_dimension(point));
        return true;
    }

    const size_t required = mode_ == "angular" ? 3 : 2;
    if (points_.size() == required) {
        if (mode_ == "angular" && dist(points_[0], point) < 1e-9) {
            ctx->status("Posicione o arco fora do vértice");
            return true;
        }
        commit(mode_ == "angular" ? angular_dimension(point) : linear_dimension(point));
        return true;
    }
    if (!points_.empty() && dist(points_.back(), point) < 1e-9) {
        ctx->status("Escolha um ponto diferente");
        return true;
    }
    if (mode_ != "linear" && points_.size() == 1 &&
        ((mode_ == "horizontal" && std::fabs(wx - points_[0].x) < 1e-9) ||
         (mode_ == "vertical" && std::fabs(wy - points_[0].y) < 1e-9))) {
        ctx->status("A medida nessa direção é zero; escolha outro ponto");
        return true;
    }
    points_.push_back(point);
    if (points_.size() == required) {
        const auto dimension = mode_ == "angular" ? angular_dimension() : linear_dimension();
        ctx->preview_dimension(dimension);
        report(*dimension);
    } else if (mode_ == "angular") {
        ctx->status(points_.size() == 1 ? "Clique no primeiro lado" : "Clique no segundo lado");
    } else {
        ctx->status("Clique no segundo ponto");
    }
    return true;
}

bool DimensionTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    const Pt point{wx, wy};
    if (is_radial(mode_)) {
        if (!source_) return false;
        const auto dimension = radial_dimension(point);
        ctx->preview_dimension(dimension);
        report(*dimension);
        return true;
    }
    if (points_.empty()) return false;
    const size_t required = mode_ == "angular" ? 3 : 2;
    if (points_.size() == required) {
        const auto dimension = mode_ == "angular" ? angular_dimension(point) : linear_dimension(point);
        ctx->preview_dimension(dimension);
        report(*dimension);
    } else if (mode_ == "angular") {
        Points preview = points_;
        preview.push_back(point);
        ctx->preview(preview, false);
    } else {
        ctx->preview({points_[0], point}, false);
        if (dist(points_[0], point) > 1e-9) {
            cad::Dimension dimension;
            dimension.kind_name = mode_;
            dimension.x1 = points_[0].x;
            dimension.y1 = points_[0].y;
            dimension.x2 = wx;
            dimension.y2 = wy;
            dimension.offset = offset;
            dimension.style = style();
            report(dimension);
        }
    }
    return true;
}

void DimensionTool::commit(const std::shared_ptr<cad::Dimension>& dimension) {
    reset();
    ctx->add(dimension, "Cota");
    report(*dimension, true);
}

bool DimensionTool::on_key(int key) {
    if (key == Qt::Key_Escape) reset();
    return BaseTool::on_key(key);
}

}  // namespace cnc::tools
