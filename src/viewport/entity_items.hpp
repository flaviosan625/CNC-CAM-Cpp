// Conversao de entidades CAD para itens graficos Qt (canvas 2D)
// (porte de src/viewport/entity_items.py).
//
// O Y da cena e invertido em relacao ao mundo: scene_y = material_height - world_y.
#pragma once

#include <optional>
#include <string>
#include <utility>

#include <QColor>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>

#include "cad/entities.hpp"

namespace cnc::vp {

// Teste de clique: o ponto (x, y) de mundo esta a `tolerance` do contorno?
bool hit_test(const cad::Entity& e, double x, double y, double tolerance);

// Testa o contorno exibido contra uma janela de selecao (mundo), incluindo subcontornos
// independentes de texto. `crossing` = janela da direita para a esquerda (toca basta).
bool in_selection_window(const cad::Entity& e, double x0, double y0, double x1, double y1, bool crossing);

// QPainterPath da entidade em coordenadas de cena.
QPainterPath build_path(const cad::Entity& e, double mh);

// Linha de cota + setas, e linhas de extensao, em caminhos separados.
std::pair<QPainterPath, QPainterPath> build_dimension_paths(const cad::Dimension& dimension, double mh);

// Item grafico que representa uma entidade do documento.
class EntityItem : public QGraphicsPathItem {
public:
    EntityItem(cad::EntityPtr entity, double mh, double unit_factor = 1.0, std::string units = "mm",
               std::optional<Rgb> layer_color = std::nullopt);

    const cad::EntityPtr& entity() const { return entity_; }
    // So existem em cotas: o rotulo com o valor e as linhas de extensao.
    QGraphicsSimpleTextItem* dimension_label() const { return dimension_label_; }
    QGraphicsPathItem* extension_item() const { return extension_item_; }
    void update_context(double mh, double unit_factor, const std::string& units, const Rgb& layer_color);
    BBox world_bounds() const;
    bool hit_test_world(double x, double y, double tolerance) const;
    void set_selection_highlight(bool enabled);
    void refresh();
    void adopt_preview_translation();
    void apply_style();

private:
    QColor layer_qcolor() const;

    cad::EntityPtr entity_;
    double mh_;
    double unit_factor_;
    std::string units_;
    Rgb layer_color_;
    uint64_t cached_version_ = 0;
    double cached_mh_ = -1.0;
    bool context_dirty_ = true;
    bool style_dirty_ = true;
    bool selection_highlight_ = true;
    bool cached_selected_ = false;
    mutable std::optional<BBox> cached_bounds_;
    mutable uint64_t bounds_version_ = 0;
    mutable std::optional<QPainterPath> cached_hit_path_;
    mutable double hit_tolerance_ = -1.0;
    QGraphicsSimpleTextItem* dimension_label_ = nullptr;
    QGraphicsPathItem* extension_item_ = nullptr;
};

}  // namespace cnc::vp
