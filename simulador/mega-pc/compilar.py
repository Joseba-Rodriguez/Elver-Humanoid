"""Compila humanoide-automatico.ino SIN MODIFICARLO para ejecutarlo en el PC.

Hace lo mismo que el IDE de Arduino antes de compilar: añade #include <Arduino.h>
y los prototipos de todas las funciones justo antes de la primera de ellas.
Compilador: Zig (clang), instalado con `pip install ziglang`.

Uso:  python compilar.py
"""
import pathlib
import re
import subprocess
import sys

AQUI = pathlib.Path(__file__).resolve().parent
INO = AQUI.parents[1] / "arduino" / "humanoide-automatico" / "humanoide-automatico.ino"
BUILD = AQUI / "build"
EXE = BUILD / "mega_sim.exe"

# Definición de función a nivel superior: "tipo nombre(args) {" empezando en la
# columna 0 (el tipo puede tener varias palabras y los args varias líneas)
DEF_FUNCION = re.compile(
    r"^(?!struct\b|enum\b|class\b|else\b|if\b|for\b|while\b|switch\b|return\b)"
    r"([A-Za-z_][\w \t]*?[\w\*&])\s*?\b([A-Za-z_]\w*)\s*\(([^;{)]*)\)\s*\{",
    re.MULTILINE,
)


def generar_sketch():
    texto = INO.read_text(encoding="utf-8")
    lineas = texto.splitlines()
    prototipos, primera = [], None
    for m in DEF_FUNCION.finditer(texto):
        args = " ".join(m.group(3).split())
        prototipos.append(f"{m.group(1).strip()} {m.group(2)}({args});")
        if primera is None:
            primera = texto.count("\n", 0, m.start())
    if primera is None:
        sys.exit("No se encontró ninguna función en el sketch")
    salida = (
        ['#include "Arduino.h"', f'#line 1 "{INO.as_posix()}"']
        + lineas[:primera]
        + prototipos
        + [f'#line {primera + 1} "{INO.as_posix()}"']
        + lineas[primera:]
    )
    BUILD.mkdir(exist_ok=True)
    destino = BUILD / "sketch.cpp"
    destino.write_text("\n".join(salida) + "\n", encoding="utf-8")
    return destino, len(prototipos)


def main():
    sketch, n = generar_sketch()
    print(f"Sketch: {INO.name}  ({n} funciones)")
    cmd = [
        sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O1",
        "-w", "-target", "x86_64-windows-gnu",
        "-I", str(AQUI / "mock"),
        str(sketch), str(AQUI / "mock" / "arduino_sim.cpp"),
        "-lwinmm", "-o", str(EXE),
    ]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit("ERROR de compilación")
    print(f"OK -> {EXE}")


if __name__ == "__main__":
    main()
