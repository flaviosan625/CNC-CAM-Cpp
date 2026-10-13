# Toolchain (Windows)

Tudo em `C:\Users\<usuario>\toolchains\` (nada é instalado no sistema):

| Peça | Versão | Como foi obtida |
|---|---|---|
| MSVC | 14.51 (VS 2026 Build Tools, já instalado) | — |
| CMake / Ninja | 4.3 / 1.13 (embutidos no VS) | — |
| Qt | 6.12.0 msvc2022_64 | `python scripts/install_qt.py --out <pasta>` (confere SHA-256/SHA-1) |
| GEOS | 3.14.1 estático | `scripts\build_geos.bat <fonte>` (tarball de download.osgeo.org) |
| Clipper | 6.4.2 | vendorizado em `third_party/clipper` |

O `aqt` 3.3 não entende o layout novo do servidor da Qt (≥ 6.10); por isso o `install_qt.py`.

## Compilar e testar

```
scripts\build.bat RelWithDebInfo --target cnc_tests
scripts\test.bat                      # todos os testes
scripts\test.bat -tc="*perfil*"       # filtra por nome
```

`RelWithDebInfo` (/MD) é a configuração padrão: Qt e GEOS são Release; não misturar com Debug.

## Regenerar os golden files (precisa do app Python)

```
cd C:\Users\flavi\Desktop\CNC-CAM
venv\Scripts\python.exe "<projeto C++>\scripts\golden\gen_cam_golden.py" [profile pocket ...]
```
