"""Cenários do percurso de furação."""

from __future__ import annotations

import copy

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, DrillPoint, Ellipse, Rectangle
from src.cam.drill import DrillSettings, RetractMode
from src.cam.model import CutDirection, MachineOrder, ToolpathType
from src.cam.presets import house_tool
from src.cam.tools import Tool, ToolType

DRILL = ToolpathType.DRILL


def _case(name, entities, settings, tool=None, **material):
    doc = make_doc(entities, **material)
    add_job(doc, DRILL, tool or house_tool(6.0, 6.0), settings, entities)
    return run_case(name, doc)


def _broca(d=5.0, passo=4.0):
    return Tool(name=f"Broca {d:g}", tool_type=ToolType.DRILL, diameter=d, pass_depth=passo,
                feed_rate=300, plunge_rate=200, spindle_speed=8000, tool_number=18)


def cases() -> list[dict]:
    out = []
    S = DrillSettings
    pts = [DrillPoint(id=f"d{i}", x=x, y=y) for i, (x, y) in enumerate([(10, 10), (60, 10), (60, 50), (10, 50)])]
    out.append(_case("pontos_de_furacao", copy.deepcopy(pts), S(cut_depth=18.0), tool=_broca()))
    out.append(_case("peck_acima_do_inicio", copy.deepcopy(pts),
                     S(cut_depth=18.0, retract_mode=RetractMode.ABOVE_START), tool=_broca()))
    out.append(_case("peck_acima_da_anterior", copy.deepcopy(pts),
                     S(cut_depth=18.0, retract_mode=RetractMode.ABOVE_PREVIOUS, retract_gap=3.0), tool=_broca()))
    out.append(_case("sem_peck", copy.deepcopy(pts), S(cut_depth=12.0, peck=False), tool=_broca()))
    out.append(_case("com_pausa_no_fundo", copy.deepcopy(pts), S(cut_depth=10.0, dwell=True, dwell_time=0.5), tool=_broca()))
    out.append(_case("profundidade_inicial", copy.deepcopy(pts), S(start_depth=2.0, cut_depth=8.0), tool=_broca()))
    out.append(_case("circulos_como_furos", [Circle(id="a", cx=20, cy=20, radius=2.5), Circle(id="b", cx=60, cy=20, radius=2.5)],
                     S(cut_depth=12.0), tool=_broca(5.0)))
    out.append(_case("furo_helicoidal_10_com_fresa_6", [Circle(id="h", cx=50, cy=50, radius=5.0)],
                     S(cut_depth=10.0), tool=house_tool(6.0, 6.0)))
    out.append(_case("furo_helicoidal_grande_limpa_miolo", [Circle(id="h", cx=50, cy=50, radius=14.0)],
                     S(cut_depth=8.0), tool=house_tool(4.0, 4.0)))
    out.append(_case("helicoidal_horario", [Circle(id="h", cx=50, cy=50, radius=8.0)],
                     S(cut_depth=8.0, helical_direction=CutDirection.CLIMB), tool=house_tool(4.0, 4.0)))
    out.append(_case("helicoidal_desligado", [Circle(id="h", cx=50, cy=50, radius=8.0)],
                     S(cut_depth=8.0, helical=False), tool=house_tool(4.0, 4.0)))
    out.append(_case("furo_menor_que_a_ferramenta", [Circle(id="m", cx=20, cy=20, radius=1.0)],
                     S(cut_depth=5.0), tool=house_tool(6.0, 6.0)))
    out.append(_case("vetor_nao_redondo", [Rectangle(id="r", x=0, y=0, width=30, height=10)], S(cut_depth=5.0), tool=house_tool(2.0, 3.0)))
    out.append(_case("elipse_aviso", [Ellipse(id="e", cx=40, cy=40, rx=15, ry=8)], S(cut_depth=5.0), tool=house_tool(2.0, 3.0)))
    out.append(_case("sem_nada", [], S(cut_depth=5.0), tool=_broca()))
    for ordem in (MachineOrder.SELECTION, MachineOrder.LEFT_RIGHT, MachineOrder.BOTTOM_TOP, MachineOrder.SHORTEST):
        out.append(_case(f"ordem_{ordem.value}", copy.deepcopy(pts), S(cut_depth=6.0, order=ordem), tool=_broca()))
    out.append(_case("zero_na_base", copy.deepcopy(pts), S(cut_depth=6.0), tool=_broca(), zero_z="bottom"))
    return out
