"""Pruebas de lógica y configuración con cJSON real, sin ESP-IDF ni placa.

Python 3.11+, compilador C (cc/gcc/clang) o --cc "zig cc".
La primera ejecución descarga dos fuentes oficiales de cJSON 1.7.19 y verifica SHA256.
No instala paquetes ni modifica la configuración del sistema.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import urllib.request

RAIZ = Path(__file__).resolve().parent
HASHES = {
    "cJSON.c": "298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d",
    "cJSON.h": "25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    deps = RAIZ / ".deps"
    deps.mkdir(exist_ok=True)
    for nombre, esperado in HASHES.items():
        destino = deps / nombre
        if not destino.exists():
            url = f"https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/{nombre}"
            destino.write_bytes(urllib.request.urlopen(url, timeout=60).read())
        if hashlib.sha256(destino.read_bytes()).hexdigest() != esperado:
            raise SystemExit(f"SHA256 incorrecto: {destino}; revisar la descarga antes de repetir")
    build = RAIZ / "build"
    build.mkdir(exist_ok=True)
    ejecutable = build / ("test_hfr.exe" if os.name == "nt" else "test_hfr")
    compilador = shlex.split(args.cc, posix=True)
    if not shutil.which(compilador[0]) and not Path(compilador[0]).is_file():
        raise SystemExit("Falta un compilador C. Usar --cc gcc, --cc clang o --cc 'zig cc'.")
    fuentes = [RAIZ / "test_hfr.c", RAIZ.parent / "main/logica.c",
               RAIZ.parent / "main/config_parser.c", RAIZ.parent / "main/configuracion.c",
               RAIZ.parent / "main/almacenamiento.c", RAIZ.parent / "main/energia.c", deps / "cJSON.c"]
    protocolo = RAIZ.parents[2] / "Mesh_Red_Completa/componentes/aura_mesh/include"
    comando = compilador + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                            "-DHFR_PIN_ALIMENTACION=1", "-I" + str(deps), "-I" + str(RAIZ / "dobles"),
                            "-I" + str(RAIZ.parent / "main"), "-I" + str(protocolo)]
    comando += [str(f) for f in fuentes] + ["-lm", "-o", str(ejecutable)]
    subprocess.run(comando, check=True)
    subprocess.run([str(ejecutable)], check=True)


if __name__ == "__main__":
    main()
