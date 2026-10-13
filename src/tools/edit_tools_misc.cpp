// Porte de agrupar, converter texto, fechar e alinhar (src/tools/edit_tools.py).
#include <algorithm>
#include <format>
#include <map>

#include <QString>

#include "tools/edit_tools.hpp"

namespace cnc::tools {

// ======================================================================
// Agrupar / Converter texto
// ======================================================================

void GroupTool::run() {
    const auto sel = ctx->selection();
    if (sel.size() < 2) {
        ctx->status("Selecione ao menos 2 objetos");
        return;
    }
    const std::string gid = new_uuid_hex();
    ctx->transform(sel, [gid](cad::Entity& e) { e.group = gid; }, "Agrupar");
    ctx->status(std::format("{} objetos agrupados", sel.size()));
}

void UngroupTool::run() {
    std::vector<cad::EntityPtr> grouped;
    for (const cad::EntityPtr& e : ctx->selection()) {
        if (e->group && !e->group->empty()) grouped.push_back(e);
    }
    const size_t n = grouped.size();
    if (!grouped.empty()) ctx->transform(grouped, [](cad::Entity& e) { e.group.reset(); }, "Desagrupar");
    ctx->status(n ? std::format("{} objeto(s) desagrupado(s)", n) : std::string("Nada agrupado"));
}

void ConvertTextTool::run() {
    std::vector<std::shared_ptr<cad::Text>> sel;
    for (const cad::EntityPtr& e : ctx->selection()) {
        if (auto t = std::dynamic_pointer_cast<cad::Text>(e)) sel.push_back(t);
    }
    if (sel.empty()) {
        ctx->status("Selecione um objeto de texto");
        return;
    }
    std::vector<cad::EntityPtr> neu;
    for (const auto& t : sel) {
        for (const Points& poly : t->outline_polygons()) {
            if (poly.size() >= 3) {
                auto p = std::make_shared<cad::Polyline>();
                p->points = poly;
                p->is_closed = true;
                p->layer = t->layer;
                p->color = t->color;
                neu.push_back(p);
            }
        }
    }
    if (neu.empty()) {
        ctx->status("Texto sem contornos");
        return;
    }
    const size_t count = neu.size();
    ctx->replace(std::vector<cad::EntityPtr>(sel.begin(), sel.end()), neu, "Converter texto");
    ctx->status(std::format("Texto convertido em {} vetor(es)", count));
}

namespace {

// str.strip() do Python decide pelo conjunto de espacos do Unicode (inclui separadores de campo).
bool is_py_space(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool is_blank(const std::string& s) {
    const QString q = QString::fromStdString(s);
    for (const QChar ch : q) {
        if (!is_py_space(ch.unicode())) return false;
    }
    return true;
}

}  // namespace

void SplitTextLinesTool::run() {
    std::vector<std::shared_ptr<cad::Text>> sel, multi;
    for (const cad::EntityPtr& e : ctx->selection()) {
        if (auto t = std::dynamic_pointer_cast<cad::Text>(e)) {
            sel.push_back(t);
            if (t->content.find('\n') != std::string::npos) multi.push_back(t);
        }
    }
    if (sel.empty()) {
        ctx->status("Selecione um objeto de texto");
        return;
    }
    if (multi.empty()) {
        ctx->status("O texto selecionado já tem uma linha só");
        return;
    }
    std::vector<cad::EntityPtr> novos;
    for (const auto& t : multi) {
        std::vector<std::string> linhas;
        size_t start = 0;
        for (;;) {
            const size_t pos = t->content.find('\n', start);
            if (pos == std::string::npos) {
                linhas.push_back(t->content.substr(start));
                break;
            }
            linhas.push_back(t->content.substr(start, pos - start));
            start = pos + 1;
        }
        const double passo = t->height * kLineSpacing;
        for (size_t i = 0; i < linhas.size(); ++i) {
            if (is_blank(linhas[i])) continue;
            auto copia = std::static_pointer_cast<cad::Text>(t->clone());
            copia->content = linhas[i];
            // A primeira linha fica onde estava; as de baixo descem.
            copia->y = t->y - static_cast<double>(i) * passo;
            // O contorno vem do texto e de onde ele esta: o da copia ainda e o do bloco inteiro.
            copia->invalidate();
            novos.push_back(copia);
        }
    }
    if (novos.empty()) {
        ctx->status("O texto não tem linha com conteúdo");
        return;
    }
    const size_t count = novos.size();
    ctx->replace(std::vector<cad::EntityPtr>(multi.begin(), multi.end()), novos, "Separar texto em linhas");
    ctx->status(std::format("{} bloco(s) → {} linha(s)", multi.size(), count));
}

void CloseVectorsTool::run() {
    std::vector<cad::EntityPtr> sel;
    for (const cad::EntityPtr& e : ctx->selection()) {
        const auto* poly = dynamic_cast<const cad::Polyline*>(e.get());
        if (poly && !poly->is_closed && poly->points.size() >= 3) sel.push_back(e);
    }
    if (sel.empty()) {
        bool any_open = false;
        for (const cad::EntityPtr& e : ctx->selection()) any_open = any_open || !e->closed();
        if (any_open) {
            ctx->status("Só dá para fechar polilinha com 3 nós ou mais. Para juntar pontas de vetores diferentes use "
                        "Unir (J).");
        } else {
            ctx->status("Selecione os vetores abertos a fechar");
        }
        return;
    }
    ctx->transform(sel, [](cad::Entity& e) { static_cast<cad::Polyline&>(e).is_closed = true; }, "Fechar vetores");
    ctx->status(std::format("{} vetor(es) fechado(s)", sel.size()));
}

// ======================================================================
// Alinhamento
// ======================================================================

AlignTool::AlignTool(ToolContext& ctx, const std::string& mode) : BaseTool(ctx), mode_(mode) {
    static const std::map<std::string, std::string> labels = {
        {"left", "Alinhar à esquerda"},          {"center_h", "Centralizar horizontalmente"},
        {"right", "Alinhar à direita"},          {"top", "Alinhar ao topo"},
        {"center_v", "Centralizar verticalmente"}, {"bottom", "Alinhar à base"},
    };
    name_ = "align_" + mode;
    const auto it = labels.find(mode);
    label_ = it != labels.end() ? it->second : "Alinhar";
}

namespace {

BBox union_box(const std::vector<cad::EntityPtr>& entities) {
    BBox box = entities.front()->bounding_box();
    for (size_t i = 1; i < entities.size(); ++i) {
        const BBox b = entities[i]->bounding_box();
        box.x0 = std::min(box.x0, b.x0);
        box.y0 = std::min(box.y0, b.y0);
        box.x1 = std::max(box.x1, b.x1);
        box.y1 = std::max(box.y1, b.y1);
    }
    return box;
}

}  // namespace

void AlignTool::run() {
    const auto sel = ctx->selection();
    if (sel.size() < 2) {
        align_to_material(sel);
        return;
    }

    const cad::EntityPtr reference = sel.back();
    std::vector<cad::EntityPtr> group;
    if (reference->group && !reference->group->empty()) {
        for (const cad::EntityPtr& e : sel) {
            if (e->group == reference->group) group.push_back(e);
        }
    }
    if (group.empty()) group = {reference};
    const BBox bounds = union_box(group);
    std::vector<cad::EntityPtr> movers;
    for (const cad::EntityPtr& e : sel) {
        if (std::find(group.begin(), group.end(), e) == group.end()) movers.push_back(e);
    }
    apply(movers, bounds.x0, bounds.y0, bounds.x1, bounds.y1);
}

void AlignTool::align_to_material(const std::vector<cad::EntityPtr>& sel) {
    if (sel.empty()) {
        ctx->status("Nenhum objeto selecionado");
        return;
    }
    const cad::MaterialSetup& mat = ctx->document->material;
    apply(sel, 0.0, 0.0, mat.width, mat.height);
}

void AlignTool::apply(const std::vector<cad::EntityPtr>& sel, double gx0, double gy0, double gx1, double gy1) {
    // Membros de um grupo sao alinhados como um unico objeto.
    std::map<std::string, std::vector<cad::EntityPtr>> groups;
    for (const cad::EntityPtr& e : sel) {
        groups[(e->group && !e->group->empty()) ? *e->group : e->id].push_back(e);
    }
    std::map<std::string, BBox> boxes_by_id;
    for (const auto& [key, members] : groups) {
        const BBox box = union_box(members);
        for (const cad::EntityPtr& e : members) boxes_by_id[e->id] = box;
    }
    std::vector<std::pair<cad::EntityPtr, Pt>> pairs;
    for (const cad::EntityPtr& e : sel) {
        const BBox b = boxes_by_id.at(e->id);
        double dx = 0.0, dy = 0.0;
        if (mode_ == "left") {
            dx = gx0 - b.x0;
        } else if (mode_ == "right") {
            dx = gx1 - b.x1;
        } else if (mode_ == "center_h") {
            dx = (gx0 + gx1) / 2 - (b.x0 + b.x1) / 2;
        } else if (mode_ == "bottom") {
            dy = gy0 - b.y0;
        } else if (mode_ == "top") {
            dy = gy1 - b.y1;
        } else if (mode_ == "center_v") {
            dy = (gy0 + gy1) / 2 - (b.y0 + b.y1) / 2;
        }
        pairs.emplace_back(e, Pt{dx, dy});
    }

    ctx->undo_stack->push(std::make_shared<AlignCommand>(*ctx->document, std::move(pairs), label_));
    ctx->refresh();
    ctx->status(label_);
}

void AlignCommand::execute() {
    for (const auto& [e, d] : pairs_) e->translate(d.x, d.y);
    doc_.modified = true;
}

void AlignCommand::undo() {
    for (const auto& [e, d] : pairs_) e->translate(-d.x, -d.y);
    doc_.modified = true;
}

}  // namespace cnc::tools
