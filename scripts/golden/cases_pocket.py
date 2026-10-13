"""Cenários do percurso de bolso."""

from __future__ import annotations

import copy

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Ellipse, Polyline, Rectangle, Text, make_star
from src.cam.model import CutDirection, FillType, MachineOrder, ProfilePass, RampType, ToolpathType
from src.cam.pocket import PocketSettings
from src.cam.presets import house_tool
from src.cam.tools import Tool

POCKET = ToolpathType.POCKET


def _rect(i="r1", x=0.0, y=0.0, w=100.0, h=60.0, **kw):
    return Rectangle(id=i, x=x, y=y, width=w, height=h, **kw)


def _case(name, entities, settings, tool=None, **material):
    doc = make_doc(entities, **material)
    add_job(doc, POCKET, tool or house_tool(6.0, 6.0), settings, entities)
    return run_case(name, doc)


def cases() -> list[dict]:
    out = []
    S = PocketSettings
    out.append(_case("offset_retangulo", [_rect()], S(cut_depth=6.0)))
    out.append(_case("offset_circulo", [Circle(id="c", cx=50, cy=50, radius=40)], S(cut_depth=5.0)))
    out.append(_case("offset_com_ilha", [_rect("p", 0, 0, 120, 90), Circle(id="i", cx=60, cy=45, radius=15)],
                     S(cut_depth=6.0)))
    out.append(_case("offset_varias_passadas", [_rect()], S(cut_depth=14.0)))
    out.append(_case("offset_passo_proprio", [_rect()], S(cut_depth=6.0, stepover=3.5)))
    out.append(_case("offset_sem_folga", [_rect()], S(cut_depth=6.0, allowance=0.0)))
    out.append(_case("offset_climb", [_rect()], S(cut_depth=6.0, direction=CutDirection.CLIMB)))
    out.append(_case("offset_estrela", [make_star(80, 80, 50, 6, 0.5, id="st")], S(cut_depth=4.0)))
    out.append(_case("offset_elipse", [Ellipse(id="e", cx=80, cy=50, rx=60, ry=35, rotation=15)], S(cut_depth=4.0)))

    for ang in (0.0, 30.0, 90.0, 135.0):
        out.append(_case(f"raster_{int(ang)}_graus", [_rect()], S(cut_depth=6.0, fill=FillType.RASTER, raster_angle=ang)))
    out.append(_case("raster_com_ilha", [_rect("p", 0, 0, 120, 90), Circle(id="i", cx=60, cy=45, radius=15)],
                     S(cut_depth=6.0, fill=FillType.RASTER, raster_angle=20.0)))
    out.append(_case("raster_circulo", [Circle(id="c", cx=50, cy=50, radius=40)],
                     S(cut_depth=5.0, fill=FillType.RASTER, raster_angle=45.0, stepover=4.0)))

    for p in ProfilePass:
        out.append(_case(f"passada_de_perfil_{p.value}", [_rect()], S(cut_depth=6.0, profile_pass=p)))
    for r in (RampType.NONE, RampType.SMOOTH):
        out.append(_case(f"rampa_{r.value}", [_rect()], S(cut_depth=8.0, ramp=r, ramp_distance=12.0)))

    out.append(_case("fresa_grande_demais", [_rect(w=8, h=8)], S(cut_depth=4.0)))
    big = Tool(name="Fresa 10mm", diameter=10.0, stepover=7.0, pass_depth=3.0, feed_rate=1000, plunge_rate=300)
    out.append(_case("aviso_de_cantos_passo_alto", [_rect()], S(cut_depth=4.0), tool=big))
    pecas = [_rect(f"p{i}", x, y, 40, 30) for i, (x, y) in enumerate([(120, 0), (0, 120), (60, 60), (0, 0)])]
    for ordem in (MachineOrder.SELECTION, MachineOrder.LEFT_RIGHT, MachineOrder.SHORTEST):
        out.append(_case(f"varios_bolsos_{ordem.value}", copy.deepcopy(pecas), S(cut_depth=4.0, order=ordem)))
    out.append(_case("profundidade_inicial", [_rect()], S(start_depth=2.0, cut_depth=6.0)))
    out.append(_case("passo_exato", [_rect()], S(cut_depth=10.0, maintain_exact_step=True)))
    out.append(_case("texto", [Text(id="t", x=0, y=0, content="OB", height=60.0)], S(cut_depth=3.0), tool=house_tool(2.0, 3.0)))
    out.append(_case("aberto_nao_vira_bolso", [Polyline(id="o", points=[(0, 0), (50, 0), (50, 50)], is_closed=False)],
                     S(cut_depth=3.0)))
    return out
