// Montagem comum dos testes das ferramentas (o ToolTests do Python).
#pragma once

#include <doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "cad/document.hpp"
#include "tools/base.hpp"
#include "utils/commands.hpp"

namespace cnc::testing {

constexpr Qt::MouseButton LEFT = Qt::LeftButton;
constexpr Qt::KeyboardModifiers NONE = Qt::NoModifier;
constexpr Qt::KeyboardModifiers SHIFT = Qt::ShiftModifier;
constexpr Qt::KeyboardModifiers CTRL = Qt::ControlModifier;
constexpr Qt::KeyboardModifiers ALT = Qt::AltModifier;

// Canvas de mentira: so registra o que as ferramentas pedem.
struct FakeCanvas : tools::ICanvas {
    void refresh_entities() override {}
    void set_preview(const Points& pts, bool closed_, bool nodes_) override {
        preview = pts;
        closed = closed_;
        nodes = nodes_;
    }
    void set_selection_preview(const Points&, bool crossing_) override { crossing = crossing_; }
    void show_dimension_preview(const std::shared_ptr<cad::Dimension>& d) override { dimension = d; }
    void clear_preview() override {}
    void show_hud_input(const Pt& pos, const std::string& text, const std::string& hint = "") override {
        hud_pos = pos;
        hud_text = text;
        hud_hint = hint;
    }
    void clear_hud_input() override {
        hud_text.clear();
        hud_hint.clear();
    }
    double pick_tolerance() const override { return 0.01; }

    Points preview;
    bool closed = false;
    bool nodes = false;
    bool crossing = false;
    std::shared_ptr<cad::Dimension> dimension;
    Pt hud_pos{};
    std::string hud_text;
    std::string hud_hint;
};

struct ToolFixture {
    cad::Document doc;
    UndoStack stack;
    tools::ToolContext ctx{doc, stack};
    FakeCanvas canvas;
    std::vector<std::string> messages;

    ToolFixture() {
        ctx.canvas = &canvas;
        ctx.status_fn = [this](const std::string& m) { messages.push_back(m); };
    }

    void select(const std::vector<cad::EntityPtr>& entities) {
        doc.entities = entities;
        for (const cad::EntityPtr& e : entities) doc.set_selected(e, true);
    }
};

inline std::shared_ptr<cad::Rectangle> rect(double x, double y, double w, double h) {
    auto r = std::make_shared<cad::Rectangle>();
    r->x = x;
    r->y = y;
    r->width = w;
    r->height = h;
    return r;
}

inline std::shared_ptr<cad::Line> line(double x1, double y1, double x2, double y2) {
    auto l = std::make_shared<cad::Line>();
    l->x1 = x1;
    l->y1 = y1;
    l->x2 = x2;
    l->y2 = y2;
    return l;
}

inline std::shared_ptr<cad::Circle> circle(double cx, double cy, double radius) {
    auto c = std::make_shared<cad::Circle>();
    c->cx = cx;
    c->cy = cy;
    c->radius = radius;
    return c;
}

inline std::shared_ptr<cad::Polyline> poly(const Points& pts, bool closed) {
    auto p = std::make_shared<cad::Polyline>();
    p->points = pts;
    p->is_closed = closed;
    return p;
}

}  // namespace cnc::testing
