# Porte do CNC CAM: Python -> C++

O app em Python (`Desktop\CNC-CAM`) continua intocado: é o **oráculo**. Cada módulo portado
é conferido contra ele por *golden files* (`tests/golden/*.json`, gerados por
`scripts/golden/gen_cam_golden.py` rodando o código original).

Como compilar e testar: ver `docs/TOOLCHAIN.md`.

## Estado por módulo

| Python | C++ | Estado | Conferido por |
|---|---|---|---|
| `cad/entities.py` | `src/cad/entities.*` | feito | golden de documentos (via percursos) |
| `cad/document.py` | `src/cad/document.*`, `material.*`, `entity_json.*` | feito | abre/grava `.cnc` do Python |
| `cad/selection.py`, `doc_vars.py` | `src/cad/selection.*`, `doc_vars.*` | feito | uso nos percursos |
| `cam/model.py` | `src/cam/model.*` | feito | goldens de percurso |
| `cam/tools.py`, `presets.py` | `src/cam/tools.*`, `presets.*` | feito | goldens |
| `cam/nesting.py` | `src/cam/nesting.*` | feito | goldens |
| `cam/profile.py` | `src/cam/profile.*` | feito | **51 cenários, 13 207 movimentos** |
| `cam/pocket.py` | `src/cam/pocket.*` | feito | **29 cenários, 12 742 movimentos** |
| `cam/drill.py` | `src/cam/drill.*` | feito | **20 cenários, 1 975 movimentos** |
| `cam/vcarve.py` | `src/cam/vcarve.*` | feito | **29 cenários, 41 275 movimentos** |
| `cam/job.py` | `src/cam/job.*` | feito (variant de settings) | goldens |
| `cam/fluting.py` | `src/cam/fluting.*` | feito | **12 cenários, 2 184 movimentos** |
| `cam/texture.py` | `src/cam/texture.*` (+ `utils/py_random.*`: Mersenne Twister do CPython) | feito | **11 cenários, 16 210 movimentos** |
| `cam/keyhole.py` | `src/cam/keyhole.*` | feito | 7 cenários |
| `cam/inlay.py` | `src/cam/inlay.*` | feito | **6 cenários, 8 752 movimentos** |
| `cam/dragknife.py` | `src/cam/dragknife.*` | feito | 9 cenários |
| `cam/gcode.py` | `src/cam/gcode.*` | feito | **8 variantes, texto idêntico byte a byte** |
| `cam/templates.py` | `src/cam/templates.*` | feito | goldens (modelos da casa) |
| `cam/setup_sheet.py` | `src/cam/setup_sheet.*` | feito | HTML idêntico |
| `cam/vectric_tools.py` | `src/cam/vectric_tools.*` | feito | **arquivo real do Aspire desta máquina: 26 ferramentas idênticas** |
| `utils/user_config.py` | `src/utils/user_config.*` | feito | |
| `cad/layout.py`, `importers`, `pdf_*` | — | pendente | |
| `sim/*` | — | pendente | |
| `tools/*`, `viewport/*`, `ui/*`, `app.py` | — | pendente | |

`cad/project_sheet.py` não será portado: o próprio teste do Python exige que esse módulo
não exista mais (folha UNY CV removida).

## Decisões de fidelidade (por que a conta sai igual)

* **GEOS 3.14.1** (mesmo do shapely 2.2.0) e **Clipper 6.4.2** (mesmo código do pyclipper).
* `round()` do Python é bancário → `std::nearbyint`; `math.radians(x)` = `x*(pi/180)`;
  o `shapely.affinity.rotate` usa `x*pi/180`.
* `math.hypot`/`math.dist` do CPython têm algoritmo próprio (`cnc::hypot`), e o `sum()` de floats
  do Python ≥ 3.12 é compensado (Neumaier) → `PySum`/`py_sum`.
* `//` e `%` de float seguem o CPython (`floordiv_f`, `fmod_py`).
* `sorted()`/`list.sort()` são estáveis → `std::stable_sort`; `min()`/`max()` devolvem o primeiro empate.
* Settings dos percursos: `std::variant` (cópia = deepcopy); `RawSettings` guarda o JSON dos tipos
  ainda não portados para não perder nada ao abrir/gravar.

## Armadilhas encontradas

* Comando Bash grande (> ~7 KB) falha com "unexpected EOF": arquivos grandes via ferramenta Write.
* O Python desta máquina não funciona com `-I` (depende de variáveis de ambiente).
