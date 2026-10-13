"""Cenários do rasgo de fixação (keyhole)."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Line, Polyline
from src.cam.keyhole import KeyholeSettings
from src.cam.model import ToolpathType
from src.cam.tools import Tool, ToolType


def _t():
    return Tool(name="TSlot", tool_type=ToolType.END_MILL, diameter=9.0, pass_depth=10.0,
                feed_rate=800, plunge_rate=300, tool_number=7)


def _case(name, entities, settings, **mat):
    doc = make_doc(entities, **mat)
    add_job(doc, ToolpathType.KEYHOLE, _t(), settings, entities)
    return run_case(name, doc)


def cases():
    S = KeyholeSettings
    return [
        _case("uma_linha", [Line(id="l", x1=10, y1=10, x2=10, y2=40)], S()),
        _case("duas_linhas", [Line(id="a", x1=0, y1=0, x2=30, y2=0), Line(id="b", x1=0, y1=20, x2=0, y2=50)],
              S(plunge_depth=8.0, slot_depth=2.0)),
        _case("zero_na_base", [Line(id="l", x1=10, y1=10, x2=10, y2=40)], S(), zero_z="bottom"),
        _case("erro_rasgo_mais_fundo_que_o_furo", [Line(id="l", x1=0, y1=0, x2=10, y2=0)],
              S(plunge_depth=3.0, slot_depth=5.0)),
        _case("polilinha_nao_vale", [Polyline(id="p", points=[(0, 0), (20, 0), (20, 20)], is_closed=False)], S()),
        _case("fechado_nao_vale", [Circle(id="c", cx=0, cy=0, radius=10)], S()),
        _case("linha_sem_comprimento", [Line(id="z", x1=3, y1=3, x2=3, y2=3)], S()),
    ]
