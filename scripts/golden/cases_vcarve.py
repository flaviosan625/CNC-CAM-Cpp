"""Cenários do Entalhe V."""

from __future__ import annotations

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, Ellipse, Polyline, Rectangle, Text, make_polygon, make_star
from src.cam.model import CutDirection, FillType, MachineOrder, RampType, ToolpathType
from src.cam.presets import house_tool
from src.cam.tools import Tool, ToolType
from src.cam.vcarve import VCarveMaterial, VCarveMode, VCarveSettings, automatic_settings

VCARVE = ToolpathType.VCARVE


def _v(angle=90.0, dia=12.0, name=None):
    return Tool(name=name or f"V {angle:g}", tool_type=ToolType.V_BIT, diameter=dia, v_angle=angle,
                pass_depth=2.0, stepover=1.0, feed_rate=1200, plunge_rate=300, spindle_speed=18000, tool_number=16)


def _grav(angle=60.0, dia=6.0, tip=0.2):
    return Tool(name="Gravacao", tool_type=ToolType.ENGRAVING, diameter=dia, v_angle=angle, tip_diameter=tip,
                pass_depth=1.0, feed_rate=1000, plunge_rate=300, spindle_speed=18000, tool_number=20)


def _ball(dia=6.0):
    return Tool(name="Esferica", tool_type=ToolType.BALL_NOSE, diameter=dia, pass_depth=2.0, stepover=0.8,
                feed_rate=1500, plunge_rate=400, spindle_speed=18000, tool_number=15)


def _case(name, entities, settings, tool, **material):
    doc = make_doc(entities, **material)
    add_job(doc, VCARVE, tool, settings, entities)
    return run_case(name, doc)


def cases() -> list[dict]:
    out = []
    S = VCarveSettings
    manual = dict(mode=VCarveMode.MANUAL)

    out.append(_case("retangulo_largo_automatico", [Rectangle(id="r", x=0, y=0, width=60, height=20)],
                     automatic_settings(_v(90), VCarveMaterial.MDF, 18.0), _v(90)))
    out.append(_case("retangulo_estreito", [Rectangle(id="r", x=0, y=0, width=80, height=4)],
                     S(**manual, max_depth=3.0), _v(90)))
    out.append(_case("circulo_ponto_unico", [Circle(id="c", cx=30, cy=30, radius=6)],
                     S(**manual, max_depth=4.0), _v(60)))
    out.append(_case("circulo_grande_limitado", [Circle(id="c", cx=50, cy=50, radius=30)],
                     S(**manual, max_depth=2.0, finish_stepover=0.5), _v(90)))
    out.append(_case("anel_com_ilha", [Circle(id="o", cx=40, cy=40, radius=25), Circle(id="i", cx=40, cy=40, radius=12)],
                     S(**manual, max_depth=3.0), _v(60)))
    out.append(_case("estrela", [make_star(50, 50, 30, 5, 0.4, id="s")], S(**manual, max_depth=3.0), _v(90)))
    out.append(_case("hexagono", [make_polygon(40, 40, 20, 6, id="h")], S(**manual, max_depth=3.0), _v(90)))
    out.append(_case("elipse", [Ellipse(id="e", cx=50, cy=30, rx=35, ry=12)], S(**manual, max_depth=3.0), _v(60)))
    out.append(_case("letra_A", [Text(id="t", x=0, y=0, content="A", height=40.0)],
                     S(**manual, max_depth=3.0, finish_stepover=0.3), _v(60)))
    out.append(_case("palavra_com_furos", [Text(id="t", x=0, y=0, content="OB", height=30.0)],
                     S(**manual, max_depth=2.5), _grav()))

    out.append(_case("fresa_de_gravacao", [Rectangle(id="r", x=0, y=0, width=40, height=10)],
                     S(**manual, max_depth=1.5), _grav(60.0, 6.0, 0.3)))
    out.append(_case("esferica", [Rectangle(id="r", x=0, y=0, width=40, height=10)],
                     S(**manual, max_depth=2.0), _ball()))
    out.append(_case("v_120_graus", [Rectangle(id="r", x=0, y=0, width=50, height=14)],
                     S(**manual, max_depth=3.0), _v(120)))

    out.append(_case("profundidade_plana", [Rectangle(id="r", x=0, y=0, width=60, height=30)],
                     S(**manual, flat_depth=2.0), _v(90)))
    out.append(_case("profundidade_inicial", [Rectangle(id="r", x=0, y=0, width=40, height=12)],
                     S(**manual, max_depth=3.0, start_depth=1.0), _v(90)))
    out.append(_case("passo_exato", [Rectangle(id="r", x=0, y=0, width=40, height=12)],
                     S(**manual, max_depth=4.0, maintain_exact_step=True), _v(90)))

    desb = house_tool(3.0, 1.5)
    out.append(_case("desbaste_com_fresa_de_topo", [Rectangle(id="r", x=0, y=0, width=80, height=40)],
                     S(**manual, max_depth=2.0, clearance_tools=[desb]), _v(90)))
    out.append(_case("desbaste_raster", [Rectangle(id="r", x=0, y=0, width=80, height=40)],
                     S(**manual, max_depth=2.0, clearance_tools=[desb], clearance_fill=FillType.RASTER, raster_angle=30.0), _v(90)))
    out.append(_case("desbaste_duas_fresas", [Rectangle(id="r", x=0, y=0, width=90, height=50)],
                     S(**manual, max_depth=2.5, clearance_tools=[house_tool(2.0, 2.0), house_tool(6.0, 3.0)]), _v(90)))
    out.append(_case("sem_regioes_planas", [Rectangle(id="r", x=0, y=0, width=80, height=40)],
                     S(**manual, max_depth=2.0, clearance_tools=[desb], skip_flat_regions=True), _v(90)))
    out.append(_case("rampa_nenhuma", [Rectangle(id="r", x=0, y=0, width=60, height=30)],
                     S(**manual, max_depth=2.0, ramp=RampType.NONE), _v(90)))
    out.append(_case("climb", [Rectangle(id="r", x=0, y=0, width=60, height=30)],
                     S(**manual, max_depth=2.0, direction=CutDirection.CLIMB), _v(90)))
    for mat in (VCarveMaterial.ACRYLIC, VCarveMaterial.HARDWOOD, VCarveMaterial.PLYWOOD):
        out.append(_case(f"material_{mat.value}", [Rectangle(id="r", x=0, y=0, width=50, height=16)],
                         automatic_settings(_v(60), mat, 18.0), _v(60)))

    out.append(_case("erro_fresa_de_topo", [Rectangle(id="r", x=0, y=0, width=50, height=16)], S(), house_tool(6.0, 6.0)))
    out.append(_case("erro_angulo_invalido", [Rectangle(id="r", x=0, y=0, width=50, height=16)], S(), _v(190)))
    out.append(_case("sem_vetor_fechado", [Polyline(id="p", points=[(0, 0), (30, 0), (30, 30)], is_closed=False)], S(), _v(90)))
    out.append(_case("sem_profundidade", [Rectangle(id="r", x=0, y=0, width=50, height=16)],
                     S(**manual, max_depth=3.0, start_depth=20.0), _v(90), thickness=18.0))
    return out
