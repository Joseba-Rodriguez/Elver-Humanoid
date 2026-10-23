# 🎮 Simuladores de Elver

Dos formas de ver a Elver moverse sin tener el robot delante.

## 1. `mega-pc/`: el sketch real, en tu PC

[`compilar.py`](mega-pc/compilar.py) hace lo mismo que el IDE de Arduino (añade `Arduino.h` y los prototipos), pero compila [`humanoide-automatico.ino`](../arduino/humanoide-automatico/humanoide-automatico.ino) **sin modificarlo** para Windows, con unas versiones simuladas de `Arduino.h` y `Servo.h` ([`mock/`](mega-pc/mock)). Lo que ves es exactamente lo que haría el Mega.

```bash
pip install ziglang pyserial     # el compilador (Zig/clang) llega por pip, sin instalar nada más
python compilar.py
```

| Comando | Qué hace |
|---|---|
| `python puente.py --teclado --visual` | Elver en el navegador (http://127.0.0.1:8765), con botones y teclado |
| `python puente.py --visual` | Escucha la placa XiaoZhi real por USB y reenvía sus órdenes al Mega simulado |
| `python puente.py --puerto COM6` | Lo mismo, indicando el puerto |
| `python puente.py --prueba` | Prueba automática de todos los comandos (36 comprobaciones) |
| `python puente.py --limites 120` | Auditoría: comandos al azar durante 120 s, revisando cada escritura en los servos contra los límites del propio sketch |

> La prueba usa tiempos reales: con el PC muy cargado puede fallar alguna comprobación de tiempos. Repítela con el PC más libre.

## 2. `simulador_humanoide.py`: Tkinter con deslizadores

Reimplementa en Python las poses y los modos de los sketches, con un deslizador por servo y un monitor serie. Solo necesita Python 3, que ya trae Tkinter.

```bash
python simulador_humanoide.py
```
