"""Cenários da textura (aleatória, com semente)."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Rectangle, Text
from src.cam.model import ToolpathType
from src.cam.texture import TextureSettings
from src.cam.tools import Tool, ToolType


def _t(r=3.0):
    return Tool(name="Esferica", tool_type=ToolType.BALL_NOSE, diameter=2 * r, pass_depth=2.0,
                feed_rate=1500, plunge_rate=400, tool_number=15)


def _case(name, entities, settings, tool=None, **mat):
    doc = make_doc(entities, **mat)
    add_job(doc, ToolpathType.TEXTURE, tool or _t(), settings, entities)
    return run_case(name, doc)


def cases():
    S = TextureSettings

    def retangulo():
        return Rectangle(id="r", x=0, y=0, width=120, height=80)

    return [
        _case("padrao_semente_42", [retangulo()], S()),
        _case("semente_diferente", [retangulo()], S(seed=7)),
        _case("semente_negativa", [retangulo()], S(seed=-123)),
        _case("semente_grande", [retangulo()], S(seed=2 ** 40 + 5)),
        _case("angulo_30_com_variacao", [retangulo()], S(angle=30.0, angle_var=40.0)),
        _case("sem_variacao", [retangulo()], S(length_var=0, depth_var=0, stepover_var=0, overlap_var=0)),
        _case("sobreposicao_total", [retangulo()], S(overlap=1.0, overlap_var=0)),
        _case("circulo", [Circle(id="c", cx=60, cy=60, radius=45)], S(length=15.0, stepover=4.0)),
        _case("com_furo", [Rectangle(id="p", x=0, y=0, width=100, height=70), Circle(id="f", cx=50, cy=35, radius=15)],
              S(seed=5)),
        _case("ferramenta_grande_nao_cabe", [Rectangle(id="r", x=0, y=0, width=8, height=8)], S(), tool=_t(6.0)),
        _case("texto", [Text(id="t", x=0, y=0, content="AO", height=60.0)], S(length=8.0, stepover=2.0), tool=_t(1.0)),
    ]
