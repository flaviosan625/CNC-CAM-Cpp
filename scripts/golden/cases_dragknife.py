"""Cenários da faca de arraste."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Polyline, Rectangle, make_star
from src.cam.dragknife import DragknifeSettings
from src.cam.model import ToolpathType
from src.cam.tools import Tool, ToolType


def _t():
    return Tool(name="Faca", tool_type=ToolType.END_MILL, diameter=1.0, pass_depth=2.0,
                feed_rate=1500, plunge_rate=600, spindle_speed=0, tool_number=9)


def _case(name, entities, settings, **mat):
    doc = make_doc(entities, **mat)
    add_job(doc, ToolpathType.DRAGKNIFE, _t(), settings, entities)
    return run_case(name, doc)


def cases():
    S = DragknifeSettings
    z = Polyline(id="z", points=[(0, 0), (40, 0), (40, 30), (80, 30), (80, 0)], is_closed=False)
    return [
        _case("polilinha_com_cantos", [z], S()),
        _case("retangulo_fechado", [Rectangle(id="r", x=0, y=0, width=50, height=30)], S()),
        _case("circulo_suave", [Circle(id="c", cx=40, cy=40, radius=25)], S()),
        _case("estrela_cantos_agudos", [make_star(50, 50, 30, 5, 0.4, id="s")], S(blade_offset=1.5)),
        _case("duas_passadas", [Rectangle(id="r", x=0, y=0, width=40, height=20)], S(passes=2)),
        _case("angulo_de_tolerancia_alto", [z], S(tolerance_angle=100.0)),
        _case("giro_mais_fundo", [Rectangle(id="r", x=0, y=0, width=40, height=20)],
              S(swivel_depth=1.2, cut_depth=2.5)),
        _case("offset_da_lamina_grande", [z], S(blade_offset=4.0)),
        _case("sem_nada", [], S()),
    ]
