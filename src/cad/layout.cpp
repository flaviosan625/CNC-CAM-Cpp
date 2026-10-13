// Porte de src/cad/layout.py.
#include "cad/layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

#include "cad/selection.hpp"

namespace cnc::cad {

namespace {

using Block = std::vector<EntityPtr>;
using Rect = std::array<double, 4>;  // x, y, largura, altura

// Agrupa por grupo: peca agrupada anda inteira ou nao anda.
std::vector<Block> blocos(const std::vector<EntityPtr>& entidades) {
    std::vector<Block> soltos;
    std::vector<std::string> ordem;
    std::map<std::string, Block> grupos;
    for (const EntityPtr& e : entidades) {
        if (e->group && !e->group->empty()) {
            if (!grupos.count(*e->group)) ordem.push_back(*e->group);
            grupos[*e->group].push_back(e);
        } else {
            soltos.push_back({e});
        }
    }
    std::vector<Block> out = soltos;
    for (const std::string& g : ordem) out.push_back(grupos[g]);
    return out;
}

BBox bounds_of(const Block& block) {
    BBox out = block[0]->bounding_box();
    for (size_t i = 1; i < block.size(); ++i) {
        const BBox b = block[i]->bounding_box();
        out.x0 = std::min(out.x0, b.x0);
        out.y0 = std::min(out.y0, b.y0);
        out.x1 = std::max(out.x1, b.x1);
        out.y1 = std::max(out.y1, b.y1);
    }
    return out;
}

struct Variant {
    bool mirrored;
    double angle;
    BBox bounds;
};

struct Orientations {
    double anchor_x, anchor_y;
    std::vector<Variant> variants;
};

Orientations orientations(const Block& block, const LayoutOptions& options) {
    const BBox b = bounds_of(block);
    const double anchor_x = (b.x0 + b.x1) / 2.0, anchor_y = (b.y0 + b.y1) / 2.0;
    const double step = std::min(360.0, std::max(1.0, options.rotation_step));
    std::vector<double> angles;
    if (!options.rotate) {
        angles = {0.0};
    } else {
        const int n = static_cast<int>(std::ceil(360.0 / step));
        for (int i = 0; i < n; ++i) {
            if (i * step < 360.0) angles.push_back(py_round(i * step, 8));
        }
    }
    Orientations out{anchor_x, anchor_y, {}};
    for (bool mirrored : options.mirror ? std::vector<bool>{false, true} : std::vector<bool>{false}) {
        for (double angle : angles) {
            Block copies;
            for (const EntityPtr& e : block) copies.push_back(e->clone());
            for (const EntityPtr& e : copies) {
                if (mirrored) e->mirror_h(anchor_x);
                if (angle != 0.0) e->rotate(angle, anchor_x, anchor_y);
            }
            out.variants.push_back({mirrored, angle, bounds_of(copies)});
        }
    }
    return out;
}

// Remove um retangulo ocupado dos retangulos livres maximos.
std::vector<Rect> split_free_rectangles(const std::vector<Rect>& free_rectangles, const Rect& used) {
    const double ux = used[0], uy = used[1], uw = used[2], uh = used[3];
    const double ux1 = ux + uw, uy1 = uy + uh;
    std::vector<Rect> split;

    for (const Rect& f : free_rectangles) {
        const double fx = f[0], fy = f[1], fw = f[2], fh = f[3];
        const double fx1 = fx + fw, fy1 = fy + fh;
        if (ux >= fx1 || ux1 <= fx || uy >= fy1 || uy1 <= fy) {
            split.push_back(f);
            continue;
        }
        if (ux > fx) split.push_back({fx, fy, ux - fx, fh});
        if (ux1 < fx1) split.push_back({ux1, fy, fx1 - ux1, fh});
        if (uy > fy) split.push_back({fx, fy, fw, uy - fy});
        if (uy1 < fy1) split.push_back({fx, uy1, fw, fy1 - uy1});
    }

    std::vector<Rect> kept;
    for (const Rect& r : split) {
        if (r[2] > 1e-9 && r[3] > 1e-9) kept.push_back(r);
    }
    std::vector<Rect> maximal;
    for (size_t index = 0; index < kept.size(); ++index) {
        const Rect& r = kept[index];
        bool contained = false;
        for (size_t other_index = 0; other_index < kept.size(); ++other_index) {
            if (other_index == index) continue;
            const Rect& o = kept[other_index];
            if (r[0] >= o[0] - 1e-9 && r[1] >= o[1] - 1e-9 && r[0] + r[2] <= o[0] + o[2] + 1e-9 &&
                r[1] + r[3] <= o[1] + o[3] + 1e-9) {
                contained = true;
                break;
            }
        }
        if (!contained) maximal.push_back(r);
    }
    return maximal;
}

struct Prepared {
    Block block;
    double anchor_x, anchor_y;
    std::vector<Variant> variants;
    double area, width, height, long_side;
};

struct Attempt {
    std::vector<Placement> placements;
    std::vector<std::string> unfit_ids;
    std::vector<Rect> occupied;
    std::array<double, 2> score;
};

}  // namespace

void Placement::apply(Entity& entity) const {
    if (mirrored) entity.mirror_h(anchor_x);
    if (angle != 0.0) entity.rotate(angle, anchor_x, anchor_y);
    if (dx != 0.0 || dy != 0.0) entity.translate(dx, dy);
}

BBox area_util(const MaterialSetup& material) {
    if (auto r = material.margin_rect()) return *r;
    return {0.0, 0.0, material.width, material.height};
}

std::pair<std::vector<Placement>, Resultado> plan_layout_advanced(const std::vector<EntityPtr>& entities,
                                                                   const MaterialSetup& material,
                                                                   const LayoutOptions& options) {
    std::vector<EntityPtr> vectors;
    for (const EntityPtr& e : entities) {
        if (is_vector(*e)) vectors.push_back(e);
    }
    const std::vector<Block> blks = blocos(vectors);
    if (blks.empty()) return {{}, Resultado{}};

    const BBox area = options.area ? *options.area
                                   : BBox{options.margin, options.margin, material.width - options.margin,
                                          material.height - options.margin};
    const double x0 = area.x0, y0 = area.y0, x1 = area.x1, y1 = area.y1;
    const bool horizontal = options.direction == "x";
    const double primary_min = horizontal ? x0 : y0, primary_max = horizontal ? x1 : y1;
    const double secondary_min = horizontal ? y0 : x0, secondary_max = horizontal ? y1 : x1;
    auto ends_with = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const bool primary_at_min = horizontal ? ends_with(options.origin, "left") : options.origin.rfind("bottom", 0) == 0;
    const bool secondary_at_min = horizontal ? options.origin.rfind("bottom", 0) == 0 : ends_with(options.origin, "left");
    const double primary_sign = primary_at_min ? 1.0 : -1.0;
    const double secondary_sign = secondary_at_min ? 1.0 : -1.0;
    const double primary_size = primary_max - primary_min;
    const double secondary_size = secondary_max - secondary_min;
    const double gap = std::max(0.0, options.gap());

    auto to_xy = [&](double primary, double secondary) {
        return horizontal ? std::make_pair(primary, secondary) : std::make_pair(secondary, primary);
    };
    auto extents = [&](const BBox& b) {
        const double bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        return horizontal ? std::make_pair(bw, bh) : std::make_pair(bh, bw);
    };

    std::vector<Prepared> prepared;
    for (const Block& block : blks) {
        Orientations o = orientations(block, options);
        const BBox ob = bounds_of(block);
        Prepared p;
        p.block = block;
        p.anchor_x = o.anchor_x;
        p.anchor_y = o.anchor_y;
        p.variants = std::move(o.variants);
        p.area = (ob.x1 - ob.x0) * (ob.y1 - ob.y0);
        p.width = ob.x1 - ob.x0;
        p.height = ob.y1 - ob.y0;
        p.long_side = std::max(ob.x1 - ob.x0, ob.y1 - ob.y0);
        prepared.push_back(std::move(p));
    }

    auto sorted_by = [&](auto less) {
        std::vector<const Prepared*> v;
        for (const Prepared& p : prepared) v.push_back(&p);
        std::stable_sort(v.begin(), v.end(), less);
        return v;
    };
    const std::vector<std::vector<const Prepared*>> orders = {
        sorted_by([](const Prepared* a, const Prepared* b) { return -a->area < -b->area; }),
        sorted_by([](const Prepared* a, const Prepared* b) { return -a->long_side < -b->long_side; }),
        sorted_by([](const Prepared* a, const Prepared* b) {
            return std::make_pair(-a->height, -a->width) < std::make_pair(-b->height, -b->width);
        }),
        sorted_by([](const Prepared* a, const Prepared* b) {
            return std::make_pair(-a->width, -a->height) < std::make_pair(-b->width, -b->height);
        }),
    };

    auto pack = [&](const std::vector<const Prepared*>& ordered, const std::string& fitness) {
        std::vector<Rect> free = {{0.0, 0.0, primary_size + gap, secondary_size + gap}};
        Attempt at;

        for (const Prepared* item : ordered) {
            struct Best {
                std::array<double, 5> score;
                int rect_index;
                double rx, ry, p_extent, s_extent;
                Variant variant;
                double padded_primary, padded_secondary;
            };
            std::optional<Best> best;
            for (size_t variant_index = 0; variant_index < item->variants.size(); ++variant_index) {
                const Variant& variant = item->variants[variant_index];
                const auto [p_extent, s_extent] = extents(variant.bounds);
                const double padded_primary = p_extent + gap;
                const double padded_secondary = s_extent + gap;
                for (size_t rect_index = 0; rect_index < free.size(); ++rect_index) {
                    const double rx = free[rect_index][0], ry = free[rect_index][1];
                    const double rw = free[rect_index][2], rh = free[rect_index][3];
                    if (padded_primary > rw + 1e-9 || padded_secondary > rh + 1e-9) continue;
                    const double leftover_primary = rw - padded_primary;
                    const double leftover_secondary = rh - padded_secondary;
                    const double short_side = std::min(leftover_primary, leftover_secondary);
                    const double long_side = std::max(leftover_primary, leftover_secondary);
                    std::array<double, 5> score;
                    const double vi = static_cast<double>(variant_index);
                    if (fitness == "area") {
                        score = {rw * rh - padded_primary * padded_secondary, short_side, ry, rx, vi};
                    } else if (fitness == "bottom-left") {
                        score = {ry + s_extent, rx + p_extent, short_side, long_side, vi};
                    } else {
                        score = {short_side, long_side, ry, rx, vi};
                    }
                    if (!best || score < best->score) {
                        best = Best{score, static_cast<int>(rect_index), rx, ry, p_extent, s_extent, variant,
                                    padded_primary, padded_secondary};
                    }
                }
            }

            if (!best) {
                for (const EntityPtr& e : item->block) at.unfit_ids.push_back(e->id);
                continue;
            }

            const double primary_start = primary_sign > 0 ? primary_min + best->rx
                                                          : primary_max - best->rx - best->p_extent;
            const double secondary_start = secondary_sign > 0 ? secondary_min + best->ry
                                                              : secondary_max - best->ry - best->s_extent;
            const auto [target_x, target_y] = to_xy(primary_start, secondary_start);
            Placement pl;
            pl.entities = item->block;
            pl.anchor_x = item->anchor_x;
            pl.anchor_y = item->anchor_y;
            pl.angle = best->variant.angle;
            pl.mirrored = best->variant.mirrored;
            pl.dx = target_x - best->variant.bounds.x0;
            pl.dy = target_y - best->variant.bounds.y0;
            at.placements.push_back(std::move(pl));
            const Rect used = {best->rx, best->ry, best->padded_primary, best->padded_secondary};
            at.occupied.push_back(used);
            free = split_free_rectangles(free, used);
        }

        double packed_count = 0;
        for (const Placement& p : at.placements) packed_count += static_cast<double>(p.entities.size());
        double used_primary = 0.0, used_secondary = 0.0;
        for (const Rect& r : at.occupied) {
            used_primary = std::max(used_primary, r[0] + r[2]);
            used_secondary = std::max(used_secondary, r[1] + r[3]);
        }
        at.score = {packed_count, -used_primary * used_secondary};
        return at;
    };

    std::vector<Attempt> attempts;
    for (const auto& order : orders) {
        for (const char* fitness : {"short-side", "area", "bottom-left"}) attempts.push_back(pack(order, fitness));
    }
    size_t best_attempt = 0;
    for (size_t i = 1; i < attempts.size(); ++i) {
        if (attempts[i].score > attempts[best_attempt].score) best_attempt = i;  // max() devolve o primeiro empate
    }
    Attempt& chosen = attempts[best_attempt];

    std::set<double> row_ys;
    for (const Rect& r : chosen.occupied) row_ys.insert(py_round(r[1], 6));
    const int rows = chosen.occupied.empty() ? 0 : static_cast<int>(row_ys.size());

    int moved = 0;
    for (const Placement& p : chosen.placements) {
        if (p.mirrored || std::fabs(p.angle) > 1e-9 || std::fabs(p.dx) > 1e-9 || std::fabs(p.dy) > 1e-9) {
            moved += static_cast<int>(p.entities.size());
        }
    }
    Resultado res;
    res.movidos = moved;
    res.nao_couberam = static_cast<int>(chosen.unfit_ids.size());
    res.faixas = rows;
    res.nao_couberam_ids = chosen.unfit_ids;
    return {std::move(chosen.placements), std::move(res)};
}

std::map<std::string, std::pair<double, double>> plan_unfit_outside(const std::vector<EntityPtr>& entities,
                                                                    const std::set<std::string>& unfit_ids,
                                                                    const MaterialSetup& material, double gap) {
    std::vector<EntityPtr> vectors;
    for (const EntityPtr& e : entities) {
        if (is_vector(*e)) vectors.push_back(e);
    }
    std::vector<Block> picked;
    for (const Block& block : blocos(vectors)) {
        const bool any = std::any_of(block.begin(), block.end(), [&](const EntityPtr& e) { return unfit_ids.count(e->id) > 0; });
        if (any) picked.push_back(block);
    }
    if (picked.empty()) return {};

    const double spacing = std::max(0.0, gap);
    double column_x = material.width + spacing;
    double column_y = 0.0;
    double column_width = 0.0;
    std::map<std::string, std::pair<double, double>> offsets;

    for (const Block& block : picked) {
        const BBox b = bounds_of(block);
        const double width = b.x1 - b.x0, height = b.y1 - b.y0;
        if (column_y > 0 && column_y + height > material.height) {
            column_x += column_width + spacing;
            column_y = 0.0;
            column_width = 0.0;
        }
        const double dx = column_x - b.x0, dy = column_y - b.y0;
        for (const EntityPtr& e : block) offsets[e->id] = {dx, dy};
        column_y += height + spacing;
        column_width = std::max(column_width, width);
    }
    return offsets;
}

std::pair<std::vector<std::tuple<EntityPtr, double, double>>, Resultado> plan_layout(
    const std::vector<EntityPtr>& entidades, const MaterialSetup& material, double gap) {
    std::vector<EntityPtr> alvos;
    for (const EntityPtr& e : entidades) {
        if (is_vector(*e)) alvos.push_back(e);
    }
    std::vector<Block> blks = blocos(alvos);
    if (blks.empty()) return {{}, Resultado{}};

    const BBox a = area_util(material);
    const double largura = a.x1 - a.x0, altura = a.y1 - a.y0;

    // Mais alta primeiro: faixas de altura parecida desperdicam menos.
    std::stable_sort(blks.begin(), blks.end(), [](const Block& p, const Block& q) {
        const BBox bp = bounds_of(p), bq = bounds_of(q);
        return -(bp.y1 - bp.y0) < -(bq.y1 - bq.y0);
    });

    std::vector<std::tuple<EntityPtr, double, double>> movimentos;
    double cursor_x = a.x0, cursor_y = a.y0, altura_faixa = 0.0;
    int faixas = 1, nao_couberam = 0, movidos = 0;

    for (const Block& bloco : blks) {
        const BBox b = bounds_of(bloco);
        const double w = b.x1 - b.x0, h = b.y1 - b.y0;
        if (w > largura || h > altura) {
            nao_couberam += static_cast<int>(bloco.size());
            continue;
        }
        if (cursor_x + w > a.x1 + 1e-9) {  // nao cabe nesta faixa
            cursor_x = a.x0;
            cursor_y += altura_faixa + gap;
            altura_faixa = 0.0;
            ++faixas;
        }
        if (cursor_y + h > a.y1 + 1e-9) {  // nao cabe na chapa
            nao_couberam += static_cast<int>(bloco.size());
            continue;
        }
        const double dx = cursor_x - b.x0, dy = cursor_y - b.y0;
        if (std::fabs(dx) > 1e-9 || std::fabs(dy) > 1e-9) movidos += static_cast<int>(bloco.size());
        for (const EntityPtr& e : bloco) movimentos.emplace_back(e, dx, dy);
        cursor_x += w + gap;
        altura_faixa = std::max(altura_faixa, h);
    }
    Resultado r;
    r.movidos = movidos;
    r.nao_couberam = nao_couberam;
    r.faixas = faixas;
    return {std::move(movimentos), std::move(r)};
}

}  // namespace cnc::cad
