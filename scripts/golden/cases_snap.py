"""Cenários do snap (viewport/snapping.py): pontos notáveis, interseções, ímã ao mover."""

from __future__ import annotations

from golden_lib import make_doc

from src.cad.entities import (
    Arc, Circle, Ellipse, Line, Polyline, Rectangle, Text, make_star,
)
from src.viewport.snapping import (
    SnapEngine, SnapSettings, closest_on_entity, entity_points, segment_intersections,
)


def _pt(x, y):
    return [x, y]


def _res(r):
    if r is None:
        return None
    return {"x": r.x, "y": r.y, "kind": r.kind.value, "distance": r.distance,
            "entity": r.entity.id if r.entity is not None else None}


def cases() -> list[dict]:
    ents = [
        Rectangle(id="r1", x=0, y=0, width=100, height=60),
        Rectangle(id="r2", x=120, y=10, width=40, height=40, corner_radius=8.0),
        Circle(id="c1", cx=50, cy=30, radius=20),
        Ellipse(id="e1", cx=200, cy=80, rx=30, ry=15, rotation=30),
        Arc(id="a1", cx=250, cy=30, radius=25, start_angle=20, end_angle=200),
        Line(id="l1", x1=-20, y1=-10, x2=140, y2=70),
        Line(id="l2", x1=-20, y1=70, x2=140, y2=-10),
        Polyline(id="p1", points=[(300, 0), (340, 0), (340, 40), (320, 60), (300, 40)], is_closed=True),
        Polyline(id="p2", points=[(300, 100), (330, 120), (360, 100)], is_closed=False),
        make_star(400, 40, 30, 5, 0.4, id="s1"),
        Text(id="t1", x=0, y=100, content="AB", height=20.0),
    ]
    doc = make_doc(ents, width=500.0, height=300.0, margin=15.0)
    doc.layers.append(type(doc.layers[0])(name="oculta", visible=False))
    ents.append(Rectangle(id="hidden", x=600, y=600, width=10, height=10, layer="oculta"))
    doc.entities.append(ents[-1])

    out: dict = {}
    # -- pontos notaveis ----------------------------------------------------------------
    out["entity_points"] = {
        e.id: [[list(p), k.value] for p, k in entity_points(e)] for e in doc.entities
    }
    out["closest_on_entity"] = {
        e.id: [{"q": q, "out": (None if (r := closest_on_entity(e, *q)) is None else [list(r[0]), r[1]])}
               for q in ((10.0, 10.0), (60.0, 90.0), (-5.0, 30.0), (210.0, 80.0))]
        for e in doc.entities
    }
    pares = [("r1", "c1"), ("l1", "l2"), ("r1", "l1"), ("p1", "e1"), ("c1", "l2"), ("r2", "r1"), ("s1", "p2")]
    por_id = {e.id: e for e in doc.entities}
    out["segment_intersections"] = [
        {"pair": [a, b], "out": [list(p) for p in segment_intersections(por_id[a], por_id[b])]} for a, b in pares
    ]

    # -- snap do cursor ---------------------------------------------------------------
    consultas = [(0.2, 0.1), (100.0, 60.5), (50.0, 51.0), (70.0, 30.0), (30.0, 18.0), (50.0, 30.0), (24.0, 18.0),
                 (-20.0, -10.0), (140.0, 69.0), (65.0, 25.0), (10.0, 14.0), (99.0, 31.0), (250.5, 55.0), (210.0, 95.0),
                 (500.0, 300.0), (250.0, 150.0), (330.0, 100.5), (123.4, 12.6), (0.5, 299.0), (15.2, 15.1)]
    variantes = {
        "padrao": SnapSettings(),
        "so_nos": SnapSettings(centers=False, midpoints=False, intersections=False, on_span=False),
        "com_grelha": SnapSettings(grid=True, grid_spacing=10.0),
        "sem_material": SnapSettings(material=False),
        "raio_grande": SnapSettings(radius_px=40.0),
        "desligado": SnapSettings(enabled=False),
        "sem_geometria": SnapSettings(geometry=False, grid=True, grid_spacing=25.0),
    }
    snaps = {}
    for nome, st in variantes.items():
        eng = SnapEngine(document=doc, settings=st)
        linhas = []
        for ppm in (1.0, 3.5):
            for q in consultas:
                linhas.append({"q": q, "ppm": ppm, "out": _res(eng.snap(q[0], q[1], ppm))})
        linhas.append({"q": [50.0, 30.0], "ppm": 2.0, "exclude": ["c1"], "out": _res(eng.snap(50.0, 30.0, 2.0, {"c1"}))})
        snaps[nome] = linhas
    out["snap"] = snaps
    out["settings_roundtrip"] = SnapSettings(radius_px=7.0, grid=True).to_dict()

    # -- ima ao mover ---------------------------------------------------------------------
    eng = SnapEngine(document=doc, settings=SnapSettings())
    movendo = [por_id["c1"]]
    movs = []
    for dx, dy in ((0.0, 0.0), (3.0, 1.0), (-48.0, -29.5), (100.0, 0.2), (71.0, -9.0), (170.0, 20.0), (2.0, 2.0)):
        for ancora in (None, (70.0, 30.0), (50.0, 50.0)):
            for ppm in (1.0, 4.0):
                r = eng.snap_moving(movendo, dx, dy, ppm, ancora)
                movs.append({"dx": dx, "dy": dy, "anchor": ancora, "ppm": ppm,
                             "out": [r[0], r[1], _res(r[2])]})
    out["snap_moving_circle"] = movs
    grupo = [por_id["r2"], por_id["e1"]]
    out["snap_moving_group"] = [
        {"dx": dx, "dy": dy, "out": [r[0], r[1], _res(r[2])]}
        for dx, dy in ((0.0, 0.0), (-20.0, 5.0), (140.0, 10.0), (1.0, 1.0), (-100.0, -10.0))
        for r in [eng.snap_moving(grupo, dx, dy, 2.0)]
    ]
    return [{"name": "snap", "document": doc._to_dict(), "data": out}]
