"""Gera os golden files de CAM a partir do app em Python.

    cd C:\\Users\\flavi\\Desktop\\CNC-CAM
    venv\\Scripts\\python.exe "<este arquivo>" [profile pocket ...]

Sem argumentos, gera todos os tipos que tiverem um módulo cases_<tipo>.py.
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from golden_lib import write_golden  # noqa: E402

KINDS = ["profile", "pocket", "drill", "vcarve", "fluting", "texture", "keyhole", "inlay", "dragknife", "gcode", "misc", "layout", "snap", "geometry"]


def main() -> int:
    wanted = sys.argv[1:] or KINDS
    for kind in wanted:
        try:
            module = importlib.import_module(f"cases_{kind}")
        except ModuleNotFoundError:
            print(f"- {kind}: sem casos ainda (cases_{kind}.py não existe)")
            continue
        cases = module.cases()
        target = write_golden(f"{kind}.json", cases)
        moves = sum(len(tp["moves"]) for c in cases if "expected" in c
                    for j in c["expected"]["jobs"] for tp in j["toolpaths"])
        erros = [c["name"] for c in cases if "expected" in c and c["expected"]["errors"]]
        print(f"- {kind}: {len(cases)} casos, {moves} movimentos -> {target.name}")
        for nome in erros:
            print(f"    aviso: o caso '{nome}' terminou com erro no Python")
    return 0


if __name__ == "__main__":
    sys.exit(main())
