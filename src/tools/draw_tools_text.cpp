// Porte das ferramentas de texto, cota e medicao de src/tools/draw_tools.py.
#include <algorithm>
#include <cmath>
#include <format>

#include "tools/draw_tools.hpp"

namespace cnc::tools {

// ======================================================================
// Texto
// ======================================================================

bool TextTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    auto t = std::make_shared<cad::Text>();
    t->x = wx;
    t->y = wy;
    t->content = content;
    t->height = height;
    t->font_family = font_family;
    t->bold = bold;
    t->italic = italic;
    t->align = align;
    if (t->outline_polygons().empty()) {
        ctx->status("Texto vazio — nada criado");
        return true;
    }
    ctx->add(t, "Texto");
    ctx->status("Texto \"" + content + "\" criado");
    return true;
}

bool TextOnCurveTool::on_press(double, double, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const auto sel = ctx->selection();
    cad::EntityPtr curve;
    for (const cad::EntityPtr& e : sel) {
        if (!cad::is_a<cad::Text>(*e)) {
            curve = e;
            break;
        }
    }
    if (!curve) {
        ctx->status("Selecione primeiro a curva-guia");
        return true;
    }

    Points path = curve->to_points(256);
    if (curve->closed() && !path.empty()) path.push_back(path.front());
    if (path.size() < 2) return true;

    std::vector<double> cum = {0.0};
    for (size_t i = 1; i < path.size(); ++i) {
        cum.push_back(cum.back() + hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y));
    }
    const double total = cum.back();
    if (total < 1e-6) return true;

    cad::Text base;
    base.x = 0;
    base.y = 0;
    base.content = content;
    base.height = height;
    base.font_family = font_family;
    const std::vector<Points>& polys = base.outline_polygons();
    if (polys.empty()) return true;
    bool first = true;
    double min_x = 0.0, max_x = 0.0;
    for (const Points& poly : polys) {
        for (const Pt& p : poly) {
            if (first) {
                min_x = max_x = p.x;
                first = false;
            } else {
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
            }
        }
    }
    const double tw = first ? 0.0 : max_x - min_x;
    if (tw < 1e-6) return true;

    const double start = std::max(0.0, std::min(total - tw, total / 2 - tw / 2));
    std::vector<cad::EntityPtr> out;
    for (const Points& poly : polys) {
        Points warped;
        for (const Pt& p : poly) {
            const double s = start + p.x - min_x;
            const auto [x, y, ang] = at(path, cum, s);
            const double ca = std::cos(ang), sa = std::sin(ang);
            warped.push_back({x - p.y * sa, y + p.y * ca});
        }
        auto polyline = std::make_shared<cad::Polyline>();
        polyline->points = std::move(warped);
        polyline->is_closed = true;
        out.push_back(polyline);
    }

    ctx->add(out, "Texto em curva");
    ctx->status("Texto aplicado à curva");
    return true;
}

std::tuple<double, double, double> TextOnCurveTool::at(const Points& path, const std::vector<double>& cum, double s) {
    s = std::max(0.0, std::min(cum.back(), s));
    size_t lo = 0, hi = cum.size() - 1;
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (cum[mid] <= s) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double seg = cum[hi] - cum[lo];
    const double t = seg > 1e-9 ? (s - cum[lo]) / seg : 0.0;
    const Pt a = path[lo], b = path[hi];
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, std::atan2(b.y - a.y, b.x - a.x)};
}

// ======================================================================
// Medicao
// ======================================================================

void MeasureTool::deactivate() {
    p0_.reset();
    BaseTool::deactivate();
}

bool MeasureTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return false;
    if (!p0_) {
        if (ctx->canvas) ctx->canvas->clear_measurement();
        p0_ = Pt{wx, wy};
        ctx->status("Clique no segundo ponto");
    } else {
        on_move(wx, wy, mods);
        if (ctx->canvas) {
            std::string text;
            for (const std::string& line : measurement_lines(wx, wy)) text += (text.empty() ? "" : "\n") + line;
            ctx->canvas->show_measurement(*p0_, {wx, wy}, text);
        }
        p0_.reset();
        ctx->clear_preview();
    }
    return true;
}

std::vector<std::string> MeasureTool::measurement_lines(double wx, double wy) const {
    const double dx = wx - p0_->x, dy = wy - p0_->y;
    const double d = hypot(dx, dy);
    const double ang = degrees(std::atan2(dy, dx));
    const cad::MaterialSetup& m = ctx->document->material;
    const double factor = m.unit_factor();
    const char* unit = m.units == "in" ? "pol" : "mm";
    return {
        std::format("Distância: {:.3f} {}", d / factor, unit),
        std::format("ΔX: {:.3f} {}    ΔY: {:.3f} {}", dx / factor, unit, dy / factor, unit),
        std::format("Ângulo: {:.2f}°", ang),
    };
}

bool MeasureTool::on_move(double wx, double wy, Qt::KeyboardModifiers) {
    if (!p0_) return false;
    ctx->preview({*p0_, {wx, wy}}, false);
    std::string text;
    for (const std::string& line : measurement_lines(wx, wy)) text += (text.empty() ? "" : "   ") + line;
    ctx->status(text);
    return true;
}

}  // namespace cnc::tools
