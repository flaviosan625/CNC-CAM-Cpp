"""Cenários do otimizador de disposição na chapa (cad/layout.py)."""

from __future__ import annotations

import dataclasses

from golden_lib import make_doc

from src.cad.entities import Circle, Rectangle, make_star
from src.cad.layout import LayoutOptions, plan_layout, plan_layout_advanced, plan_unfit_outside


def _pecas(n: int, base_w=60.0, base_h=40.0):
    ents = []
    for i in range(n):
        w = base_w + (i * 17) % 55
        h = base_h + (i * 29) % 70
        ents.append(Rectangle(id=f"p{i}", x=1000 + i * 3, y=2000 - i * 5, width=w, height=h))
    return ents


def _case(name, doc, options):
    ents = doc.entities
    placements, res = plan_layout_advanced(ents, doc.material, options)
    movs, res2 = plan_layout(ents, doc.material, 8.0)
    unfit = set(res.nao_couberam_ids)
    return {
        "name": name,
        "document": doc._to_dict(),
        "options": {**dataclasses.asdict(options), "area": list(options.area) if options.area else None},
        "advanced": {
            "placements": [{"ids": [e.id for e in p.entities], "anchor_x": p.anchor_x, "anchor_y": p.anchor_y,
                            "angle": p.angle, "mirrored": p.mirrored, "dx": p.dx, "dy": p.dy} for p in placements],
            "resultado": {"movidos": res.movidos, "nao_couberam": res.nao_couberam, "faixas": res.faixas,
                          "ids": list(res.nao_couberam_ids)},
        },
        "unfit_outside": {k: list(v) for k, v in plan_unfit_outside(ents, unfit, doc.material, 8.0).items()},
        "simple": {
            "movimentos": [[e.id, dx, dy] for e, dx, dy in movs],
            "resultado": {"movidos": res2.movidos, "nao_couberam": res2.nao_couberam, "faixas": res2.faixas},
        },
    }


def cases() -> list[dict]:
    out = []
    O = LayoutOptions
    chapa = dict(width=1220.0, height=2440.0)
    out.append(_case("dez_pecas_padrao", make_doc(_pecas(10), **chapa), O()))
    out.append(_case("sem_rotacao", make_doc(_pecas(10), **chapa), O(rotate=False)))
    out.append(_case("passo_de_90_graus", make_doc(_pecas(10), **chapa), O(rotation_step=90.0)))
    out.append(_case("passo_de_15_graus", make_doc(_pecas(6), **chapa), O(rotation_step=15.0)))
    out.append(_case("com_espelho", make_doc(_pecas(8), **chapa), O(mirror=True, rotation_step=90.0)))
    out.append(_case("origem_canto_superior_direito", make_doc(_pecas(8), **chapa), O(origin="top-right")))
    out.append(_case("origem_inferior_direito_direcao_y", make_doc(_pecas(8), **chapa), O(origin="bottom-right", direction="y")))
    out.append(_case("origem_superior_esquerdo", make_doc(_pecas(8), **chapa), O(origin="top-left", rotate=False)))
    out.append(_case("margem_e_folga_maiores", make_doc(_pecas(12), **chapa), O(margin=25.0, clearance=14.0)))
    out.append(_case("fresa_maior_que_a_folga", make_doc(_pecas(8), **chapa), O(tool_diameter=12.0, clearance=4.0)))
    out.append(_case("area_definida", make_doc(_pecas(10), **chapa), O(area=(100.0, 100.0, 700.0, 900.0))))
    out.append(_case("nao_cabem_todas", make_doc(_pecas(25, 150.0, 200.0), width=600.0, height=500.0), O()))
    out.append(_case("margem_da_chapa", make_doc(_pecas(10), margin=20.0, **chapa), O()))

    mistas = _pecas(4) + [Circle(id="c1", cx=3000, cy=3000, radius=45), Circle(id="c2", cx=3100, cy=3200, radius=30),
                          make_star(3300, 3000, 50, 5, 0.4, id="s1")]
    out.append(_case("formas_mistas", make_doc(mistas, **chapa), O()))

    agrupadas = _pecas(5)
    agrupadas[0].group = "g1"
    agrupadas[1].group = "g1"
    agrupadas[3].group = "g2"
    agrupadas[4].group = "g2"
    out.append(_case("pecas_agrupadas", make_doc(agrupadas, **chapa), O()))
    out.append(_case("sem_pecas", make_doc([], **chapa), O()))
    return out
