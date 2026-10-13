# Código de terceiros

Tudo aqui é copiado sem alteração. `CHECKSUMS.txt` guarda o SHA-256 de cada arquivo.

| Pasta | O que é | Origem | Licença |
|---|---|---|---|
| `clipper/` | Clipper **6.4.2** (Angus Johnson) | `src/` do sdist do pyclipper 1.4.0 (PyPI, sha256 `9882bd88…c8e1`) | Boost 1.0 |
| `nlohmann/` | nlohmann/json 3.12.0 (arquivo único) | github.com/nlohmann/json, release v3.12.0 | MIT |
| `doctest/` | doctest 2.5.3 (arquivo único) | github.com/doctest/doctest, tag v2.5.3 | MIT |

O Clipper é o **mesmo código** que o `pyclipper` usa no app em Python — por isso
os offsets e booleanas dão o mesmo resultado nos dois.

Não vendorizados (compilados à parte, ver `docs/TOOLCHAIN.md`): Qt 6.12.0 e GEOS 3.14.1
(o GEOS é o motor do shapely 2.2.0 do app Python).
