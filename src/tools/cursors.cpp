// Porte de src/tools/cursors.py.
#include "tools/cursors.hpp"

#include <cmath>
#include <map>
#include <string>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>

#include "geo/types.hpp"

namespace cnc::tools::cursors {

namespace {

std::map<std::string, QCursor>& cache() {
    static std::map<std::string, QCursor> c;
    return c;
}

// Tesoura apontando para cima-esquerda; `spread` abre as laminas.
void draw_scissors(QPainter& painter, int size, double spread) {
    const double cx = size * 0.42;
    const double cy = size * 0.58;  // o pivo, onde as laminas se cruzam
    const double comprimento = size * 0.46;
    const double anel = size * 0.11;

    for (int lado : {-1, 1}) {
        const double ang = radians(-90 + lado * spread);
        const double px = cx + comprimento * std::cos(ang);
        const double py = cy + comprimento * std::sin(ang);
        painter.drawLine(QPointF(cx, cy), QPointF(px, py));

        // Cabo: desce do pivo para o lado oposto e termina no anel.
        const double ang_cabo = radians(90 + lado * (spread * 0.6 + 12));
        const double hx = cx + size * 0.30 * std::cos(ang_cabo);
        const double hy = cy + size * 0.30 * std::sin(ang_cabo);
        painter.drawLine(QPointF(cx, cy), QPointF(hx, hy));
        painter.drawEllipse(QPointF(hx, hy), anel, anel);
    }
    painter.drawEllipse(QPointF(cx, cy), size * 0.045, size * 0.045);
}

QCursor make_scissors(double spread, const QString& cor) {
    const int size = 28;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    // Contorno branco por baixo: a tesoura precisa aparecer tanto sobre o material claro quanto
    // sobre o fundo escuro do canvas.
    painter.setPen(QPen(QColor(255, 255, 255, 220), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    draw_scissors(painter, size, spread);

    painter.setPen(QPen(QColor(cor), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw_scissors(painter, size, spread);
    painter.end();

    // Ponto quente na ponta das laminas, que e onde o corte acontece.
    return QCursor(pix, static_cast<int>(size * 0.42), static_cast<int>(size * 0.12));
}

// Setas curvas opostas, como num icone de rotacao.
void draw_rotate(QPainter& painter, int size) {
    const double cx = size / 2.0, cy = size / 2.0;
    const double raio = size * 0.33;
    // Dois arcos opostos de ~160 graus, deixando aberturas no topo e na base.
    for (int inicio : {10, 190}) {
        painter.drawArc(static_cast<int>(cx - raio), static_cast<int>(cy - raio), static_cast<int>(raio * 2),
                        static_cast<int>(raio * 2), inicio * 16, 160 * 16);
        // Pontinha da seta no fim do arco
        const double a = radians(inicio + 160);
        const double px = cx + raio * std::cos(a);
        const double py = cy - raio * std::sin(a);  // Y para baixo no desenho
        const double tangente = radians(inicio + 160 + 90);
        const double tx = std::cos(tangente), ty = -std::sin(tangente);
        const double normal = radians(inicio + 160);
        const double nx = std::cos(normal), ny = -std::sin(normal);
        const double base_x = px - tx * size * 0.11;
        const double base_y = py - ty * size * 0.11;
        painter.drawLine(QPointF(px, py), QPointF(base_x + nx * size * 0.07, base_y + ny * size * 0.07));
        painter.drawLine(QPointF(px, py), QPointF(base_x - nx * size * 0.07, base_y - ny * size * 0.07));
    }
}

}  // namespace

QCursor scissors(bool is_open) {
    const std::string chave = is_open ? "open" : "closed";
    auto it = cache().find(chave);
    if (it == cache().end()) {
        it = cache().emplace(chave, is_open ? make_scissors(26.0, "#ff9f43") : make_scissors(7.0, "#1b2430")).first;
    }
    return it->second;
}

QCursor rotate() {
    auto it = cache().find("rotate");
    if (it != cache().end()) return it->second;
    const int size = 28;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    // Contorno branco para o icone ler sobre chapa e sobre canvas escuro.
    painter.setPen(QPen(QColor(255, 255, 255, 230), 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw_rotate(painter, size);
    painter.setPen(QPen(QColor("#2f4ad3"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw_rotate(painter, size);
    painter.end();
    return cache().emplace("rotate", QCursor(pix, size / 2, size / 2)).first->second;
}

void clear_cache() { cache().clear(); }

}  // namespace cnc::tools::cursors
