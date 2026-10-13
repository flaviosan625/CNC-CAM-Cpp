"""Cenários de canais (fluting)."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Arc, Circle, Line, Polyline, Rectangle
from src.cam.fluting import FlutingSettings
from src.cam.model import ToolpathType
from src.cam.tools import Tool, ToolType


def _ball(d=6.0, passo=2.0):
    return Tool(name="Esferica", tool_type=ToolType.BALL_NOSE, diameter=d, pass_depth=passo,
                feed_rate=1500, plunge_rate=400, tool_number=15)


def _case(name, entities, settings, tool=None, **mat):
    doc = make_doc(entities, **mat)
    add_job(doc, ToolpathType.FLUTING, tool or _ball(), settings, entities)
    return run_case(name, doc)


def cases():
    S = FlutingSettings
    out = []
    out.append(_case("linha_reta", [Line(id="l", x1=0, y1=0, x2=100, y2=0)], S(cut_depth=3.0)))
    out.append(_case("polilinha", [Polyline(id="p", points=[(0, 0), (50, 20), (100, 0), (150, 30)], is_closed=False)],
                     S(cut_depth=4.0)))
    out.append(_case("circulo_fechado", [Circle(id="c", cx=50, cy=50, radius=30)], S(cut_depth=3.0)))
    out.append(_case("retangulo_fechado", [Rectangle(id="r", x=0, y=0, width=80, height=40)], S(cut_depth=3.0)))
    out.append(_case("arco", [Arc(id="a", cx=0, cy=0, radius=40, start_angle=0, end_angle=120)], S(cut_depth=2.0)))
    out.append(_case("afunila_profundidade_final", [Line(id="l", x1=0, y1=0, x2=120, y2=0)],
                     S(cut_depth=4.0, end_depth=1.0)))
    out.append(_case("varias_passadas", [Line(id="l", x1=0, y1=0, x2=100, y2=0)], S(cut_depth=6.0),
                     tool=_ball(6.0, 1.5)))
    out.append(_case("sem_rampas", [Line(id="l", x1=0, y1=0, x2=100, y2=0)],
                     S(cut_depth=3.0, ramp_in=0.0, ramp_out=0.0)))
    out.append(_case("rampas_longas", [Line(id="l", x1=0, y1=0, x2=100, y2=0)],
                     S(cut_depth=3.0, ramp_in=40.0, ramp_out=40.0)))
    out.append(_case("profundidade_inicial", [Line(id="l", x1=0, y1=0, x2=100, y2=0)],
                     S(start_depth=1.0, cut_depth=3.0)))
    out.append(_case("dois_canais", [Line(id="a", x1=0, y1=0, x2=60, y2=0), Line(id="b", x1=0, y1=20, x2=60, y2=20)],
                     S(cut_depth=2.0)))
    out.append(_case("comprimento_zero", [Line(id="z", x1=5, y1=5, x2=5, y2=5)], S(cut_depth=2.0)))
    return out
