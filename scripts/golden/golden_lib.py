"""Infraestrutura dos "golden files": roda o app em Python (a referência) e grava o
resultado em JSON para os testes em C++ compararem.

Como usar (a partir da pasta do projeto Python, com o venv dele):

    cd C:\\Users\\flavi\\Desktop\\CNC-CAM
    venv\\Scripts\\python.exe "C:\\Users\\flavi\\Desktop\\CNC CAM C++\\scripts\\golden\\gen_cam_golden.py"

Cada caso guarda o documento COMPLETO como o Python o grava (o mesmo `.cnc` que o C++
precisa abrir) e, para cada percurso, o que o Python calculou. O teste em C++ abre o
documento, recalcula e confere movimento por movimento.

O app em Python NÃO é alterado: este script só importa e executa o código dele.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

PYTHON_APP = Path(os.environ.get("CNC_PY_APP", r"C:\Users\flavi\Desktop\CNC-CAM"))
sys.path.insert(0, str(PYTHON_APP))

# Isola a configuração: nunca toca em ~/.cnc-cam do usuário.
import tempfile  # noqa: E402

os.environ.setdefault("CNC_CAM_CONFIG", tempfile.mkdtemp(prefix="cnc-golden-"))

from PySide6.QtWidgets import QApplication  # noqa: E402

_app = QApplication.instance() or QApplication([])

from src.cad.document import Document, MaterialSetup  # noqa: E402
from src.cam.job import ToolpathJob  # noqa: E402
from src.cam.model import ToolpathType  # noqa: E402
from src.cam.tools import Tool  # noqa: E402

GOLDEN_DIR = Path(__file__).resolve().parents[2] / "tests" / "golden"


def versions() -> dict:
    import importlib.metadata as md

    out = {"python": sys.version.split()[0]}
    for pkg in ("shapely", "pyclipper", "numpy", "PySide6", "ezdxf", "svgpathtools", "pymupdf"):
        try:
            out[pkg] = md.version(pkg)
        except md.PackageNotFoundError:
            pass
    return out


def toolpath_to_dict(tp) -> dict:
    return {
        "name": tp.name,
        "kind": tp.kind.value,
        "tool_name": tp.tool_name,
        "tool_number": tp.tool_number,
        "spindle_speed": tp.spindle_speed,
        "feed_rate": tp.feed_rate,
        "plunge_rate": tp.plunge_rate,
        "notes": list(tp.notes),
        "summary": tp.summary(),
        "moves": [[m.kind.value, m.x, m.y, m.z, m.seconds] for m in tp.moves],
    }


def run_case(name: str, doc: Document) -> dict:
    """Recalcula todos os percursos do documento e grava o que o Python produziu."""
    errors = doc.recalculate_toolpaths()
    return {
        "name": name,
        "document": doc._to_dict(),
        "expected": {
            "errors": errors,
            "jobs": [
                {
                    "name": job.name,
                    "error": job.error,
                    "summary": job.summary(),
                    "notes": job.all_notes(),
                    "toolpaths": [toolpath_to_dict(tp) for tp in job.results],
                }
                for job in doc.toolpaths
            ],
        },
    }


def make_doc(entities, *, thickness: float = 18.0, width: float = 600.0, height: float = 400.0,
             **material) -> Document:
    doc = Document()
    doc.material = MaterialSetup(width=width, height=height, thickness=thickness, **material)
    doc.entities = list(entities)
    return doc


def add_job(doc: Document, kind: ToolpathType, tool: Tool, settings, entities, name: str | None = None,
            job_id: str | None = None) -> ToolpathJob:
    job = ToolpathJob(
        name=name or settings.name,
        kind=kind,
        tool=tool,
        settings=settings,
        entity_ids=[e.id for e in entities],
    )
    job.id = job_id or f"job{len(doc.toolpaths) + 1}"
    doc.toolpaths.append(job)
    return job


def write_golden(filename: str, cases: list[dict]) -> Path:
    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    payload = {"generated_by": versions(), "cases": cases}
    target = GOLDEN_DIR / filename
    target.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    return target
