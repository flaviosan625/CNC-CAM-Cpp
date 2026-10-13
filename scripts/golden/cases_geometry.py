"""Cenários de cad/geometry.py: conversão GEOS, booleanas, offset, filete, aparar, estender, curva."""

from __future__ import annotations

from golden_lib import make_doc

from src.cad.entities import (
    Arc, Circle, Ellipse, Line, Polyline, Rectangle, Text, make_polygon, make_star,
)
from src.cad import geometry as g


def _ents(entities):
    return [{"points": [list(p) for p in e.points], "closed": e.is_closed, "layer": e.layer,
             "color": list(e.color)} for e in entities]


def _geom(geom):
    if geom is None:
        return None
    return {"type": geom.geom_type, "area": geom.area, "length": geom.length,
            "empty": geom.is_empty, "out": _ents(g.from_shapely(geom))}


def cases() -> list[dict]:
    ents = [
        Rectangle(id="r1", x=0, y=0, width=100, height=60),
        Rectangle(id="r2", x=60, y=30, width=80, height=60, corner_radius=10.0, layer="L2", color=(0.8, 0.2, 0.1)),
        Circle(id="c1", cx=50, cy=30, radius=20),
        Circle(id="c2", cx=120, cy=60, radius=25),
        Ellipse(id="e1", cx=200, cy=80, rx=30, ry=15, rotation=30),
        Arc(id="a1", cx=250, cy=30, radius=25, start_angle=20, end_angle=200),
        Line(id="l1", x1=-20, y1=-10, x2=140, y2=70),
        Line(id="l2", x1=-20, y1=70, x2=140, y2=-10),
        Line(id="l3", x1=10, y1=-30, x2=10, y2=-5),
        Polyline(id="p1", points=[(300, 0), (340, 0), (340, 40), (320, 60), (300, 40)], is_closed=True),
        Polyline(id="p2", points=[(300, 100), (330, 120), (360, 100), (380, 140)], is_closed=False),
        Polyline(id="p3", points=[(360, 100), (400, 90), (420, 130)], is_closed=False),
        Polyline(id="p4", points=[(420, 130), (430, 160)], is_closed=False),
        Polyline(id="p5", points=[(0, 200), (50, 250), (100, 200), (50, 150)], is_closed=True),
        Polyline(id="bow", points=[(0, 300), (60, 360), (0, 360), (60, 300)], is_closed=True),  # autointersecta
        make_star(400, 40, 30, 5, 0.4, id="s1"),
        make_polygon(500, 60, 25, 6, 0.0, id="h1"),
        Text(id="t1", x=0, y=400, content="AB", height=20.0),
    ]
    doc = make_doc(ents, width=600.0, height=500.0)
    by_id = {e.id: e for e in ents}

    def pick(*ids):
        return [by_id[i] for i in ids]

    out: dict = {}

    out["to_shapely"] = {e.id: _geom(g.to_shapely(e)) for e in ents}

    ops = []
    for ids in (("r1", "c1"), ("r1", "r2"), ("c1", "c2"), ("r1", "r2", "c2"), ("p1", "s1"), ("c1", "l1"),
                ("r1", "h1"), ("t1", "r1"), ("r1",), ("bow", "p5")):
        ops.append({"ids": list(ids), "union": _ents(g.boolean_union(pick(*ids))),
                    "intersect": _ents(g.boolean_intersect(pick(*ids)))})
    out["boolean"] = ops
    subs = []
    for base, cut in ((("r1",), ("c1",)), (("r1", "r2"), ("c1", "c2")), (("c1",), ("r1",)), (("r1",), ("r2",)),
                      (("r1",), ("p1",)), (("p5",), ("bow",)), (("t1",), ("r1",)), (("r1",), ("l1",))):
        subs.append({"base": list(base), "cut": list(cut), "out": _ents(g.boolean_subtract(pick(*base), pick(*cut)))})
    out["subtract"] = subs

    offsets = []
    for eid in ("r1", "r2", "c1", "e1", "a1", "l1", "p1", "p2", "p5", "s1", "t1", "bow", "h1"):
        for dist in (1.5, 5.0, 12.0):
            for outward in (True, False):
                for join in ("round", "mitre", "bevel"):
                    offsets.append({"id": eid, "dist": dist, "outward": outward, "join": join,
                                    "out": _ents(g.offset_entity(by_id[eid], dist, outward, join))})
    out["offset"] = offsets

    filetes = []
    quad = [(0.0, 0.0), (40.0, 0.0), (40.0, 30.0), (0.0, 30.0)]
    tri = [(0.0, 0.0), (50.0, 5.0), (20.0, 40.0)]
    zig = [(0.0, 0.0), (30.0, 10.0), (60.0, 0.0), (90.0, 12.0)]
    for pts in (quad, tri, zig):
        for idx in range(len(pts)):
            for radius in (0.0, 3.0, 8.0, 100.0):
                for closed in (True, False):
                    for seg in (12, 4):
                        filetes.append({"pts": [list(p) for p in pts], "idx": idx, "radius": radius,
                                        "closed": closed, "seg": seg,
                                        "out": [list(p) for p in g.fillet_corner(pts, idx, radius, seg, closed)]})
    out["fillet"] = filetes

    joins = []
    for ids, tol in ((("p2", "p3", "p4"), 0.1), (("p2", "p3"), 0.1), (("p2", "p4"), 0.1), (("l1", "l2"), 0.1),
                     (("p2", "p3", "p4"), 100.0), (("p1", "p2"), 0.1), (("p3", "p2"), 0.5)):
        joins.append({"ids": list(ids), "tol": tol, "out": _ents(g.join_open_vectors(pick(*ids), tol))})
    # cadeia que fecha sobre si mesma
    a = Polyline(id="ja", points=[(0, 0), (10, 0), (10, 10)], is_closed=False)
    b = Polyline(id="jb", points=[(10, 10), (0, 10), (0, 0.05)], is_closed=False)
    ents.extend([a, b])
    by_id.update({"ja": a, "jb": b})
    doc.entities = list(ents)
    joins.append({"ids": ["ja", "jb"], "tol": 0.1, "out": _ents(g.join_open_vectors([a, b], 0.1))})
    out["join_open"] = joins

    extends = []
    for lid in ("l1", "l2", "l3"):
        for targets in (("r1",), ("c1",), ("r1", "c1", "p1"), ("p5",)):
            for endpoint in (None, "start", "end"):
                line = Line(x1=by_id[lid].x1, y1=by_id[lid].y1, x2=by_id[lid].x2, y2=by_id[lid].y2)
                ok = g.extend_line_to(line, pick(*targets), endpoint)
                extends.append({"line": lid, "targets": list(targets), "endpoint": endpoint, "ok": ok,
                                "out": [line.x1, line.y1, line.x2, line.y2]})
    out["extend"] = extends

    trims = []
    for eid, others in (("l1", ("l2", "r1", "c1")), ("l2", ("l1", "r1")), ("r1", ("l1", "l2", "c1")),
                        ("c1", ("l1", "l2", "r1")), ("p5", ("l1", "r1", "c1")), ("p2", ("p3", "l1", "e1")),
                        ("a1", ("l1", "l2", "c2"))):
        e = by_id[eid]
        x0, y0, x1, y1 = e.bounding_box()
        for fx, fy in ((0.1, 0.1), (0.5, 0.5), (0.9, 0.2), (0.3, 0.8), (0.0, 1.0)):
            x, y = x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy
            trims.append({"id": eid, "others": list(others), "x": x, "y": y,
                          "out": _ents(g.trim_at_point(e, x, y, pick(*others)))})
    out["trim"] = trims

    bounds = []
    for ids in (("r1",), ("r1", "c2"), ("p1", "s1", "h1"), ("c1", "l1"), ("t1",), ("p2", "p3")):
        for offset in (0.0, 4.0, -3.0):
            for rubber in (False, True):
                bounds.append({"ids": list(ids), "offset": offset, "rubber": rubber,
                               "out": _ents(g.vector_boundary(pick(*ids), offset, rubber))})
    out["boundary"] = bounds

    curves = []
    pts_open = [(0.0, 0.0), (10.0, 8.0), (20.0, 3.0), (35.0, 15.0), (50.0, 0.0)]
    for pts in (pts_open, pts_open[:3], pts_open[:2], [(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]):
        for closed in (True, False):
            for smooth, subdiv in ((0.5, 8), (0.5, 4), (0.2, 6), (1.0, 3)):
                curves.append({"pts": [list(p) for p in pts], "closed": closed, "smooth": smooth, "subdiv": subdiv,
                               "out": [list(p) for p in g.fit_curve(pts, closed, smooth, subdiv)]})
    out["fit_curve"] = curves

    return [{"name": "geometry", "document": doc._to_dict(), "data": out}]
