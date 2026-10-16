// Alcas da selecao - rotacao, escala e marcacao de ancoragem (porte de src/viewport/rotate_handles.py).
//
// Tres conjuntos de marcas visuais aparecem ao redor da selecao com a ferramenta de selecao ativa:
//  1. Ancoras de ponto: pontinhos brancos sobre a caixa envolvente. Nao respondem a clique: servem so
//     para encostar pecas com precisao.
//  2. Alcas de escala: oito quadradinhos brancos um pouco FORA da caixa. Arrastar escala a partir do
//     canto/lado oposto, que e o pivo.
//  3. Pontos de rotacao: quatro pontos azuis mais afastados ainda, nos cantos externos. Arrastar gira a
//     selecao em torno do centro.
// A ordem dos afastamentos (dentro -> fora: ancora, escala, rotacao) mantem a leitura clara mesmo em
// zoom perto, onde as tres marcas de um canto se alinham na diagonal.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QGraphicsItem>

#include "geo/types.hpp"

namespace cnc::vp {

class Canvas2D;

// Azul do ponto de rotacao, igual ao do Aspire.
inline constexpr const char* kRotateColor = "#2f4ad3";
inline constexpr double kScalePx = 5.5;     // lado do quadradinho de escala, em pixels de tela
inline constexpr double kAnchorPx = 3.5;    // diametro do pontinho de ancora
inline constexpr double kRotPx = 5.0;       // diametro do ponto azul de rotacao
inline constexpr double kScaleOffsetPx = 11.0;  // afastamento da alca de escala para fora da caixa
inline constexpr double kRotOffsetPx = 23.0;    // afastamento do ponto azul de rotacao
// Tolerancia do clique na alca. Precisa ser menor que o afastamento da escala, senao a zona de captura
// cobre a borda da peca e arrastar o vetor vira redimensionar sem querer.
inline constexpr double kHitPx = 7.0;

enum class HandleKind {
    ScaleTL, ScaleT, ScaleTR, ScaleL, ScaleR, ScaleBL, ScaleB, ScaleBR,
    RotateTL, RotateTR, RotateBR, RotateBL,
};

inline bool is_rotate(HandleKind k) { return k >= HandleKind::RotateTL; }
inline bool is_scale(HandleKind k) { return k <= HandleKind::ScaleBR; }
const char* handle_name(HandleKind k);  // "scale-tl", "rotate-br", ...

struct HandleHit {
    HandleKind kind;
    double world_x;
    double world_y;
    // Canto/lado real da caixa envolvente, independente do afastamento visual.
    double bbox_x;
    double bbox_y;
};

struct AnchorHit {
    Pt point;
    std::string label;
};

// Alcas da selecao. O canvas mantem um objeto unico; show() repinta.
class SelectionHandles {
public:
    explicit SelectionHandles(Canvas2D* canvas) : canvas_(canvas) {}

    bool visible() const { return !items_.empty(); }
    const std::optional<BBox>& bbox() const { return bbox_; }
    void clear();
    void show(const BBox& bbox, bool bulk_selection = false);

    // Alca clicavel sob o cursor (so escala e rotacao respondem).
    std::optional<HandleHit> hit(double wx, double wy) const;
    // Ancora de ancoragem sob o cursor: diz por qual ponto a peca foi agarrada.
    std::optional<AnchorHit> anchor_at(double wx, double wy) const;
    // Canto/lado oposto da caixa, pivo fixo da escala.
    Pt scale_pivot(HandleKind kind) const;
    // Rotacao gira em volta do centro da caixa.
    Pt rotate_pivot() const;
    // Quais eixos escalam para esta alca: (x_livre, y_livre).
    static std::pair<bool, bool> axes(HandleKind kind);

private:
    QGraphicsItem* square(double wx, double wy);
    QGraphicsItem* blue_dot(double wx, double wy);

    Canvas2D* canvas_;
    std::vector<QGraphicsItem*> items_;
    std::map<HandleKind, Pt> positions_;
    std::vector<AnchorHit> anchors_;
    std::optional<BBox> bbox_;
};

}  // namespace cnc::vp
