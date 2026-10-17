// Ferramentas de criacao de vetores - desenho interativo no canvas (porte de src/tools/draw_tools.py).
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <QImage>

#include "cad/entities.hpp"
#include "tools/base.hpp"

namespace cnc::tools {

// Tolerancia de clique em pixels.
inline constexpr double kPickPx = 8.0;

// ======================================================================
// Mover vista (Pan / Hand tool)
// ======================================================================

class PanTool : public BaseTool {
public:
    using BaseTool::BaseTool;

    std::string name() const override { return "pan"; }
    std::string label() const override { return "Mover vista"; }
    QCursor cursor() const override { return QCursor(Qt::OpenHandCursor); }
    bool cursor_snap() const override { return false; }
    std::string hint() const override {
        return "Clique e arraste com o botão esquerdo para mover a vista livremente";
    }
};

// ======================================================================
// Selecao
// ======================================================================

class SelectTool : public BaseTool {
public:
    using BaseTool::BaseTool;

    std::string name() const override { return "select"; }
    std::string label() const override { return "Seleção"; }
    QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }
    bool cursor_snap() const override { return false; }
    std::string hint() const override;
    void deactivate() override;

    // O que esta sendo arrastado, e so durante o arrasto. Parado, a peca selecionada precisa
    // continuar marcando os pontos dela - e olhando o "no" aceso no canto que se sabe por onde
    // pegar. Quem impede o clique de ser puxado para esse no e cursor_snap(), nao esta lista.
    std::vector<cad::EntityPtr> snap_targets() const override;
    std::string active_mode() const override { return mode_; }
    std::optional<vp::HandleKind> active_handle() const override;

    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_key(int key) override;

    // Vetor sob o ponto, de cima para baixo (exposto para os testes).
    cad::EntityPtr pick(double wx, double wy, double tol) const;

private:
    std::vector<cad::EntityPtr> members(const cad::EntityPtr& entity) const;
    vp::SelectionHandles* handles() const;
    bool press_move(double wx, double wy, Qt::KeyboardModifiers mods, const std::optional<vp::AnchorHit>& grab);
    bool press_handle(const vp::HandleHit& alca);
    std::pair<double, double> scale_factors(double wx, double wy, Qt::KeyboardModifiers mods) const;
    bool move_scale(double wx, double wy, Qt::KeyboardModifiers mods);
    bool move_rotate(double wx, double wy, Qt::KeyboardModifiers mods);
    std::pair<double, double> move_delta(double wx, double wy, Qt::KeyboardModifiers mods);

    std::string mode_;  // vazio = nenhum
    Pt start_{};
    Pt last_{};
    bool moved_ = false;
    std::vector<cad::EntityPtr> cmd_targets_;
    std::optional<vp::HandleHit> handle_;  // alca em uso, quando arrasta alca
    std::optional<Pt> grab_;               // ancora por onde a peca foi agarrada
    bool copying_ = false;                 // Ctrl no clique: solta uma copia
    Pt pivot_{};
    double a0_ = 0.0;   // angulo inicial para rotacao
    double acc_ = 0.0;  // giro acumulado (graus)
};

// ======================================================================
// Base para desenho por arraste (centro -> raio, canto -> canto)
// ======================================================================

// Clique-arraste-solta; as subclasses constroem a entidade.
class DragTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_key(int key) override;

    virtual Points ghost(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const;
    virtual bool ghost_closed() const { return true; }
    virtual cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const = 0;

    bool is_dragging() const { return p0_.has_value(); }
    const std::optional<Pt>& anchor() const { return p0_; }

protected:
    virtual bool handle_numeric_key(int key);
    virtual void update_numeric_preview() {}
    virtual bool commit_numeric_input() { return false; }
    virtual void clear_numeric_input();

    std::optional<Pt> p0_;
    Pt last_p1_{};
    Qt::KeyboardModifiers last_mods_{};
    std::string num_input_1_;
    std::string num_input_2_;
    int active_field_ = 0;
    bool numeric_active_ = false;
};

class CircleTool : public DragTool {
public:
    using DragTool::DragTool;
    std::string name() const override { return "circle"; }
    std::string label() const override { return "Círculo"; }
    std::string hint() const override {
        return "Clique no centro e arraste para definir o raio · Digite o valor para cota exata (D para diâmetro)";
    }
    cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const override;

protected:
    bool handle_numeric_key(int key) override;
    void update_numeric_preview() override;
    bool commit_numeric_input() override;
    void clear_numeric_input() override;

private:
    bool is_diameter_ = false;
};

class EllipseTool : public DragTool {
public:
    using DragTool::DragTool;
    std::string name() const override { return "ellipse"; }
    std::string label() const override { return "Elipse"; }
    std::string hint() const override { return "Arraste para definir a elipse · Ctrl = círculo · Alt = a partir do centro"; }
    cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const override;
};

class RectangleTool : public DragTool {
public:
    using DragTool::DragTool;
    double corner_radius = 0.0;
    std::string name() const override { return "rectangle"; }
    std::string label() const override { return "Retângulo"; }
    std::string hint() const override {
        return "Arraste de um canto ao outro · Digite largura, Tab, altura e Enter para cota exata";
    }
    cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const override;

protected:
    bool handle_numeric_key(int key) override;
    void update_numeric_preview() override;
    bool commit_numeric_input() override;
};

class PolygonTool : public DragTool {
public:
    using DragTool::DragTool;
    int sides = 6;
    std::string name() const override { return "polygon"; }
    std::string label() const override { return "Polígono"; }
    std::string hint() const override;
    cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const override;
};

class StarTool : public DragTool {
public:
    using DragTool::DragTool;
    int points = 5;
    double inner_ratio = 0.4;
    std::string name() const override { return "star"; }
    std::string label() const override { return "Estrela"; }
    std::string hint() const override;
    cad::EntityPtr build(const Pt& p0, const Pt& p1, Qt::KeyboardModifiers mods) const override;
};

// ======================================================================
// Linha / Polilinha
// ======================================================================

class PolylineTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "polyline"; }
    std::string label() const override { return "Polilinha"; }
    std::string hint() const override;
    virtual bool single_segment() const { return false; }

    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_double_click(double wx, double wy) override;
    bool on_key(int key) override;
    void deactivate() override;

    bool is_arc_mode() const { return arc_mode_; }

protected:
    virtual void finish_shape(bool close);

    Points pts_;
    bool numeric_active_ = false;
    std::string numeric_length_;

    void update_numeric_line_preview();
    void clear_numeric_input();

private:
    static Pt ortho(const Pt& a, const Pt& b);
    Pt cursor_{};
    bool arc_mode_ = false;
    Pt last_tangent_{1.0, 0.0};
    bool has_tangent_ = false;
    bool returned_to_vertex_ = false;
};

class LineTool : public PolylineTool {
public:
    using PolylineTool::PolylineTool;
    std::string name() const override { return "line"; }
    std::string label() const override { return "Linha"; }
    std::string hint() const override {
        return "Clique no início e depois no fim · Digite o comprimento e Enter para cota exata";
    }
    bool single_segment() const override { return true; }
};

class ConstructionLineTool : public PolylineTool {
public:
    using PolylineTool::PolylineTool;
    std::string name() const override { return "construction_line"; }
    std::string label() const override { return "Linha de construção"; }
    bool single_segment() const override { return true; }
    std::string hint() const override;

protected:
    void finish_shape(bool close) override;
};

class DrillPointTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "drill_point"; }
    std::string label() const override { return "Ponto de furação"; }
    std::string hint() const override { return "Clique para adicionar centros de furação · Esc finaliza"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
};

// ======================================================================
// Arco (3 pontos) e desenho livre
// ======================================================================

class ArcTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "arc"; }
    std::string label() const override { return "Arco"; }
    std::string hint() const override { return "Clique: início → fim → ponto de passagem"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    void deactivate() override;

    // Arco que passa pelos tres pontos (inicio, fim, passagem), ou nulo se forem colineares.
    static std::shared_ptr<cad::Arc> arc_through(const Pt& p1, const Pt& p2, const Pt& p3);

private:
    void build();
    Points pts_;
};

class FreehandTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    bool smooth = true;
    std::string name() const override { return "freehand"; }
    std::string label() const override { return "Desenho livre"; }
    std::string hint() const override { return "Pressione e arraste para desenhar à mão livre"; }
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    Points pts_;
    bool active_ = false;
};

// ======================================================================
// Texto
// ======================================================================

class TextTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string content = "Texto";
    double height = 20.0;
    std::string font_family = "Arial";
    bool bold = false;
    bool italic = false;
    std::string align = "left";

    std::string name() const override { return "text"; }
    std::string label() const override { return "Texto"; }
    std::string hint() const override { return "Clique no ponto de ancoragem do texto"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
};

class TextOnCurveTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string content = "Texto";
    double height = 20.0;
    std::string font_family = "Arial";

    std::string name() const override { return "text_on_curve"; }
    std::string label() const override { return "Texto em curva"; }
    std::string hint() const override { return "Selecione uma curva e clique para posicionar o texto nela"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

    // Ponto da trilha a distancia `s` do inicio, com o angulo da tangente.
    static std::tuple<double, double, double> at(const Points& path, const std::vector<double>& cum, double s);
};

// ======================================================================
// Cota e medicao
// ======================================================================

class DimensionTool : public BaseTool {
public:
    explicit DimensionTool(ToolContext& ctx) : BaseTool(ctx) {}
    double offset = 15.0;

    std::string name() const override { return "dimension"; }
    std::string label() const override { return "Cotas"; }
    std::string hint() const override;
    const std::string& mode() const { return mode_; }
    // Ignora valores desconhecidos; trocar de modo descarta o que estava em andamento.
    void set_mode(const std::string& value);

    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_key(int key) override;

private:
    static bool is_linear(const std::string& m);
    static bool is_radial(const std::string& m);
    void reset();
    cad::DimensionStyle style() const;
    std::shared_ptr<cad::Dimension> linear_dimension(const std::optional<Pt>& position = std::nullopt) const;
    std::shared_ptr<cad::Dimension> angular_dimension(const std::optional<Pt>& position = std::nullopt) const;
    std::shared_ptr<cad::Dimension> radial_dimension(const Pt& position) const;
    void report(const cad::Dimension& dimension, bool placed = false);
    cad::EntityPtr pick_source(double wx, double wy) const;
    void commit(const std::shared_ptr<cad::Dimension>& dimension);

    std::string mode_ = "linear";
    Points points_;
    cad::EntityPtr source_;  // Circle ou Arc
    std::optional<Pt> source_point_;
};

class MeasureTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "measure"; }
    std::string label() const override { return "Medir"; }
    std::string hint() const override { return "Clique em dois pontos para medir a distância"; }
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;

private:
    std::vector<std::string> measurement_lines(double wx, double wy) const;
    std::optional<Pt> p0_;
};

// ======================================================================
// Textura vetorial e vetorizacao de bitmap
// ======================================================================

class VectorTextureTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double spacing = 5.0;
    double angle = 45.0;
    std::string pattern = "hatch";

    std::string name() const override { return "vector_texture"; }
    std::string label() const override { return "Textura vetorial"; }
    void run() override;
    void apply_form() { run(); }
};

using Segment = std::pair<Pt, Pt>;
// Segmentos do contorno de uma grade binaria (marching squares), na ordem do Python.
std::vector<Segment> marching_squares(const std::vector<std::vector<bool>>& grid, int w, int h);
// Encadeia segmentos em lacos fechados.
std::vector<Points> chain_segments(const std::vector<Segment>& segs, double tol = 0.01);

class TraceBitmapTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "trace_bitmap"; }
    std::string label() const override { return "Vetorizar bitmap"; }
    bool instant() const override { return true; }
    void run() override;
    // Vetoriza uma imagem ja carregada e adiciona os contornos. Separado do run() para ser
    // testavel sem janela de arquivo.
    void trace_image(const QImage& image);
};

}  // namespace cnc::tools
