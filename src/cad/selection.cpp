// Porte de src/cad/selection.py.
#include "cad/selection.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "cad/document.hpp"

namespace cnc::cad {

bool is_cota_layer(const std::string& layer) {
    std::string lower = layer;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "cotas" || lower == "cota" || lower == "dimensions" || lower == "dimension";
}

bool is_vector(const Entity& e) {
    if (is_cota_layer(e.layer)) return false;
    if (is_a<Dimension>(e)) return false;
    if (auto* cl = as<ConstructionLine>(e)) return cl->machinable;
    return !is_a<DrillPoint>(e);
}

std::vector<EntityPtr> machinable(const std::vector<EntityPtr>& items) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : items) {
        if (!e) continue;
        if (is_cota_layer(e->layer) || is_a<Dimension>(*e)) continue;
        if (is_vector(*e)) out.push_back(e);
    }
    return out;
}

std::vector<EntityPtr> machinable_for(const std::vector<EntityPtr>& items, cam::ToolpathType kind) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : items) {
        if (!e) continue;
        if (is_cota_layer(e->layer) || is_a<Dimension>(*e)) continue;
        if (kind == cam::ToolpathType::Drill) {
            if (is_a<DrillPoint>(*e) || is_vector(*e)) out.push_back(e);
        } else {
            if (is_vector(*e)) out.push_back(e);
        }
    }
    return out;
}

std::vector<EntityPtr> not_machinable(const std::vector<EntityPtr>& items) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : items) {
        if (!e) continue;
        if (is_cota_layer(e->layer) || is_a<Dimension>(*e) || !is_vector(*e)) out.push_back(e);
    }
    return out;
}

std::string why_not_machinable(const std::vector<EntityPtr>& items) {
    const std::vector<EntityPtr> descartados = not_machinable(items);
    if (descartados.empty()) return "";
    int guias = 0, pontos = 0, cotas = 0;
    for (const EntityPtr& e : descartados) {
        if (is_a<ConstructionLine>(e)) ++guias;
        else if (is_a<DrillPoint>(e)) ++pontos;
        else if (is_a<Dimension>(e) || is_cota_layer(e->layer)) ++cotas;
    }
    std::vector<std::string> partes;
    if (guias) partes.push_back(std::to_string(guias) + " guia(s) de régua");
    if (cotas) partes.push_back(std::to_string(cotas) + " cota(s) ou elemento(s) na camada Cotas (bloqueados para percurso)");
    if (pontos) partes.push_back(std::to_string(pontos) + " ponto(s) exclusivo(s) de furação");
    std::string out;
    for (size_t i = 0; i < partes.size(); ++i) {
        if (i) out += " e ";
        out += partes[i];
    }
    return out;
}

std::vector<EntityPtr> selectable(const Document& document) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : document.entities) {
        if (is_vector(*e) && document.is_editable(*e)) out.push_back(e);
    }
    return out;
}

std::vector<EntityPtr> open_vectors(const Document& document) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : selectable(document)) {
        if (!e->closed()) out.push_back(e);
    }
    return out;
}

std::vector<EntityPtr> closed_vectors(const Document& document) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : selectable(document)) {
        if (e->closed()) out.push_back(e);
    }
    return out;
}

std::vector<EntityPtr> vectors_on_layer(const Document& document, const std::string& layer) {
    std::vector<EntityPtr> out;
    for (const EntityPtr& e : selectable(document)) {
        if (e->layer == layer) out.push_back(e);
    }
    return out;
}

namespace {

// Chave que dois vetores sobrepostos compartilham. Usa o CONJUNTO dos pontos arredondados,
// nao a lista: duas copias da mesma peca podem comecar em nos diferentes e ainda ocupar
// exatamente o mesmo lugar na chapa - que e o caso que interessa.
using Signature = std::tuple<std::string, bool, size_t, std::vector<std::pair<double, double>>>;

Signature signature(const Entity& e, double tol) {
    const int casas = std::max(0, static_cast<int>(std::to_string(static_cast<int>(1 / tol)).size()) - 1);
    std::vector<std::pair<double, double>> pts;
    for (const Pt& p : e.to_points(64)) {
        double x = py_round(p.x, casas), y = py_round(p.y, casas);
        if (x == 0.0) x = 0.0;  // -0.0 e 0.0 sao o mesmo ponto
        if (y == 0.0) y = 0.0;
        pts.emplace_back(x, y);
    }
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    return {e.type_name(), e.closed(), pts.size(), std::move(pts)};
}

}  // namespace

std::vector<EntityPtr> duplicate_vectors(const Document& document, double tol) {
    std::set<Signature> vistos;
    std::vector<EntityPtr> repetidos;
    for (const EntityPtr& e : selectable(document)) {
        Signature chave = signature(*e, tol);
        if (vistos.count(chave)) {
            repetidos.push_back(e);
        } else {
            vistos.insert(std::move(chave));
        }
    }
    return repetidos;
}

int apply_selection(Document& document, const std::vector<EntityPtr>& items, bool adicionar) {
    if (!adicionar) document.clear_selection();
    for (const EntityPtr& e : items) document.set_selected(e, true);
    return static_cast<int>(items.size());
}

}  // namespace cnc::cad
