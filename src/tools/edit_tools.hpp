// Ferramentas de edicao de vetores - booleanas, offset, nos, aparar (porte de src/tools/edit_tools.py).
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cad/entities.hpp"
#include "tools/base.hpp"

namespace cnc::tools {

// ======================================================================
// Edicao de nos
// ======================================================================

class NodeEditTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "node_edit"; }
    std::string label() const override { return "Edição de nós"; }
    QCursor cursor() const override { return QCursor(Qt::PointingHandCursor); }
    std::string hint() const override;

    void activate() override;
    void deactivate() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_double_click(double wx, double wy) override;
    bool on_key(int key) override;

    const cad::EntityPtr& target() const { return target_; }

private:
    void load(const cad::EntityPtr& e);
    bool grab_near(double wx, double wy, double tol);
    void show();
    void commit();

    cad::EntityPtr target_;
    Points pts_;
    Points clean_;
    std::optional<size_t> idx_;
    std::optional<size_t> selected_node_;
    Pt hover_{};
};

// ======================================================================
// Booleanas
// ======================================================================

class BoolTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    virtual size_t min_sel() const { return 2; }
    virtual std::vector<cad::EntityPtr> op(const std::vector<cad::EntityPtr>& sel) const = 0;
    void run() override;

private:
    std::string why_not(const std::vector<cad::EntityPtr>& raw, const std::vector<cad::EntityPtr>& closed) const;
};

class WeldTool : public BoolTool {
public:
    using BoolTool::BoolTool;
    std::string name() const override { return "weld"; }
    std::string label() const override { return "Soldar vetores"; }
    std::vector<cad::EntityPtr> op(const std::vector<cad::EntityPtr>& sel) const override;
};

class SubtractTool : public BoolTool {
public:
    using BoolTool::BoolTool;
    std::string name() const override { return "subtract"; }
    std::string label() const override { return "Subtrair vetores"; }
    std::vector<cad::EntityPtr> op(const std::vector<cad::EntityPtr>& sel) const override;
};

class IntersectTool : public BoolTool {
public:
    using BoolTool::BoolTool;
    std::string name() const override { return "intersect"; }
    std::string label() const override { return "Interseção de vetores"; }
    std::vector<cad::EntityPtr> op(const std::vector<cad::EntityPtr>& sel) const override;
};

// ======================================================================
// Offset, filete, unir
// ======================================================================

class OffsetTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double distance = 5.0;
    bool outward = true;
    bool keep_original = true;
    std::string join_style = "round";

    std::string name() const override { return "offset"; }
    std::string label() const override { return "Offset de vetores"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Defina a distância no painel e clique em Aplicar"; }
    void apply_form();
};

class FilletTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double radius = 5.0;

    std::string name() const override { return "fillet"; }
    std::string label() const override { return "Filete / Arredondamento"; }
    std::string hint() const override { return "Clique num canto de um vetor para arredondá-lo"; }
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
};

class JoinTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double tolerance = 0.5;

    std::string name() const override { return "join"; }
    std::string label() const override { return "Unir vetores abertos"; }
    bool needs_selection() const override { return true; }
    void run() override;
    void apply_form() { run(); }

    // Distancia entre as duas extremidades livres mais proximas, ou nada.
    static std::optional<double> smallest_gap(const std::vector<cad::EntityPtr>& entities);
};

// ======================================================================
// Aparar e estender
// ======================================================================

// Aparar objetos contra um contorno - o "Trim Objects" do Vectric. Nao e a tesoura (essa e o Aparar
// Interativo): recorta TUDO o que esta selecionado contra o ultimo vetor selecionado.
class TrimTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    bool clear_outside = true;

    std::string name() const override { return "trim"; }
    std::string label() const override { return "Aparar objetos"; }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    std::string hint() const override;
    virtual void apply_form();
    bool on_press(double, double, Qt::MouseButton, Qt::KeyboardModifiers) override { return false; }
    bool trim_at_click(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods);

private:
    bool confirm(size_t deleted, size_t total, const cad::Entity& boundary) const;
    static std::string summary(size_t touched, size_t deleted);
};

// A tesoura: clique no trecho e ele sai. O cursor fica fechado por padrao e ABERTO sobre um vetor
// aparavel - sem esse sinal nao ha como saber se o clique vai cortar ali.
class InteractiveTrimTool : public TrimTool {
public:
    using TrimTool::TrimTool;
    std::string name() const override { return "interactive_trim"; }
    std::string label() const override { return "Aparar interativo"; }
    std::string hint() const override;
    QCursor cursor() const override;

    void activate() override;
    void deactivate() override;
    // A tesoura nao tem botao de aplicar: sem este corte, um clique em Aplicar recortaria o desenho
    // inteiro contra o ultimo vetor selecionado.
    void apply_form() override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_release(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    cad::EntityPtr trimmable_at(double wx, double wy) const;
    void set_cursor_open(bool open);

    bool down_ = false;
    std::optional<bool> hover_;
    Points trail_;
};

class ExtendTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "extend"; }
    std::string label() const override { return "Estender vetores"; }
    std::string hint() const override;
    void deactivate() override;
    bool on_move(double wx, double wy, Qt::KeyboardModifiers mods) override;
    bool on_press(double wx, double wy, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

    const std::shared_ptr<cad::Line>& target() const { return target_; }

    struct ExtendCandidate {
        std::shared_ptr<cad::Line> line;
        std::string endpoint;
        Pt tip;
        Pt target_pt;
    };
    std::optional<ExtendCandidate> find_candidate(double wx, double wy) const;

private:
    std::shared_ptr<cad::Line> target_;
    std::string endpoint_;
};

// ======================================================================
// Contorno e ajuste de curvas
// ======================================================================

class BoundaryTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double offset = 0.0;
    bool rubber_band = false;

    std::string name() const override { return "boundary"; }
    std::string label() const override { return "Contorno vetorial"; }
    bool needs_selection() const override { return true; }
    void run() override;
    void apply_form() { run(); }
};

class FitCurvesTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    double tolerance = 0.1;
    bool keep_original = false;

    std::string name() const override { return "fit_curves"; }
    std::string label() const override { return "Ajustar curvas"; }
    bool needs_selection() const override { return true; }
    std::string hint() const override { return "Aproxima o vetor por segmentos de reta, respeitando a tolerância informada"; }
    void run() override;
    void apply_form() { run(); }
};

// ======================================================================
// Agrupar, converter texto, fechar, alinhar
// ======================================================================

class GroupTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "group"; }
    std::string label() const override { return "Agrupar"; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    void run() override;
};

class UngroupTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "ungroup"; }
    std::string label() const override { return "Desagrupar"; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    void run() override;
};

class ConvertTextTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "convert_text"; }
    std::string label() const override { return "Converter texto em curvas"; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    void run() override;
};

// Quebra um bloco de texto em um objeto por linha, com entrelinha de 1,25 da altura da letra.
class SplitTextLinesTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    static constexpr double kLineSpacing = 1.25;
    std::string name() const override { return "text_lines"; }
    std::string label() const override { return "Separar texto em linhas"; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    void run() override;
};

// Fecha polilinhas abertas ligando o ultimo no ao primeiro.
class CloseVectorsTool : public BaseTool {
public:
    using BaseTool::BaseTool;
    std::string name() const override { return "close_vectors"; }
    std::string label() const override { return "Fechar vetores"; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    void run() override;
};

// Uma instancia por modo de alinhamento.
class AlignTool : public BaseTool {
public:
    AlignTool(ToolContext& ctx, const std::string& mode = "left");
    std::string name() const override { return name_; }
    std::string label() const override { return label_; }
    bool instant() const override { return true; }
    bool needs_selection() const override { return true; }
    const std::string& mode() const { return mode_; }
    void run() override;

private:
    void align_to_material(const std::vector<cad::EntityPtr>& sel);
    void apply(const std::vector<cad::EntityPtr>& sel, double gx0, double gy0, double gx1, double gy1);

    std::string mode_;
    std::string name_;
    std::string label_;
};

class AlignCommand : public Command {
public:
    AlignCommand(cad::Document& doc, std::vector<std::pair<cad::EntityPtr, Pt>> pairs, std::string label)
        : doc_(doc), pairs_(std::move(pairs)), label_(std::move(label)) {}
    void execute() override;
    void undo() override;
    std::string description() const override { return label_; }

private:
    cad::Document& doc_;
    std::vector<std::pair<cad::EntityPtr, Pt>> pairs_;
    std::string label_;
};

}  // namespace cnc::tools
