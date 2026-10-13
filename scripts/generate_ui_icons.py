"""Bake the PySide6 reference icons into PNG resources for the C++ port."""

from pathlib import Path
import sys

from PySide6.QtWidgets import QApplication

PYTHON_APP = Path(__file__).resolve().parents[2] / "CNC-CAM"
sys.path.insert(0, str(PYTHON_APP))

from src.ui.tool_panel import get_icon  # noqa: E402


NAMES = [
    "import", "export", "preview_3d", "measure", "grid", "workspace_setup", "select",
    "circle", "ellipse", "rectangle", "polygon", "star", "polyline", "text", "line",
    "arc", "construction_line", "freehand", "drill_point", "vector_texture",
    "text_on_curve", "dimension", "trace_bitmap", "move", "distort", "mirror", "scale",
    "array_copy", "circular_copy", "rotate", "node_edit", "fillet", "trim",
    "interactive_trim", "extend", "fit_curves", "join", "weld", "subtract", "intersect",
    "group", "ungroup", "offset", "boundary", "convert_text", "align_left",
    "align_center_h", "align_right", "align_top", "align_center_v", "align_bottom",
    "layout_optimizer", "zoom_fit", "zoom_in", "zoom_out", "pan", "layers", "lock",
    "lock_open", "eye", "eye_closed",
]


def main() -> None:
    app = QApplication.instance() or QApplication([])
    output = Path(__file__).resolve().parents[1] / "assets" / "tool-icons"
    output.mkdir(parents=True, exist_ok=True)
    missing = []
    for name in NAMES:
        icon = get_icon(name)
        if icon.isNull():
            missing.append(name)
            continue
        icon.pixmap(96, 96).save(str(output / f"{name}.png"))
    if missing:
        raise SystemExit(f"icons not found in Python reference: {', '.join(missing)}")


if __name__ == "__main__":
    main()
