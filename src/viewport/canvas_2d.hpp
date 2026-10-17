// Canvas 2D com reguas para o material (porte de src/viewport/canvas_2d.py).
//
// scene_y = material_height - world_y: o mundo e Y-para-cima com origem no canto inferior esquerdo
// da chapa, a cena do Qt e Y-para-baixo.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QToolButton>
#include <QWidget>

#include "cad/document.hpp"
#include "cad/layout.hpp"
#include "cam/model.hpp"
#include "tools/base.hpp"
#include "utils/shortcuts.hpp"
#include "viewport/entity_items.hpp"
#include "viewport/rotate_handles.hpp"
#include "viewport/snapping.hpp"

namespace cnc::vp {

inline constexpr int kRulerSize = 28;
// Tolerancia de clique em pixels, a mesma da selecao.
inline constexpr double kPickPx = 8.0;
// Empurrao das setas do teclado, em mm: normal, Ctrl (fino) e Shift (grosso).
inline constexpr double kNudgeMm = 1.0;
inline constexpr double kNudgeFineMm = 0.1;
inline constexpr double kNudgeCoarseMm = 10.0;
// Cinza da linha de margem - visivel no branco da chapa sem competir com o desenho.
inline constexpr const char* kMarginColor = "#b9b9b9";
// Laranja do snap - a mesma cor da selecao, para ler como "e isto aqui".
inline constexpr const char* kSnapColor = "#ff9f43";
// Cores dos percursos, uma por posicao na lista.
inline constexpr std::array<const char*, 6> kToolpathColors = {"#4fc3f7", "#81c784", "#ffb74d",
                                                               "#ba68c8", "#4db6ac", "#f06292"};

// Espacamento "redondo" (1, 2, 5, 10 x 10^n) para a regua.
double nice_spacing(double px_per_unit, double target_px = 100.0);
// Rotulo da regua: inteiro sem casas, senao uma casa decimal.
QString ruler_label(double v);

class Canvas2D;

// Ponte arrastavel; informa a nova posicao ao soltar o mouse ou exclusao com clique.
class TabMarker : public QGraphicsEllipseItem {
public:
    using MovedFn = std::function<void(int, const QPointF&)>;
    using DeletedFn = std::function<void(int)>;
    TabMarker(int index, MovedFn callback, DeletedFn delete_callback, bool manual);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    int index_;
    MovedFn callback_;
    DeletedFn delete_callback_;
    QPointF press_scene_pos_{};
};

// Regua que cria linha de construcao quando se arrasta dela para dentro do desenho.
class ConstructionRuler : public QWidget {
    Q_OBJECT
public:
    ConstructionRuler(Canvas2D* canvas, const QString& axis, QWidget* parent = nullptr);

signals:
    void construction_line_requested(const QString& axis, double position);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    Canvas2D* canvas_;

private:
    double world_position(const QPointF& local_pos) const;

    QString axis_;
    std::optional<QPointF> drag_start_;
    bool dragged_ = false;
};

class RulerH : public ConstructionRuler {
    Q_OBJECT
public:
    explicit RulerH(Canvas2D* canvas, QWidget* parent = nullptr);
    void set_cursor(double vx);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<double> cx_;
};

class RulerV : public ConstructionRuler {
    Q_OBJECT
public:
    RulerV(Canvas2D* canvas, double mat_h, QWidget* parent = nullptr);
    void set_cursor(double vy);
    void set_material_height(double h);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double mat_h_;
    std::optional<double> cy_;
};

// Ponte desenhada sobre o percurso: (id do vetor, x, y, manual).
struct TabPlacement {
    std::string entity_id;
    double x = 0.0;
    double y = 0.0;
    bool manual = false;
};

// Vista 2D do material com entidades, preview e ferramentas.
class Canvas2D : public QGraphicsView, public tools::ICanvas {
    Q_OBJECT
public:
    explicit Canvas2D(QWidget* parent = nullptr);

    // Guias sao mobilia da vista, nao geometria: esconde-las nao pode mexer no documento nem
    // entrar no desfazer.
    bool show_guides = true;
    bool show_grid = false;
    bool manual_tab_mode = false;
    bool manual_drill_mode = false;
    double material_width = 300.0;
    double material_height = 200.0;
    SnapEngine snap;
    shortcuts::Table shortcut_table;
    SelectionHandles handles{this};

    cad::Document* document() const { return document_; }
    tools::BaseTool* tool() const { return tool_; }
    const cad::MaterialSetup& material() const { return material_ ? *material_ : own_material_; }
    const std::set<std::string>& fit_warning_ids() const { return fit_warning_ids_; }

    // -- configuracao --
    void set_document(cad::Document* doc);
    void set_tool(tools::BaseTool* tool);
    void cancel_current_operation();
    void setup_material(double width, double height);
    void fit_view();
    void zoom_by(double factor);
    void zoom_by(double factor, const QPointF& anchor_view_pos);
    void zoom_selection();
    void set_grid_visible(bool visible);
    void reload_shortcuts();
    bool is_pan_active() const { return (tool_ && tool_->name() == "pan") || space_pressed_; }
    void pan_by(const QPointF& delta);
    void ensure_infinite_scene_rect();

    // -- coordenadas --
    QPointF world_to_scene(double x, double y) const { return {x, material_height - y}; }
    Pt scene_to_world(const QPointF& p) const { return {p.x(), material_height - p.y()}; }
    Pt to_world(const QPointF& view_pos) const { return scene_to_world(mapToScene(view_pos.toPoint())); }
    double pick_tolerance() const override;
    double px_per_mm() const override;

    // -- entidades --
    void refresh_entities() override;
    void preview_translation(const std::vector<cad::EntityPtr>& entities, double dx, double dy) override;
    void clear_translation_preview() override;
    void commit_translation_preview() override;
    void set_fit_warning_entities(const std::set<std::string>& entity_ids);
    void show_layout_preview(const std::vector<cad::Placement>& placements, const std::set<std::string>& unfit_ids);
    void clear_layout_preview();
    cad::EntityPtr entity_at(double wx, double wy) const;
    // Ponto da tela ao lado do vetor, onde a caixa de edicao abre.
    QPoint entity_anchor(const cad::Entity& entity) const;

    // -- snap --
    Pt apply_snap(double wx, double wy, std::optional<Qt::KeyboardModifiers> mods = std::nullopt,
                  const std::set<std::string>& exclude = {});
    void show_snap_marker(const std::optional<SnapResult>& result) override;
    void clear_snap_marker();

    // -- alcas e rotulo de angulo --
    void refresh_selection_handles(bool notify = true);
    void show_angle_label(double wx, double wy, double graus);
    void clear_angle_label();

    // -- percursos --
    void refresh_toolpaths(const std::vector<const cam::Toolpath*>& toolpaths);
    void set_manual_tab_markers(const std::vector<Pt>& points);
    void set_profile_tab_markers(const std::vector<TabPlacement>& placements, bool movable = true);

    // -- preview (rubber band) --
    void set_preview(const Points& pts, bool closed = false, bool nodes = false) override;
    void show_dimension_preview(const std::shared_ptr<cad::Dimension>& dimension) override;
    void clear_preview() override;
    void set_selection_preview(const Points& pts, bool crossing) override;
    Points construction_line_points(const QString& axis, double position) const;
    void show_construction_preview(const Points& points);
    void clear_construction_preview();
    void show_measurement(const Pt& start, const Pt& end, const std::string& text) override;
    void clear_measurement() override;

    // -- ICanvas --
    vp::SelectionHandles* selection_handles() override { return &handles; }
    const SnapEngine* snap_engine() const override { return &snap; }
    void set_cursor(const QCursor& cursor) override { setCursor(cursor); }

signals:
    void mouse_moved(double wx, double wy);
    void view_changed();
    // Emitido junto, para quem precisa reagir as alcas mudando de posicao.
    void selection_changed();
    void shortcut_requested(const QString& action);
    void cancel_requested();
    void tab_marker_moved(int index, double x, double y);
    void tab_marker_deleted(int index);
    void files_dropped(const QStringList& paths, double wx, double wy);
    void manual_tab_clicked(double wx, double wy);
    void manual_drill_clicked(double wx, double wy);
    void entity_double_clicked(const QString& entity_id);
    void context_menu_requested(const QPoint& global_pos, double wx, double wy);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::optional<BBox> selection_bbox_for(const std::vector<cad::EntityPtr>& selected) const;
    void rebuild_background();
    void add_margin_rect();
    void clear_fit_warning_items();
    void rebuild_fit_warning_items();
    void remove_item(QGraphicsItem* item);
    void clear_items(std::vector<QGraphicsItem*>& items);
    void retire_items(std::vector<QGraphicsItem*>& items);
    void clear_bulk_selection_proxy();
    void begin_navigation_preview();
    void finish_navigation_preview();
    void update_navigation_preview_transform();
    void drawItems(QPainter* painter, int numItems, QGraphicsItem* items[],
                   const QStyleOptionGraphicsItem options[]) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    std::optional<BBox> selection_bbox() const;
    void update_hover_cursor(double wx, double wy);
    std::optional<int> cursor_code_for(const std::optional<HandleKind>& kind) const;
    void apply_cursor_code(int code);
    std::set<std::string> dragging_ids() const;
    bool nudge(QKeyEvent* event);
    QStringList dropped_paths(const QMimeData* mime) const;
    static QPen tp_pen(const QColor& color, double width, bool dashed);

    cad::Document* document_ = nullptr;
    tools::BaseTool* tool_ = nullptr;
    cad::MaterialSetup own_material_;
    const cad::MaterialSetup* material_ = nullptr;
    QGraphicsScene* scene_;

    bool panning_ = false;
    bool space_pressed_ = false;
    QPointF last_pan_;
    QPointF pan_accumulator_{};
    // A ferramenta ja usou este clique direito (polilinha e estender terminam com ele): nao abrir
    // menu por cima.
    bool right_consumed_ = false;
    bool dragging_tab_marker_ = false;
    std::vector<QGraphicsItem*> bg_items_;
    std::vector<EntityItem*> ent_items_;
    std::unordered_map<const cad::Entity*, EntityItem*> ent_item_index_;
    std::vector<std::pair<EntityItem*, QPointF>> translation_preview_origins_;
    QGraphicsPixmapItem* translation_preview_pixmap_ = nullptr;
    QPointF translation_preview_pixmap_origin_;
    std::vector<EntityItem*> bulk_selection_proxy_items_;
    qreal bulk_selection_proxy_scale_ = 0.0;
    bool bulk_selection_proxy_persistent_ = false;
    bool preserve_bulk_selection_proxy_on_refresh_ = false;
    bool translation_preview_reusing_persistent_proxy_ = false;
    QPointF translation_preview_offset_;
    bool translation_preview_proxy_active_ = false;
    QTimer* navigation_preview_timer_ = nullptr;
    QImage navigation_preview_image_;
    QSize navigation_preview_viewport_size_;
    QTransform navigation_preview_transform_;
    QTransform navigation_preview_capture_viewport_transform_;
    bool navigation_preview_active_ = false;
    std::set<std::string> fit_warning_ids_;
    std::vector<QGraphicsItem*> fit_warning_items_;
    std::vector<QGraphicsItem*> layout_preview_items_;
    std::vector<QGraphicsItem*> tp_items_;
    std::vector<QGraphicsItem*> manual_tab_items_;
    std::vector<QGraphicsItem*> automatic_tab_items_;
    std::vector<QGraphicsItem*> node_items_;
    QGraphicsPathItem* preview_ = nullptr;
    EntityItem* dimension_preview_ = nullptr;
    QGraphicsPathItem* measurement_line_ = nullptr;
    QGraphicsPathItem* measurement_box_ = nullptr;
    QGraphicsPathItem* construction_preview_ = nullptr;
    QGraphicsSimpleTextItem* angle_label_ = nullptr;
    QGraphicsPathItem* snap_item_ = nullptr;
    QGraphicsSimpleTextItem* snap_text_ = nullptr;
    // Cursor da alca sob o mouse: -1 = rotacao, senao o Qt::CursorShape.
    std::optional<int> hover_cursor_;
};

// Reguas + Canvas 2D.
class Canvas2DWidget : public QWidget {
    Q_OBJECT
public:
    explicit Canvas2DWidget(double mat_w = 300.0, double mat_h = 200.0, QWidget* parent = nullptr);

    Canvas2D* canvas;
    RulerH* ruler_h;
    RulerV* ruler_v;
    QToolButton* fit_button;

    void set_material(double w, double h);
    void set_document(cad::Document* document);

signals:
    void mouse_moved(double wx, double wy);
    void selection_changed();
    void files_dropped(const QStringList& paths, double wx, double wy);
    void construction_line_requested(const QString& axis, double position);
    void entity_double_clicked(const QString& entity_id);
    void context_menu_requested(const QPoint& global_pos, double wx, double wy);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QToolButton* build_fit_button();
    QToolButton* build_zoom_button(const char* icon_name, const QString& tooltip, double factor);
    void position_button();
    void on_mouse(double wx, double wy);

    QToolButton* zoom_in_button = nullptr;
    QToolButton* zoom_out_button = nullptr;
};

}  // namespace cnc::vp
