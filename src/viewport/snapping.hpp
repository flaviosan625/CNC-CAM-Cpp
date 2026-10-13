// Snap: achar o ponto exato que o usuario esta mirando (porte de src/viewport/snapping.py).
//
// Sem isto, desenhar "no canto da peca" e desenhar a 0,3 mm do canto, e encostar uma peca na outra
// deixa uma fresta que so aparece depois, no corte.
//
// Os tipos seguem a lista do Aspire (Editar > Opcoes de Snap, F4): no (extremidade de segmento, tem
// prioridade), centro, ponto medio, intersecao, sobre o vetor, grelha e cantos do material.
//
// A prioridade importa: perto de um canto existem varios candidatos ao mesmo tempo, e o no quase
// sempre e o que a pessoa quer. Desempate so por distancia faria o cursor pular entre o no e o meio
// do segmento. Tudo aqui e geometria pura - nada de widgets - para ser testado sem janela.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "cad/document.hpp"
#include "cad/entities.hpp"

namespace cnc::vp {

enum class SnapKind { Node, Center, Midpoint, Intersection, OnSpan, Grid, Material };

const char* snap_label(SnapKind k);
// Menor numero vence. No ganha de tudo; "sobre o vetor" e o ultimo recurso.
int snap_priority(SnapKind k);

struct SnapResult {
    double x = 0.0;
    double y = 0.0;
    SnapKind kind = SnapKind::Node;
    double distance = 0.0;
    cad::EntityPtr entity;

    const char* label() const { return snap_label(kind); }
    Pt point() const { return {x, y}; }
};

// Equivale ao formulario de Opcoes de Snap do Aspire.
struct SnapSettings {
    bool enabled = true;
    bool geometry = true;
    bool grid = false;
    bool material = true;

    bool nodes = true;
    bool centers = true;
    bool midpoints = true;
    bool intersections = true;
    bool on_span = true;

    // Distancia maxima do cursor ao ponto, em pixels de tela.
    double radius_px = 12.0;
    double grid_spacing = 10.0;
    bool show_label = true;

    Json to_json() const;
    static SnapSettings from_json(const Json& data);
};

using NotablePoint = std::pair<Pt, SnapKind>;

// Nos, centros e pontos medios de uma entidade.
std::vector<NotablePoint> entity_points(const cad::Entity& e, int segments = 64);
// Ponto mais proximo sobre o contorno e a distancia ate ele.
std::optional<std::pair<Pt, double>> closest_on_entity(const cad::Entity& e, double x, double y, int segments = 96);
// Cruzamentos entre dois vetores, por forca bruta nos segmentos.
std::vector<Pt> segment_intersections(const cad::Entity& a, const cad::Entity& b, int segments = 64);

struct MovingSnap {
    double dx;
    double dy;
    std::optional<SnapResult> hit;
};

// Procura o ponto notavel mais proximo do cursor.
class SnapEngine {
public:
    const cad::Document* document = nullptr;
    SnapSettings settings;

    std::vector<cad::EntityPtr> visible_entities(const std::set<std::string>& exclude = {}) const;
    // Melhor ponto para o cursor em (wx, wy), ou nada.
    std::optional<SnapResult> snap(double wx, double wy, double px_per_mm, const std::set<std::string>& exclude = {}) const;
    // Ajuste fino para encostar o que esta sendo movido no que esta parado. Devolve (dx, dy)
    // corrigidos e o ponto onde encostou. Com `anchor` - a posicao original do ponto por onde a peca
    // foi agarrada - esse ponto tem PREFERENCIA: se ele acha um alvo, e ele que encosta; se nao
    // acha nada, o encaixe normal continua valendo.
    MovingSnap snap_moving(const std::vector<cad::EntityPtr>& moving, double dx, double dy, double px_per_mm,
                           const std::optional<Pt>& anchor = std::nullopt) const;

private:
    bool wants(SnapKind k) const;
    std::vector<Pt> material_points() const;
};

}  // namespace cnc::vp
