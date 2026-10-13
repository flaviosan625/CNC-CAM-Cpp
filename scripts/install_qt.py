"""Instala o Qt (MSVC) direto do repositório oficial da Qt, sem o aqt.

O aqt 3.3 não entende o layout novo do servidor (a partir do Qt 6.10 existe um
repositório por arquitetura: ``qt6_6120/qt6_6120_msvc2022_64``). Este script
faz o mínimo necessário e confere tudo o que baixa:

1. baixa ``Updates.xml`` e confere o SHA-256 publicado ao lado dele;
2. escolhe só os módulos pedidos (o pacote completo passa de 1 GB);
3. baixa cada ``.7z``, confere o SHA-1 publicado e extrai com py7zr.

Uso (precisa do py7zr, que vem junto do aqtinstall)::

    python install_qt.py --out C:/Users/voce/toolchains/Qt
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

BASE = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"

#: Módulos que o CNC CAM usa. O resto do pacote (QML, docs, traduções) fica de fora.
DEFAULT_MODULES = ("qtbase", "qtsvg", "qttools", "d3dcompiler_47", "opengl32sw")


def fetch(url: str, retries: int = 4) -> bytes:
    for tentativa in range(1, retries + 1):
        try:
            with urllib.request.urlopen(url, timeout=60) as resp:
                return resp.read()
        except Exception as exc:  # rede instável: tenta de novo
            if tentativa == retries:
                raise
            print(f"  falhou ({exc}); tentativa {tentativa + 1}/{retries}")
            time.sleep(2 * tentativa)
    raise AssertionError("inalcançável")


def download(url: str, dest: Path, expected_sha1: str) -> None:
    """Baixa em pedaços, mostrando o progresso, e confere o SHA-1."""
    if dest.exists() and hashlib.sha1(dest.read_bytes()).hexdigest() == expected_sha1:
        print(f"  já baixado: {dest.name}")
        return
    with urllib.request.urlopen(url, timeout=60) as resp:
        total = int(resp.headers.get("Content-Length", "0"))
        sha1 = hashlib.sha1()
        done, marca = 0, 0
        with dest.open("wb") as fh:
            while chunk := resp.read(1 << 20):
                fh.write(chunk)
                sha1.update(chunk)
                done += len(chunk)
                if total and done * 10 // total > marca:
                    marca = done * 10 // total
                    print(f"  {dest.name[:28]:28s} {marca * 10:3d}%  ({done / 1e6:.0f} MB)")
    if sha1.hexdigest() != expected_sha1:
        dest.unlink()
        raise SystemExit(f"SHA-1 diferente em {dest.name}: "
                         f"{sha1.hexdigest()} != {expected_sha1}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", default="6.12.0")
    ap.add_argument("--arch", default="win64_msvc2022_64")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--cache", type=Path, default=None,
                    help="onde guardar os .7z (padrão: <out>/.cache)")
    ap.add_argument("--modules", nargs="*", default=list(DEFAULT_MODULES))
    args = ap.parse_args()

    try:
        import py7zr
    except ImportError:
        raise SystemExit("falta o py7zr:  pip install py7zr")

    nodots = args.version.replace(".", "") + ("0" if args.version.count(".") == 1 else "")
    repo = f"{BASE}/qt6_{nodots}/qt6_{nodots}_{args.arch.removeprefix('win64_')}"
    pkg = f"qt.qt6.{nodots}.{args.arch}"
    cache = args.cache or args.out / ".cache"
    cache.mkdir(parents=True, exist_ok=True)

    print(f"Índice: {repo}/Updates.xml")
    xml = fetch(f"{repo}/Updates.xml")
    publicado = fetch(f"{repo}/Updates.xml.sha256").split()[0].decode()
    if hashlib.sha256(xml).hexdigest() != publicado:
        raise SystemExit("SHA-256 do Updates.xml não confere — abortando.")
    print("  índice confere (SHA-256).")

    root = ET.fromstring(xml)
    pacote = next((p for p in root.findall("PackageUpdate")
                   if p.findtext("Name") == pkg), None)
    if pacote is None:
        raise SystemExit(f"pacote {pkg} não existe nesse índice")
    versao = pacote.findtext("Version")
    arquivos = [a for a in (pacote.findtext("DownloadableArchives") or "").split(", ")
                if a and any(a.startswith(m) for m in args.modules)]
    print(f"Pacote {pkg} {versao}: {len(arquivos)} arquivo(s) escolhido(s)")

    # Os .7z da Qt já vêm com a raiz do prefixo (bin/, lib/, include/...),
    # sem a pasta "<versão>/<arquitetura>"; ela é criada aqui.
    qt_dir = args.out / args.version / args.arch.removeprefix("win64_")
    qt_dir.mkdir(parents=True, exist_ok=True)

    for nome in arquivos:
        url = f"{repo}/{pkg}/{versao}{nome}"
        sha1 = fetch(url + ".sha1").decode().split()[0]
        print(f"- {nome.split('-')[0]}")
        alvo = cache / nome
        download(url, alvo, sha1)
        print("  extraindo...")
        with py7zr.SevenZipFile(alvo, "r") as z:
            z.extractall(path=qt_dir)

    ok = (qt_dir / "bin" / "Qt6Core.dll").exists()
    print(f"\nQt em: {qt_dir}  ->  {'OK' if ok else 'FALTOU Qt6Core.dll'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
