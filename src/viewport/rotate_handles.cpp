// Porte de src/viewport/rotate_handles.py.
#include "viewport/rotate_handles.hpp"

#include <algorithm>
#include <stdexcept>

#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPen>

#include "viewport/canvas_2d.hpp"

namespace cnc::vp {

namespace {

// Branco das alcas, com borda escura para contrastar com o fundo.
constexpr const char* kWhiteFill = "#ffffff";
constexpr const char* kDarkEdge = "#1b2430";
// Caixa envolvente - cinza fraco, pontilhada.
constexpr const char* kBoxColor = "#8899aa";

// Pivo fixo da escala: o canto/lado oposto ao que foi arrastado.
Pt canto_oposto(HandleKind kind, const BBox& b) {
    const double cx = (b.x0 + b.x1) / 2.0, cy = (b.y0 + b.y1) / 2.0;
    switch (kind) {
        case HandleKind::ScaleTL: return {b.x1, b.y0};
        case HandleKind::ScaleTR: return {b.x0, b.y0};
        case HandleKind::ScaleBR: return {b.x0, b.y1};
        case HandleKind::ScaleBL: return {b.x1, b.y1};
        case HandleKind::ScaleT: return {cx, b.y0};
        case HandleKind::ScaleB: return {cx, b.y1};
        case HandleKind::ScaleL: return {b.x1, cy};
        case HandleKind::ScaleR: return {b.x0, cy};
        default: break;
    }
    throw std::logic_error("canto_oposto: so alcas de escala tem pivo");
}

// Canto/lado real da caixa referente a esta alca (sem o afastamento).
Pt canto_bbox(HandleKind kind, const BBox& b) {
    const double cx = (b.x0 + b.x1) / 2.0, cy = (b.y0 + b.y1) / 2.0;
    switch (kind) {
        case HandleKind::ScaleTL: return {b.x0, b.y1};
        case HandleKind::ScaleT: return {cx, b.y1};
        case HandleKind::ScaleTR: return {b.x1, b.y1};
        case HandleKind::ScaleL: return {b.x0, cy};
        case HandleKind::ScaleR: return {b.x1, cy};
        case HandleKind::ScaleBL: return {b.x0, b.y0};
        case HandleKind::ScaleB: return {cx, b.y0};
        case HandleKind::ScaleBR: return {b.x1, b.y0};
        case HandleKind::RotateTL: return {b.x0, b.y1};
        case HandleKind::RotateTR: return {b.x1, b.y1};
        case HandleKind::RotateBR: return {b.x1, b.y0};
        case HandleKind::RotateBL: return {b.x0, b.y0};
    }
    return {cx, cy};
}

// Desloca a marca para fora da caixa envolvente, na direcao certa.
Pt afastar(HandleKind kind, const BBox& b, double offset_mm) {
    int dx = 0, dy = 0;
    switch (kind) {
        case HandleKind::ScaleTL: case HandleKind::RotateTL: dx = -1; dy = +1; break;
        case HandleKind::ScaleT: dx = 0; dy = +1; break;
        case HandleKind::ScaleTR: case HandleKind::RotateTR: dx = +1; dy = +1; break;
        case HandleKind::ScaleL: dx = -1; dy = 0; break;
        case HandleKind::ScaleR: dx = +1; dy = 0; break;
        case HandleKind::ScaleBL: case HandleKind::RotateBL: dx = -1; dy = -1; break;
        case HandleKind::ScaleB: dx = 0; dy = -1; break;
        case HandleKind::ScaleBR: case HandleKind::RotateBR: dx = +1; dy = -1; break;
    }
    const Pt base = canto_bbox(kind, b);
    return {base.x + dx * offset_mm, base.y + dy * offset_mm};
}

QPen cosmetic_pen(const QColor& color, double width) {
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}

}  // namespace

const char* handle_name(HandleKind k) {
    switch (k) {
        case HandleKind::ScaleTL: return "scale-tl";
        case HandleKind::ScaleT: return "scale-t";
        case HandleKind::ScaleTR: return "scale-tr";
        case HandleKind::ScaleL: return "scale-l";
        case HandleKind::ScaleR: return "scale-r";
        case HandleKind::ScaleBL: return "scale-bl";
        case HandleKind::ScaleB: return "scale-b";
        case HandleKind::ScaleBR: return "scale-br";
        case HandleKind::RotateTL: return "rotate-tl";
        case HandleKind::RotateTR: return "rotate-tr";
        case HandleKind::RotateBR: return "rotate-br";
        case HandleKind::RotateBL: return "rotate-bl";
    }
    return "";
}

void SelectionHandles::clear() {
    for (QGraphicsItem* item : items_) {
        canvas_->scene()->removeItem(item);
        delete item;
    }
    items_.clear();
    positions_.clear();
    anchors_.clear();
    bbox_.reset();
}

void SelectionHandles::show(const BBox& bbox, bool bulk_selection) {
    clear();
    bbox_ = bbox;
    const double x0 = bbox.x0, y0 = bbox.y0, x1 = bbox.x1, y1 = bbox.y1;

    QPen caixa_pen(QColor(bulk_selection ? "#ff8c00" : kBoxColor), bulk_selection ? 1.5 : 1.0, Qt::DashLine);
    caixa_pen.setCosmetic(true);
    const QPointF canto = canvas_->world_to_scene(x0, y1);
    QGraphicsRectItem* caixa = canvas_->scene()->addRect(0, 0, x1 - x0, y1 - y0, caixa_pen);
    caixa->setPos(canto);
    caixa->setZValue(199);
    items_.push_back(caixa);

    // Ancoras de ancoragem: so os quatro cantos servem de ponto de agarre. Centro e meios de lado
    // ficam de fora de proposito: clicar no meio da peca e o gesto de "pegar e mover", e nao pode
    // prender o encaixe a um ponto que nao tem alvo nenhum por perto.
    for (const Pt& p : {Pt{x0, y1}, Pt{x1, y1}, Pt{x0, y0}, Pt{x1, y0}}) {
        anchors_.push_back({p, "nó"});
    }

    const double px_mm = std::max(1e-6, canvas_->px_per_mm());
    const double off_scale = kScaleOffsetPx / px_mm;
    const double off_rot = kRotOffsetPx / px_mm;

    // Alcas de escala - um pouco fora da caixa, nas 8 direcoes.
    for (HandleKind kind : {HandleKind::ScaleTL, HandleKind::ScaleT, HandleKind::ScaleTR, HandleKind::ScaleL,
                            HandleKind::ScaleR, HandleKind::ScaleBL, HandleKind::ScaleB, HandleKind::ScaleBR}) {
        const Pt p = afastar(kind, bbox, off_scale);
        positions_[kind] = p;
        items_.push_back(square(p.x, p.y));
    }

    // Pontos azuis de rotacao - mais afastados ainda, so nos cantos.
    for (HandleKind kind : {HandleKind::RotateTL, HandleKind::RotateTR, HandleKind::RotateBR, HandleKind::RotateBL}) {
        const Pt p = afastar(kind, bbox, off_rot);
        positions_[kind] = p;
        items_.push_back(blue_dot(p.x, p.y));
    }
}

QGraphicsItem* SelectionHandles::square(double wx, double wy) {
    const double lado = kScalePx;
    auto* item = new QGraphicsRectItem(-lado / 2, -lado / 2, lado, lado);
    item->setPen(cosmetic_pen(QColor(kDarkEdge), 1.0));
    item->setBrush(QBrush(QColor(kWhiteFill)));
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    item->setZValue(201);
    item->setPos(canvas_->world_to_scene(wx, wy));
    canvas_->scene()->addItem(item);
    return item;
}

QGraphicsItem* SelectionHandles::blue_dot(double wx, double wy) {
    const double r = kRotPx / 2.0;
    auto* item = new QGraphicsEllipseItem(-r, -r, kRotPx, kRotPx);
    item->setPen(cosmetic_pen(QColor("#0a1f8c"), 1.0));
    item->setBrush(QBrush(QColor(kRotateColor)));
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    item->setZValue(202);
    item->setPos(canvas_->world_to_scene(wx, wy));
    canvas_->scene()->addItem(item);
    return item;
}

std::optional<HandleHit> SelectionHandles::hit(double wx, double wy) const {
    if (positions_.empty() || !bbox_) return std::nullopt;
    const double px_mm = std::max(1e-6, canvas_->px_per_mm());
    const double tolerancia = kHitPx / px_mm;
    std::optional<HandleHit> melhor;
    double menor = tolerancia;
    // std::map percorre na ordem do enum, que e a ordem de insercao do Python (escala, depois
    // rotacao): so vale para empates, mas empate e justamente onde a ordem decide.
    for (const auto& [kind, p] : positions_) {
        const double d = hypot(wx - p.x, wy - p.y);
        if (d < menor) {
            menor = d;
            const Pt b = canto_bbox(kind, *bbox_);
            melhor = HandleHit{kind, p.x, p.y, b.x, b.y};
        }
    }
    return melhor;
}

std::optional<AnchorHit> SelectionHandles::anchor_at(double wx, double wy) const {
    if (anchors_.empty()) return std::nullopt;
    const double px_mm = std::max(1e-6, canvas_->px_per_mm());
    const double tolerancia = kHitPx / px_mm;
    std::optional<AnchorHit> melhor;
    double menor = tolerancia;
    for (const AnchorHit& a : anchors_) {
        const double d = hypot(wx - a.point.x, wy - a.point.y);
        if (d < menor) {
            menor = d;
            melhor = a;
        }
    }
    return melhor;
}

Pt SelectionHandles::scale_pivot(HandleKind kind) const {
    if (!is_scale(kind) || !bbox_) throw std::logic_error("scale_pivot: alca de escala e caixa sao obrigatorias");
    return canto_oposto(kind, *bbox_);
}

Pt SelectionHandles::rotate_pivot() const {
    if (!bbox_) throw std::logic_error("rotate_pivot: sem caixa");
    return {(bbox_->x0 + bbox_->x1) / 2.0, (bbox_->y0 + bbox_->y1) / 2.0};
}

std::pair<bool, bool> SelectionHandles::axes(HandleKind kind) {
    const bool em_x = kind == HandleKind::ScaleL || kind == HandleKind::ScaleR || kind == HandleKind::ScaleTL ||
                      kind == HandleKind::ScaleTR || kind == HandleKind::ScaleBL || kind == HandleKind::ScaleBR;
    const bool em_y = kind == HandleKind::ScaleT || kind == HandleKind::ScaleB || kind == HandleKind::ScaleTL ||
                      kind == HandleKind::ScaleTR || kind == HandleKind::ScaleBL || kind == HandleKind::ScaleBR;
    return {em_x, em_y};
}

}  // namespace cnc::vp
