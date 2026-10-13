// Ferramentas de transformacao - operam sobre a selecao (porte de src/tools/transform_tools.py).
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "tools/base.hpp"

namespace cnc::tools {

// Caixa envolvente da selecao (zeros se vazia).
BBox sel_bbox(const std::vector<cad::EntityPtr>& ents);
// Ponto de ancoragem da caixa ("bottom-left", "center", ...); desconhecido = centro.
Pt anchor_point(const std::vector<cad::EntityPtr>& ents, const std::string& anchor);

// ======================================================================
// Mover
// ======================================================================

class MoveTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double dx = 0.0;
    double dy = 0.0;
    bool absolute = false;
    std::string anchor = "center";
    bool make_copy = false;

    std::string name() const override { return "move"; }
    std::string label() const override { return "Mover"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override;
    // O que esta sendo arrastado: nao deve grudar em si mesmo.
    std::vector<cad::EntityPtr> snap_targets() const override { return drag_ ? targets_ : std::vector<cad::EntityPtr>{}; }
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void apply_form();

private:
    bool drag_ = false;
    Pt last_{};
    Pt start_{};
    std::vector<cad::EntityPtr> targets_;
};

// ======================================================================
// Rotacionar
// ======================================================================

class RotateTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double angle = 90.0;
    std::string anchor = "center";
    bool make_copy = false;

    std::string name() const override { return "rotate"; }
    std::string label() const override { return "Rotacionar"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Arraste para girar em torno do centro · Alt trava em 15°"; }
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void apply_form();

private:
    std::optional<Pt> pivot_;
    double a0_ = 0.0;
    double acc_ = 0.0;
    std::vector<cad::EntityPtr> targets_;
};

// ======================================================================
// Espelhar
// ======================================================================

class MirrorTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    bool make_copy = false;
    bool about_material = false;

    std::string name() const override { return "mirror"; }
    std::string label() const override { return "Espelhar"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Use os botões do painel · H = horizontal, V = vertical"; }
    // axis: "h" ou "v"
    void apply(const std::string& axis);
    bool on_key(int key) override;
};

// ======================================================================
// Redimensionar
// ======================================================================

class ScaleTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double width = 0.0;
    double height = 0.0;
    bool link_xy = true;
    std::string anchor = "center";
    bool use_percent = false;
    double percent = 100.0;
    bool make_copy = false;

    std::string name() const override { return "scale"; }
    std::string label() const override { return "Redimensionar"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Arraste para escalar proporcionalmente ou informe as dimensões no painel"; }
    void activate() override;
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void apply_form();

private:
    std::optional<Pt> pivot_;
    double d0_ = 1.0;
    double acc_ = 1.0;
    std::vector<cad::EntityPtr> targets_;
};

// ======================================================================
// Distorcer
// ======================================================================

class DistortTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "distort"; }
    std::string label() const override { return "Distorcer"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Arraste um dos 4 cantos da caixa para distorcer"; }
    void activate() override;
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    void show_box(const std::vector<cad::EntityPtr>& sel);

    std::optional<size_t> corner_;
    BBox box_{};
    std::vector<cad::EntityPtr> targets_;
    Points dst_;
};

// ======================================================================
// Copia em array e circular
// ======================================================================

class ArrayCopyTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    int cols = 3;
    int rows = 2;
    double gap_x = 10.0;
    double gap_y = 10.0;
    bool use_gap = true;

    std::string name() const override { return "array_copy"; }
    std::string label() const override { return "Cópia em array"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Defina linhas/colunas e espaçamento no painel"; }
    void apply_form();
};

class CircularCopyTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    int count = 6;
    double total_angle = 360.0;
    double step_angle = 60.0;
    bool use_total = true;
    bool rotate_copies = true;
    std::optional<double> center_x;
    std::optional<double> center_y;

    std::string name() const override { return "circular_copy"; }
    std::string label() const override { return "Cópia circular"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Clique para definir o centro · ajuste a quantidade no painel"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void apply_form();
};

}  // namespace cnc::tools
