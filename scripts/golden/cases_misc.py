"""Funções utilitárias do CAM e do CAD: o que não é um percurso inteiro, mas a interface usa."""

from __future__ import annotations

import copy
import re

from golden_lib import add_job, make_doc

from src.cad.doc_vars import all_variables, expand, tokens_in
from src.cad.entities import Circle, DrillPoint, Rectangle, Text
from src.cam import presets as presets_mod
from src.cam.job import calculated_toolpaths
from src.cam.model import (
    CutDirection, MachineOrder, Move, MoveKind, Toolpath, ToolpathType, best_order, close_ring, compute_passes,
    is_ccw, order_loops, path_length, point_at, resample, rotate_to_start, signed_area, slice_path,
    tangent_at_start, travel_ccw, travel_length, _fmt_time,
)
from src.cam.presets import HOUSE_PRESETS, find as find_preset, for_thickness
from src.cam.profile import ProfileSettings
from src.cam.setup_sheet import setup_sheet_html
from src.cam.templates import ToolpathTemplate, TemplateLibrary
from src.cam.tools import Tool, ToolType, default_tools
from src.cam.vectric_tools import default_aspire_tool_db, import_aspire_tools, read_vectric_tools
from src.cam.tools import ToolDatabase


def _mask_date(text: str) -> str:
    return re.sub(r"\d{2}/\d{2}/\d{4} \d{2}:\d{2}", "DATA", text)


def _pts(points):
    return [[x, y] for x, y in points]


def cases() -> list[dict]:
    out: dict = {}

    # -- passadas em Z -------------------------------------------------------
    out["compute_passes"] = [
        {"args": [t, p, e, v], "out": compute_passes(t, p, e, v)}
        for t in (0.0, 1e-12, 3.0, 6.0, 12.0, 18.0, 19.0, 25.5, 100.0)
        for p in (0.0, 1.0, 3.0, 6.0, 11.0)
        for e in (False, True)
        for v in (0.15, 0.0, 0.5)
    ]
    out["fmt_time"] = [{"minutes": m, "out": _fmt_time(m)}
                       for m in (0.0, 0.004, 0.5, 0.999, 1.0, 12.4, 59.99, 60.0, 65.0, 125.5, 600.0, 3600.7)]

    # -- polilinhas ------------------------------------------------------------
    sq = [(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]
    cw = list(reversed(sq))
    path = [(0.0, 0.0), (10.0, 0.0), (10.0, 5.0), (3.0, 5.0)]
    out["polyline_utils"] = {
        "signed_area": [signed_area(sq), signed_area(cw)],
        "is_ccw": [is_ccw(sq), is_ccw(cw)],
        "close_ring": _pts(close_ring(list(sq))),
        "path_length": path_length(path),
        "rotate_to_start": [_pts(rotate_to_start(close_ring(list(sq)), s)) for s in ((9, 9), (0.1, 9.5), (5, -2))],
        "resample": _pts(resample(path, [1.0, 12.0, 17.5, 40.0, 0.0])),
        "point_at": [list(point_at(path, d)) for d in (0.0, 4.0, 10.0, 16.0, 99.0)],
        "slice_path": [_pts(slice_path(path, a, b)) for a, b in ((0.0, 6.0), (5.0, 15.0), (12.0, 22.0), (0.0, 22.0))],
        "tangent_at_start": [list(tangent_at_start(path)), list(tangent_at_start([(1, 1), (1, 1), (1, 3)]))],
        "travel_ccw": [[mi, d.value, travel_ccw(mi, d)] for mi in (True, False) for d in CutDirection],
    }
    loops = [[(x, y), (x + 5, y), (x + 5, y + 5)] for x, y in
             ((100, 10), (10, 100), (50, 50), (10, 10), (110, 110), (60, 5), (50, 50.0))]
    out["order_loops"] = [
        {"order": o.value, "band": b, "out": order_loops(loops, o, b)}
        for o in MachineOrder for b in (50.0, 20.0)
    ]
    out["best_order"] = [
        {"orders": [o.value for o in os_], "out": list(best_order(loops, os_)[0]), "winner": best_order(loops, os_)[1].value}
        for os_ in ([MachineOrder.LEFT_RIGHT, MachineOrder.BOTTOM_TOP], list(MachineOrder), [MachineOrder.SHORTEST])
    ]
    out["travel_length"] = [travel_length(loops, list(range(len(loops)))), travel_length(loops, [3, 0, 2])]

    # -- toolpath: estatisticas -------------------------------------------------
    tp = Toolpath(name="t", kind=ToolpathType.PROFILE, feed_rate=1000, plunge_rate=300, moves=[
        Move(MoveKind.RAPID, 0, 0, 5), Move(MoveKind.PLUNGE, 0, 0, -3), Move(MoveKind.CUT, 50, 0, -3),
        Move(MoveKind.DWELL, 50, 0, -3, 0.5), Move(MoveKind.CUT, 50, 40, -3), Move(MoveKind.RAPID, 50, 40, 5)])
    out["toolpath_stats"] = {
        "bounds": list(tp.bounds()), "depth_range": list(tp.depth_range()), "lengths": list(tp.lengths()),
        "seconds": tp.estimated_seconds(), "seconds_fast": tp.estimated_seconds(20000.0), "summary": tp.summary(),
    }

    # -- ferramentas ------------------------------------------------------------
    tools = default_tools()
    out["default_tools"] = [t.to_dict() for t in tools]
    out["tool_methods"] = [{
        "tool": t.to_dict(), "radius": t.radius, "stepover_percent": t.stepover_percent, "chip_load": t.chip_load,
        "needs_tails": t.needs_tails, "describe": t.describe(), "max_v_depth": (None if t.max_v_depth() == float("inf") else t.max_v_depth()),
        "flat_width": [t.flat_width_at(d) for d in (0.0, 0.5, 1.0, 3.0, 10.0)],
    } for t in tools]
    db = ToolDatabase()
    out["db_names"] = [t.name for t in db.tools]
    dup = Tool(name=db.tools[0].name)
    db.add(dup)
    out["db_add_duplicate_name"] = dup.name

    # -- presets da casa -----------------------------------------------------------
    out["house_presets"] = [{
        "name": p.name, "describe": p.describe(), "cut_depth": p.cut_depth, "passes": p.passes,
        "tool": p.tool().to_dict(), "note": p.note,
    } for p in HOUSE_PRESETS]
    out["find_preset"] = [{"q": [m, t], "found": (find_preset(m, t).name if find_preset(m, t) else None)}
                          for m, t in (("MDF", 15), ("mdf", 6.0), ("Acrílico", 4), ("acrilico", 8), ("PVC", 12), ("ps", 2), (" PVC ", 10))]
    out["for_thickness"] = [[t, [p.name for p in for_thickness(t)]] for t in (6, 10, 15, 18, 2, 4)]

    # -- modelos --------------------------------------------------------------------
    lib = TemplateLibrary(path=None)  # type: ignore[arg-type]
    presets_mod.seed(lib)
    out["house_templates"] = [t.to_dict() for t in lib.templates]
    out["template_methods"] = [{
        "label": t.label, "describe": t.describe(),
        "warning": [t.thickness_warning(make_doc([], thickness=th).material) for th in (15.0, 18.0, 12.0)],
    } for t in lib.templates]

    # -- variaveis do documento ------------------------------------------------------
    doc = make_doc([Rectangle(id="r", x=0, y=0, width=10, height=10)], thickness=15.0, width=1220.5, height=2440.0)
    doc.variables = {"CLIENTE": "Joana Ñ", "ESPESSURA": "99"}
    vals = all_variables(doc)
    textos = ["Peça |CLIENTE| - |ESPESSURA| mm", "|trabalho| e |LARGURA|x|ALTURA|", "|MATERIAL| |DESCONHECIDO| ||", "sem nada", "|cliente|"]
    out["doc_vars"] = {
        "values": {k: v for k, v in vals.items() if k not in ("DATA", "TRABALHO")},
        "expand": [expand(t, vals).replace(vals["DATA"], "DATA") for t in textos],
        "tokens": [tokens_in(t) for t in textos],
    }

    # -- Aspire (arquivo real desta maquina, se existir) --------------------------------
    path = default_aspire_tool_db()
    if path.exists():
        ferramentas, naosup, achados = read_vectric_tools(path)
        base = ToolDatabase()
        base_names = len(base.tools)
        rel = import_aspire_tools(base, path)
        out["vectric"] = {
            "found": achados, "unsupported": naosup, "tools": [t.to_dict() for t in ferramentas],
            "report": {"found": rel.found, "duplicates": rel.duplicates, "imported": [t.to_dict() for t in rel.imported],
                       "unsupported": rel.unsupported},
            "db_after": [t.to_dict() for t in base.tools],
        }

    # -- folha HTML ---------------------------------------------------------------------
    d2 = make_doc([Rectangle(id="peca", x=0, y=0, width=100, height=60), Circle(id="furo", cx=50, cy=30, radius=10)], thickness=15.0)
    add_job(d2, ToolpathType.PROFILE, Tool(name="Topo <6> & \"x\"", diameter=6.0, tool_number=3), ProfileSettings(cut_depth=16.0, name="Corte A&B"),
            d2.entities)
    add_job(d2, ToolpathType.PROFILE, Tool(name="Fresa 2", diameter=2.0, tool_number=2), ProfileSettings(cut_depth=4.0, name="Gravar"), [d2.entities[0]])
    d2.recalculate_toolpaths()
    out["setup_sheet_html"] = _mask_date(setup_sheet_html(calculated_toolpaths(d2.toolpaths), d2.material, "Peça <1> & cia"))
    out["setup_sheet_html_empty"] = _mask_date(setup_sheet_html([], d2.material, ""))
    return [{"name": "misc", "data": out}]
