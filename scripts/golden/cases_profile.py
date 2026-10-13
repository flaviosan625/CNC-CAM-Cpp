"""Cenários do percurso de perfil. Cada um roda no Python e vira um golden file."""

from __future__ import annotations

import copy

from golden_lib import add_job, make_doc, run_case

from src.cad.entities import (
    Arc, Circle, Ellipse, Polyline, Rectangle, Text, make_polygon, make_star,
)
from src.cam.model import (
    CutDirection, LeadType, MachineOrder, OpenSide, ProfileSide, RampType, ToolpathType,
)
from src.cam.presets import house_tool
from src.cam.profile import ProfileSettings

PROFILE = ToolpathType.PROFILE


def _rect(i="r1", x=0.0, y=0.0, w=100.0, h=60.0, **kw):
    return Rectangle(id=i, x=x, y=y, width=w, height=h, **kw)


def _circle(i="c1", cx=0.0, cy=0.0, r=25.0):
    return Circle(id=i, cx=cx, cy=cy, radius=r)


def _case(name, entities, settings, tool=None, **material):
    doc = make_doc(entities, **material)
    add_job(doc, PROFILE, tool or house_tool(6.0, 6.0), settings, entities)
    return run_case(name, doc)


def cases() -> list[dict]:
    out = []
    S = ProfileSettings

    # -- lado da fresa -------------------------------------------------------
    r = _rect()
    out.append(_case("retangulo_por_fora", [r], S(cut_depth=18.0)))
    out.append(_case("retangulo_por_dentro", [_rect()], S(side=ProfileSide.INSIDE, cut_depth=10.0)))
    out.append(_case("retangulo_sobre_a_linha", [_rect()], S(side=ProfileSide.ON, cut_depth=6.0)))
    out.append(_case("retangulo_com_folga", [_rect()], S(allowance=0.5, cut_depth=12.0)))
    out.append(_case("sentido_climb", [_rect()], S(direction=CutDirection.CLIMB)))
    out.append(_case("circulo_fora", [_circle(cx=50, cy=50)], S(cut_depth=15.0)))
    out.append(_case("elipse_fora", [Ellipse(id="e1", cx=80, cy=60, rx=50, ry=30, rotation=20.0)], S()))
    out.append(_case("retangulo_girado_cantos_redondos",
                     [_rect(rotation=30.0, corner_radius=8.0)], S(cut_depth=8.0)))
    out.append(_case("estrela", [make_star(100, 100, 40, 5, 0.45, id="s1")], S(cut_depth=9.0)))
    out.append(_case("poligono_hexagono", [make_polygon(60, 60, 30, 6, id="h1")], S()))

    # -- aninhamento: furo e peca ---------------------------------------------
    peca = _rect("peca", 0, 0, 120, 80)
    furo = _circle("furo", 60, 40, 15)
    out.append(_case("peca_com_furo_separados", [peca, furo], S(cut_depth=18.0)))
    peca2, furo2 = _rect("peca", 0, 0, 120, 80), _circle("furo", 60, 40, 15)
    out.append(_case("peca_com_furo_juntos", [peca2, furo2], S(separate_interior=False)))
    out.append(_case("furo_grande_demais_para_a_fresa",
                     [_circle("miudo", 20, 20, 2.0)], S(side=ProfileSide.INSIDE, cut_depth=5.0)))
    ilha = [_rect("a", 0, 0, 200, 200), _rect("b", 20, 20, 160, 160), _circle("c", 100, 100, 30)]
    out.append(_case("ilha_dentro_de_furo", ilha, S(cut_depth=6.0)))

    # -- passadas em Z ---------------------------------------------------------
    out.append(_case("passo_exato", [_rect()], S(cut_depth=19.0, maintain_exact_step=True)))
    out.append(_case("passo_variavel_15_por_cento", [_rect()], S(cut_depth=19.0)))
    out.append(_case("profundidade_inicial", [_rect()], S(start_depth=3.0, cut_depth=9.0)))
    out.append(_case("chapa_com_zero_na_base", [_rect()], S(cut_depth=8.0), zero_z="bottom"))
    out.append(_case("alturas_de_seguranca", [_rect()], S(cut_depth=6.0), clearance_z=12.0, plunge_gap=4.0))

    # -- rampas ----------------------------------------------------------------
    for ramp in (RampType.NONE, RampType.SMOOTH, RampType.ZIGZAG, RampType.SPIRAL):
        out.append(_case(f"rampa_{ramp.value}", [_rect()], S(cut_depth=12.0, ramp=ramp, ramp_distance=15.0)))
    out.append(_case("rampa_por_angulo", [_rect()], S(cut_depth=12.0, ramp_angle=4.0)))

    # -- entradas e saidas -----------------------------------------------------
    out.append(_case("entrada_linear", [_rect()], S(lead=LeadType.LINEAR, lead_length=6.0, lead_angle=30.0)))
    out.append(_case("entrada_em_arco", [_rect()], S(lead=LeadType.ARC, lead_radius=4.0, lead_angle=70.0)))
    out.append(_case("entrada_sem_saida", [_rect()], S(lead=LeadType.LINEAR, lead_out=False)))
    out.append(_case("corte_alem_do_ponto_inicial", [_circle(cx=60, cy=60, r=30)], S(overcut=4.0)))

    # -- pontes ----------------------------------------------------------------
    out.append(_case("pontes_3d", [_rect()], S(tabs=True, tab_count=4, tab_length=12.0, tab_thickness=3.0)))
    out.append(_case("pontes_planas", [_rect()], S(tabs=True, tabs_3d=False, tab_count=3)))
    out.append(_case("pontes_por_espacamento", [_rect(w=300, h=200)],
                     S(tabs=True, tab_spacing=120.0, tab_min_count=2, tab_max_count=6)))
    out.append(_case("pontes_comecando_no_inicio", [_rect()], S(tabs=True, tab_first_at_start=True)))
    rect_manual = _rect("manual")
    out.append(_case("pontes_manuais", [rect_manual],
                     S(tabs=True, tab_count=2, manual_tabs=[("manual", 50.0, -3.0), ("manual", 103.0, 30.0)])))
    out.append(_case("pontes_so_manuais", [_rect("m2")],
                     S(tabs=True, tab_manual_only=True, manual_tabs=[("m2", 25.0, 0.0)])))

    # -- passada final ---------------------------------------------------------
    out.append(_case("passada_final_separada", [_rect()],
                     S(separate_last_pass=True, last_pass_allowance=0.3, cut_depth=12.0)))
    out.append(_case("passada_final_invertida", [_rect()],
                     S(separate_last_pass=True, reverse_last_pass=True)))

    # -- vetor aberto ----------------------------------------------------------
    aberto = Polyline(id="ab", points=[(0, 0), (60, 0), (60, 40), (110, 40)], is_closed=False)
    for lado in (OpenSide.ON, OpenSide.LEFT, OpenSide.RIGHT):
        out.append(_case(f"aberto_{lado.value}", [copy.deepcopy(aberto)], S(open_side=lado, cut_depth=5.0)))
    out.append(_case("arco_aberto", [Arc(id="ar", cx=0, cy=0, radius=40, start_angle=10, end_angle=200)],
                     S(open_side=OpenSide.LEFT, cut_depth=4.0)))

    # -- ordem entre varias pecas ----------------------------------------------
    pecas = [_rect(f"p{i}", x, y, 40, 30) for i, (x, y) in
             enumerate([(200, 10), (10, 200), (100, 100), (10, 10), (210, 210), (110, 10)])]
    for ordem in MachineOrder:
        out.append(_case(f"ordem_{ordem.value}", copy.deepcopy(pecas), S(order=ordem, cut_depth=6.0)))
    out.append(_case("varias_ordens_a_melhor", copy.deepcopy(pecas),
                     S(orders=("left_right", "bottom_top", "shortest"), cut_depth=6.0)))
    out.append(_case("usar_pontos_iniciais", [_rect("p1", 0, 0, 50, 30), _rect("p2", 100, 0, 50, 30)],
                     S(use_start_points=True)))

    # -- tolerancia e texto ----------------------------------------------------
    out.append(_case("tolerancia_grossa", [_circle(cx=40, cy=40, r=30)], S(tolerance=0.1)))
    out.append(_case("tolerancia_fina", [_circle(cx=40, cy=40, r=30)], S(tolerance=0.002)))
    out.append(_case("texto_com_furos_nas_letras", [Text(id="t1", x=10, y=10, content="ABO", height=40.0)],
                     S(cut_depth=6.0)))
    out.append(_case("ferramenta_de_2mm", [_rect(w=30, h=20)], S(cut_depth=6.0), tool=house_tool(2.0, 3.0)))
    return out
