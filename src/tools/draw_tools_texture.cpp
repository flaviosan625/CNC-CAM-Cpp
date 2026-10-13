// Porte de VectorTextureTool e TraceBitmapTool (src/tools/draw_tools.py).
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include <QFileDialog>
#include <QMessageBox>

#include "cad/geometry.hpp"
#include "tools/draw_tools.hpp"

namespace cnc::tools {

// ======================================================================
// Textura vetorial
// ======================================================================

void VectorTextureTool::run() {
    std::vector<cad::EntityPtr> sel;
    for (const cad::EntityPtr& e : ctx->selection()) {
        if (e->closed()) sel.push_back(e);
    }
    if (sel.empty()) {
        ctx->status("Selecione vetores fechados para preencher");
        return;
    }

    std::vector<cad::EntityPtr> made;
    for (const cad::EntityPtr& e : sel) {
        const geo::Geom poly = cad::to_shapely(*e);
        if (!poly || (poly.geom_type() != "Polygon" && poly.geom_type() != "MultiPolygon")) continue;
        const BBox box = e->bounding_box();
        const double diag = hypot(box.x1 - box.x0, box.y1 - box.y0);
        const double cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
        const std::vector<double> angles = pattern == "hatch" ? std::vector<double>{angle} : std::vector<double>{angle, angle + 90};
        for (double ang : angles) {
            const double a = radians(ang);
            const double ux = std::cos(a), uy = std::sin(a);
            const double px = -uy, py = ux;
            const int n = static_cast<int>(diag / std::max(spacing, 0.1)) + 2;
            for (int i = -n; i <= n; ++i) {
                const double ox = px * i * spacing, oy = py * i * spacing;
                const geo::Geom seg = geo::make_linestring({{cx + ox - ux * diag, cy + oy - uy * diag},
                                                            {cx + ox + ux * diag, cy + oy + uy * diag}});
                if (!seg) continue;
                const geo::Geom clipped = seg.intersection(poly);
                if (!clipped || clipped.is_empty()) continue;
                for (cad::EntityPtr& g : cad::from_shapely(clipped, e.get())) made.push_back(std::move(g));
            }
        }
    }

    if (!made.empty()) {
        const size_t count = made.size();
        ctx->add(made, "Textura vetorial");
        ctx->status(std::format("{} linha(s) de textura criada(s)", count));
    } else {
        ctx->status("Nenhuma textura gerada");
    }
}

// ======================================================================
// Vetorizar bitmap
// ======================================================================

void TraceBitmapTool::run() {
    const QString path = QFileDialog::getOpenFileName(nullptr, "Vetorizar imagem", "", "Imagens (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;

    const QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(nullptr, "Erro", "Não foi possível abrir a imagem.");
        return;
    }
    trace_image(img);
}

void TraceBitmapTool::trace_image(const QImage& source) {
    const cad::MaterialSetup& mat = ctx->document->material;
    const int max_px = 400;
    QImage img = source;
    if (std::max(img.width(), img.height()) > max_px) {
        img = img.scaled(max_px, max_px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    img = img.convertToFormat(QImage::Format_Grayscale8);

    const int w = img.width(), h = img.height();
    const double scale = std::min(mat.width / w, mat.height / h);
    // Fundo branco ao redor da imagem fecha contornos que tocam a borda.
    std::vector<std::vector<bool>> grid(static_cast<size_t>(h + 2), std::vector<bool>(static_cast<size_t>(w + 2), false));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            grid[static_cast<size_t>(y + 1)][static_cast<size_t>(x + 1)] = img.pixelColor(x, y).value() < 128;
        }
    }
    const std::vector<Segment> segs = marching_squares(grid, w + 2, h + 2);
    if (segs.empty()) {
        ctx->status("Nenhum contorno encontrado na imagem");
        return;
    }

    std::vector<cad::EntityPtr> ents;
    for (const Points& loop : chain_segments(segs)) {
        if (loop.size() < 3) continue;
        auto poly = std::make_shared<cad::Polyline>();
        for (size_t i = 0; i + 1 < loop.size(); ++i) {
            poly->points.push_back({(loop[i].x - 0.5) * scale, (h + 0.5 - loop[i].y) * scale});
        }
        poly->is_closed = true;
        ents.push_back(poly);
    }
    if (!ents.empty()) {
        const size_t count = ents.size();
        ctx->add(ents, "Vetorizar bitmap");
        ctx->status(std::format("{} contorno(s) vetorizado(s)", count));
    } else {
        ctx->status("Nenhum contorno utilizável");
    }
}

std::vector<Segment> marching_squares(const std::vector<std::vector<bool>>& grid, int w, int h) {
    std::vector<Segment> segs;
    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const auto g = [&](int yy, int xx) { return grid[static_cast<size_t>(yy)][static_cast<size_t>(xx)]; };
            const int code = (g(y, x) ? 1 : 0) | (g(y, x + 1) ? 2 : 0) | (g(y + 1, x + 1) ? 4 : 0) | (g(y + 1, x) ? 8 : 0);
            if (code == 0 || code == 15) continue;
            const Pt t{x + 0.5, static_cast<double>(y)};
            const Pt r{x + 1.0, y + 0.5};
            const Pt b{x + 0.5, y + 1.0};
            const Pt ll{static_cast<double>(x), y + 0.5};
            switch (code) {
                case 1: segs.push_back({ll, t}); break;
                case 2: segs.push_back({t, r}); break;
                case 3: segs.push_back({ll, r}); break;
                case 4: segs.push_back({r, b}); break;
                case 5: segs.push_back({ll, t}); segs.push_back({r, b}); break;
                case 6: segs.push_back({t, b}); break;
                case 7: segs.push_back({ll, b}); break;
                case 8: segs.push_back({b, ll}); break;
                case 9: segs.push_back({b, t}); break;
                case 10: segs.push_back({t, r}); segs.push_back({b, ll}); break;
                case 11: segs.push_back({b, r}); break;
                case 12: segs.push_back({r, ll}); break;
                case 13: segs.push_back({r, t}); break;
                case 14: segs.push_back({t, ll}); break;
                default: break;
            }
        }
    }
    return segs;
}

std::vector<Points> chain_segments(const std::vector<Segment>& segs, double tol) {
    using Key = std::pair<long long, long long>;
    auto key = [tol](const Pt& p) { return Key{py_round_i64(p.x / tol), py_round_i64(p.y / tol)}; };

    std::map<Key, std::vector<std::pair<size_t, Pt>>> adj;
    for (size_t index = 0; index < segs.size(); ++index) {
        adj[key(segs[index].first)].emplace_back(index, segs[index].second);
        adj[key(segs[index].second)].emplace_back(index, segs[index].first);
    }

    std::vector<Points> loops;
    std::set<size_t> used;
    for (size_t index = 0; index < segs.size(); ++index) {
        if (used.count(index)) continue;
        const Pt a = segs[index].first, b = segs[index].second;
        Points loop = {a, b};
        used.insert(index);
        Pt cur = b;
        for (size_t step = 0; step < segs.size(); ++step) {
            if (key(cur) == key(a)) break;
            const std::pair<size_t, Pt>* next = nullptr;
            for (const auto& edge : adj[key(cur)]) {
                if (!used.count(edge.first)) {
                    next = &edge;
                    break;
                }
            }
            if (!next) break;
            used.insert(next->first);
            cur = next->second;
            loop.push_back(cur);
        }
        if (loop.size() >= 4 && key(cur) == key(a)) loops.push_back(std::move(loop));
    }
    return loops;
}

}  // namespace cnc::tools
