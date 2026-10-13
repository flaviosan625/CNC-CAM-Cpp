"""Cenários do pós-processador G-code (e da folha de trabalho em texto)."""

from __future__ import annotations

import dataclasses
import re

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import Circle, DrillPoint, Rectangle
from src.cam.dragknife import DragknifeSettings
from src.cam.drill import DrillSettings
from src.cam.gcode import GCodeSettings, PostProcessor, generate_gcode, setup_sheet
from src.cam.job import calculated_toolpaths
from src.cam.model import ToolpathType
from src.cam.pocket import PocketSettings
from src.cam.presets import house_tool
from src.cam.profile import ProfileSettings
from src.cam.tools import Tool, ToolType


def _doc():
    """Documento com três ferramentas: troca de ferramenta, furação, bolso e perfil."""
    peca = Rectangle(id="peca", x=0, y=0, width=100, height=60)
    furo = Circle(id="furo", cx=50, cy=30, radius=10)
    pontos = [DrillPoint(id=f"d{i}", x=x, y=y) for i, (x, y) in enumerate([(10, 10), (90, 10), (90, 50)])]
    doc = make_doc([peca, furo, *pontos], thickness=15.0, home_x=5.0, home_y=7.5)
    broca = Tool(name="Broca 5", tool_type=ToolType.DRILL, diameter=5.0, pass_depth=4.0, feed_rate=300,
                 plunge_rate=200, spindle_speed=8000, tool_number=18)
    add_job(doc, ToolpathType.DRILL, broca, DrillSettings(cut_depth=16.0, dwell=True, dwell_time=0.25), pontos)
    add_job(doc, ToolpathType.POCKET, house_tool(4.0, 11.0), PocketSettings(cut_depth=5.0), [peca])
    add_job(doc, ToolpathType.PROFILE, house_tool(6.0, 6.0), ProfileSettings(cut_depth=16.0, name="Corte — peça (1ª)"),
            [peca, furo])
    return doc


def _settings_to_dict(s: GCodeSettings) -> dict:
    d = dataclasses.asdict(s)
    d["post"] = s.post.value
    return d


def _mask(text: str) -> str:
    """A data muda a cada execução: mascara a linha para comparar."""
    return re.sub(r"Data: \d{2}/\d{2}/\d{4} \d{2}:\d{2}", "Data: *", text)


def cases() -> list[dict]:
    variantes = {
        "padrao_grbl": GCodeSettings(),
        "mach3": GCodeSettings(post=PostProcessor.MACH3),
        "linuxcnc_com_numeros_de_linha": GCodeSettings(post=PostProcessor.LINUXCNC, line_numbers=True, line_increment=5),
        "duas_casas": GCodeSettings(decimals=2),
        "quatro_casas_sem_comentarios": GCodeSettings(decimals=4, comments=False),
        "sem_troca_de_ferramenta": GCodeSettings(tool_change=False),
        "com_refrigeracao_e_fim_proprio": GCodeSettings(coolant_on="M8", coolant_off="M9", program_end="M30",
                                                         spindle_on="M4", spindle_off="M5"),
        "sem_voltar_a_origem": GCodeSettings(home_at_end=False),
    }
    out = []
    for nome, st in variantes.items():
        doc = _doc()
        caso = run_case(f"gcode_{nome}", doc)
        tps = calculated_toolpaths(doc.toolpaths)
        caso["gcode"] = {
            "settings": _settings_to_dict(st),
            "job_name": "Peça — teste",
            "text": _mask(generate_gcode(tps, doc.material, st, "Peça — teste")),
            "sheet": _mask(setup_sheet(tps, doc.material, "Peça — teste")),
        }
        out.append(caso)
    return out
