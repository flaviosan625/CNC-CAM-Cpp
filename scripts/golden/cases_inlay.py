"""Cenários do inlay (bolso + plug)."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Rectangle, Text, make_star
from src.cam.inlay import InlaySettings
from src.cam.model import ToolpathType
from src.cam.presets import house_tool


def _case(name, entities, settings, tool=None, **mat):
    doc = make_doc(entities, **mat)
    add_job(doc, ToolpathType.INLAY, tool or house_tool(3.0, 1.5), settings, entities)
    return run_case(name, doc)


def cases():
    S = InlaySettings
    return [
        _case("retangulo", [Rectangle(id="r", x=0, y=0, width=60, height=40)], S()),
        _case("circulo", [Circle(id="c", cx=40, cy=40, radius=25)], S(pocket_depth=4.0, plug_depth=6.0)),
        _case("estrela", [make_star(50, 50, 30, 5, 0.45, id="s")], S(glue_allowance=0.2)),
        _case("sem_folga_de_cola", [Rectangle(id="r", x=0, y=0, width=60, height=40)], S(glue_allowance=0.0)),
        _case("profundidade_inicial", [Rectangle(id="r", x=0, y=0, width=60, height=40)], S(start_depth=1.0)),
        _case("texto", [Text(id="t", x=0, y=0, content="AB", height=40.0)], S(pocket_depth=2.0, plug_depth=3.0),
              tool=house_tool(2.0, 3.0)),
    ]
