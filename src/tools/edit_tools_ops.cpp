// Porte das ferramentas de operacao sobre vetores de src/tools/edit_tools.py: booleanas, offset,
// filete, unir, contorno e ajuste de curvas.
#include <algorithm>
#include <cmath>
#include <format>

#include "cad/geometry.hpp"
#include "tools/edit_tools.hpp"

namespace cnc::tools {

// ======================================================================
// Booleanas
// ======================================================================

void BoolTool::run() {
    const auto raw = ctx->selection();
    std::vector<cad::EntityPtr> sel;
    for (const cad::EntityPtr& e : raw) {
        if (e->closed() && cad::to_shapely(*e)) sel.push_back(e);
    }
    if (sel.size() < min_sel()) {
        ctx->status(why_not(raw, sel));
        return;
    }
    const auto res = op(sel);
    if (res.size() == sel.size()) {
        // Nada se fundiu: os vetores nao se tocam. Trocar N por N so apagaria a geometria original
        // e devolveria a mesma coisa, com o usuario achando que funcionou.
        ctx->status(std::format("{}: os {} vetores não se tocam, então não há o que juntar. Sobreponha-os primeiro.",
                                label(), sel.size()));
        return;
    }
    ctx->replace(sel, res, label());
    ctx->status(std::format("{}: {} vetores → {} vetor(es)", label(), sel.size(), res.size()));
}

// Mensagem que diz o que falta, em vez de so repetir a regra.
std::string BoolTool::why_not(const std::vector<cad::EntityPtr>& raw, const std::vector<cad::EntityPtr>& closed) const {
    if (raw.empty()) return label() + ": selecione os vetores primeiro";
    const size_t open = raw.size() - closed.size();
    if (open) {
        return std::format("{} só trabalha com vetores FECHADOS, e {} dos {} selecionados estão abertos. Use 'Unir "
                           "vetores abertos' para fechá-los antes.",
                           label(), open, raw.size());
    }
    return std::format("{} precisa de pelo menos {} vetores fechados; há {}.", label(), min_sel(), closed.size());
}

std::vector<cad::EntityPtr> WeldTool::op(const std::vector<cad::EntityPtr>& sel) const { return cad::boolean_union(sel); }

std::vector<cad::EntityPtr> SubtractTool::op(const std::vector<cad::EntityPtr>& sel) const {
    const std::vector<cad::EntityPtr> base(sel.begin(), sel.end() - 1);
    return cad::boolean_subtract(base, {sel.back()});
}

std::vector<cad::EntityPtr> IntersectTool::op(const std::vector<cad::EntityPtr>& sel) const {
    return cad::boolean_intersect(sel);
}

// ======================================================================
// Offset
// ======================================================================

void OffsetTool::apply_form() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    std::vector<cad::EntityPtr> out;
    for (const cad::EntityPtr& e : sel) {
        for (cad::EntityPtr& r : cad::offset_entity(*e, distance, outward, join_style)) out.push_back(std::move(r));
    }
    if (out.empty()) {
        ctx->status("Offset não produziu resultado");
        return;
    }
    if (keep_original) {
        ctx->add(out, "Offset");
    } else {
        ctx->replace(sel, out, "Offset");
    }
    ctx->status(std::format("Offset de {:.2f} mm aplicado", distance));
}

// ======================================================================
// Filete
// ======================================================================

bool FilletTool::on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return false;
    const double tol = (ctx->canvas ? ctx->canvas->pick_tolerance() : 1.0) * 10;

    struct Best {
        double d;
        cad::EntityPtr ent;
        size_t idx;
        Points pts;
    };
    std::optional<Best> best;
    for (const cad::EntityPtr& e : ctx->document->entities) {
        if (!ctx->document->is_editable(*e)) continue;
        if (cad::is_a<cad::Text>(*e)) continue;
        Points pts = e->to_points(96);
        for (size_t i = 0; i < pts.size(); ++i) {
            const double d = hypot(wx - pts[i].x, wy - pts[i].y);
            if (d <= tol && (!best || d < best->d)) best = Best{d, e, i, pts};
        }
    }

    if (!best) {
        ctx->status("Clique mais perto de um canto");
        return true;
    }

    const Points new_pts = cad::fillet_corner(best->pts, static_cast<int>(best->idx), radius, 12, best->ent->closed());
    if (new_pts.size() == best->pts.size()) {
        ctx->status("Não foi possível arredondar esse canto");
        return true;
    }

    auto neu = std::make_shared<cad::Polyline>();
    neu->points = new_pts;
    neu->is_closed = best->ent->closed();
    neu->layer = best->ent->layer;
    neu->color = best->ent->color;
    ctx->replace({best->ent}, {neu}, "Filete");
    ctx->status(std::format("Filete R{:.2f} aplicado", radius));
    return true;
}

// ======================================================================
// Unir vetores
// ======================================================================

void JoinTool::run() {
    const auto raw = ctx->selection();
    std::vector<cad::EntityPtr> sel;
    for (const cad::EntityPtr& e : raw) {
        if (!e->closed()) sel.push_back(e);
    }
    if (sel.size() < 2) {
        const size_t closed = raw.size() - sel.size();
        if (closed && raw.size() >= 2) {
            ctx->status(std::format("Unir trabalha com vetores ABERTOS, e {} dos {} selecionados já estão fechados.",
                                    closed, raw.size()));
        } else {
            ctx->status("Selecione ao menos 2 vetores abertos");
        }
        return;
    }

    const auto res = cad::join_open_vectors(sel, tolerance);
    // Lista vazia tambem e "nao uniu": sem este caso, o replace abaixo trocaria os vetores por nada
    // e apagaria a selecao inteira.
    if (res.empty() || res.size() >= sel.size()) {
        const auto gap = smallest_gap(sel);
        if (!gap) {
            ctx->status("Os vetores não têm extremidades para unir");
        } else {
            ctx->status(std::format("Nada a unir: a folga menor entre extremidades é {:.2f} mm e a tolerância está "
                                    "em {:.2f} mm. Aumente a tolerância no painel.",
                                    *gap, tolerance));
        }
        return;
    }

    const auto closed_count = std::count_if(res.begin(), res.end(), [](const cad::EntityPtr& e) { return e->closed(); });
    std::string text = std::format("{} vetores → {} vetor(es)", sel.size(), res.size());
    if (closed_count) text += std::format(", {} fechado(s)", closed_count);
    ctx->replace(sel, res, "Unir vetores");
    ctx->status(text);
}

// Distancia entre as duas extremidades livres mais proximas. Sem este numero o usuario so ve "nada
// aconteceu" e nao tem como saber qual tolerancia usar.
std::optional<double> JoinTool::smallest_gap(const std::vector<cad::EntityPtr>& entities) {
    std::vector<std::pair<const cad::Entity*, Pt>> ends;
    for (const cad::EntityPtr& e : entities) {
        const Points pts = e->to_points(8);
        if (pts.size() >= 2) {
            ends.emplace_back(e.get(), pts.front());
            ends.emplace_back(e.get(), pts.back());
        }
    }
    std::optional<double> smallest;
    for (size_t i = 0; i < ends.size(); ++i) {
        for (size_t j = i + 1; j < ends.size(); ++j) {
            if (ends[i].first == ends[j].first) continue;
            const double d = dist(ends[i].second, ends[j].second);
            if (!smallest || d < *smallest) smallest = d;
        }
    }
    return smallest;
}

// ======================================================================
// Contorno / Ajustar curvas
// ======================================================================

void BoundaryTool::run() {
    const auto sel = ctx->selection();
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    const auto res = cad::vector_boundary(sel, offset, rubber_band);
    if (res.empty()) {
        ctx->status("Não foi possível gerar o contorno");
        return;
    }
    ctx->add(res, "Contorno");
    ctx->status("Contorno criado");
}

void FitCurvesTool::run() {
    std::vector<cad::EntityPtr> sel;
    for (const cad::EntityPtr& e : ctx->selection()) {
        if (!cad::is_a<cad::Text>(*e)) sel.push_back(e);
    }
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    std::vector<cad::EntityPtr> neu, changed;
    for (const cad::EntityPtr& e : sel) {
        const Points pts = e->to_points(96);
        if (pts.size() < 3) continue;
        const geo::Geom geom = cad::to_shapely(*e);
        if (!geom) continue;
        changed.push_back(e);
        for (cad::EntityPtr& r : cad::from_shapely(geom.simplify(tolerance, true), e.get())) neu.push_back(std::move(r));
    }
    if (neu.empty()) {
        ctx->status("Nada para suavizar");
        return;
    }
    const size_t count = neu.size();
    if (keep_original) {
        ctx->add(neu, "Ajustar curvas");
    } else {
        ctx->replace(changed, neu, "Ajustar curvas");
    }
    ctx->status(std::format("{} vetor(es) suavizado(s)", count));
}

}  // namespace cnc::tools
