"""Genera muestras de la voz robótica de Elver para elegir la intensidad.

Aplica el MISMO algoritmo (en enteros, igual que en el ESP32-C3) que voz_robot.h
a una frase dicha por la voz de Windows "Microsoft Pablo" (hombre, español de España):
  1. Filtro de peine con realimentación: timbre metálico de "lata".
  2. Modulación en anillo: el clásico efecto de voz de robot.

La voz sale a 16 kHz (como la que manda el servidor de xiaozhi) y se pasa a
24 kHz (como hace la placa antes del altavoz).

Uso: python generar_muestras.py   (crea los .wav en esta carpeta)
"""
import array
import math
import pathlib
import subprocess
import wave

AQUI = pathlib.Path(__file__).resolve().parent
FS = 24000                       # frecuencia de salida del altavoz de la placa
VOZ = "Microsoft Pablo"
FRASE = ("Hola, soy Elver, un robot humanoide creado por José Manuel y Joseba. "
         "¿Jugamos a piedra, papel o tijera?")

# (retardo ms, realimentación, frecuencia Hz, profundidad, ganancia x256).
# Elegido: "fuerte A" con ganancia 1,6 (410/256) -> es lo que lleva voz_robot.h
AJUSTES = {
    "elver-fuerte-A": (8, 0.55, 70, 0.80, 410),
}


def generar_original():
    temporal = AQUI / "_voz16k.wav"
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(AQUI / "voz_windows.ps1"), "-Voz", VOZ, "-Texto", FRASE,
                    "-Salida", str(temporal)], check=True)
    with wave.open(str(temporal)) as w:
        fs_origen = w.getframerate()
        muestras = array.array("h", w.readframes(w.getnframes()))
    temporal.unlink()
    # Interpolación lineal a 24 kHz
    n = len(muestras) * FS // fs_origen
    salida = array.array("h")
    for i in range(n):
        pos = i * fs_origen / FS
        j = int(pos)
        a = muestras[j]
        b = muestras[min(j + 1, len(muestras) - 1)]
        salida.append(int(a + (b - a) * (pos - j)))
    return salida


def voz_robot(muestras, retardo_ms, realim, freq, prof, ganancia):
    """Copia exacta (en Python) del algoritmo entero de voz_robot.h."""
    seno = [round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(256)]
    d = FS * retardo_ms // 1000
    buf = [0] * d
    idx = 0
    g_realim = int(realim * 32768)
    g_entrada = int(32768 / (1 + realim))       # evita saturar al sumar la realimentación
    m = int(prof * 32768)
    fase = 0
    inc = int(freq * (1 << 32) / FS)
    salida = array.array("h")
    for x in muestras:
        y = (x * g_entrada + buf[idx] * g_realim) >> 15
        y = max(-32768, min(32767, y))
        buf[idx] = y
        idx = (idx + 1) % d
        s = seno[(fase >> 24) & 0xFF]
        fase = (fase + inc) & 0xFFFFFFFF
        mod = (32768 - m) + ((m * s) >> 15)
        z = max(-32768, min(32767, (y * mod) >> 15))
        salida.append(max(-32768, min(32767, (z * ganancia) >> 8)))   # recupera volumen
    return salida


def guardar(nombre, muestras):
    with wave.open(str(AQUI / f"{nombre}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(muestras.tobytes())
    pico = max(abs(x) for x in muestras)
    print(f"creado {nombre}.wav  (pico {pico})")


def main():
    for viejo in AQUI.glob("*.wav"):
        viejo.unlink()
    original = generar_original()
    guardar("0-original", original)
    for nombre, ajustes in AJUSTES.items():
        guardar(nombre, voz_robot(original, *ajustes))


if __name__ == "__main__":
    main()
