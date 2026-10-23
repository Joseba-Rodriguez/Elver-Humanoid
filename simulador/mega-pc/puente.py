"""Puente placa XiaoZhi (real) -> Arduino Mega (simulado).

Lanza el Mega simulado (tu sketch compilado para el PC con compilar.py) y le
inyecta por Serial1 los comandos que envía la placa de IA, igual que haría el
cable IO2 -> pin 19. Muestra en vivo los 16 servos y la conversación.

Uso:
    python puente.py                 placa real (busca el puerto USB del ESP32-C3)
    python puente.py --puerto COM6   placa real en un puerto concreto
    python puente.py --teclado       sin placa: escribes los comandos con el teclado
    python puente.py --prueba        prueba automática de todos los comandos
    python puente.py --limites 120   auditoría de límites con comandos al azar

    Añade --visual a los dos primeros para ver a Elver moverse en el navegador
    (http://127.0.0.1:8765), con botones para mandarle órdenes.

Salir: Ctrl+C
"""
import argparse
import collections
import os
import pathlib
import re
import subprocess
import sys
import threading
import time

AQUI = pathlib.Path(__file__).resolve().parent
EXE = AQUI / "build" / "mega_sim.exe"

SERVOS = [
    ("CUELLO", [(36, "Inclinación izquierda"), (37, "Inclinación derecha"), (38, "Giro")]),
    ("OJOS", [(39, "Párpado sup. izq."), (40, "Párpado sup. der."), (41, "Párpado inf. izq."),
              (42, "Párpado inf. der."), (43, "Ojos vertical"), (44, "Ojos horizontal")]),
    ("MANO", [(30, "Pulgar flexión"), (31, "Índice"), (32, "Corazón"), (33, "Anular + meñique"),
              (34, "Pulgar vertical"), (35, "Muñeca"), (45, "Codo")]),
]

COMANDOS = {
    "0": "ánimo Tranquilo", "1": "ánimo Curioso", "2": "ánimo Somnoliento", "3": "ánimo Alerta",
    "4": "ánimo Tímido", "d": "dormir", "a": "despertar", "g": "gesto al azar",
    "h": "saludar", "v": "ven aquí", "s": "silencio", "m": "así así", "t": "tamborilear",
    "C": "cabeza al centro", "I": "inclinar izq.", "D": "inclinar der.",
    "L": "girar izq.", "R": "girar der.", "N": "negar con la cabeza",
    "n": "puño (piedra)", "b": "mano abierta (papel)", "x": "paz (tijera)", "u": "pulgar arriba",
    "w": "señalar", "o": "OK", "r": "mano relajada", "j": "guiño", "q": "quieto", "Q": "fin de quieto",
    "E": "conversación: escuchando", "H": "conversación: hablando", "Z": "conversación: reposo",
}

ANSI = re.compile(r"\x1b\[[0-9;]*m")


class MegaSimulado:
    """Proceso mega_sim.exe: stdin = RX1 del Mega, stdout = servos y Serial."""

    def __init__(self, al_evento):
        if not EXE.exists():
            sys.exit(f"No existe {EXE}. Ejecuta antes: python compilar.py")
        self.angulos = {}
        self.lineas_usb = collections.deque(maxlen=400)
        self.historial = collections.defaultdict(list)    # pin -> [(t, ángulo)]
        self.al_evento = al_evento
        self.proc = subprocess.Popen([str(EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, encoding="utf-8", errors="replace", bufsize=1)
        threading.Thread(target=self._leer, daemon=True).start()

    def _leer(self):
        for linea in self.proc.stdout:
            linea = linea.rstrip("\n")
            if linea.startswith("@S "):
                _, pin, ang = linea.split()
                pin, ang = int(pin), int(ang)
                self.angulos[pin] = ang
                self.historial[pin].append((time.time(), ang))
            elif linea.startswith("[USB] "):
                texto = linea[6:]
                self.lineas_usb.append((time.time(), texto))
                if not texto.startswith(("Comandos:", "  ", "Gestos:", "Cuello:", "Mano:", "Otros:")):
                    self.al_evento("mega", texto)

    def enviar(self, c):
        self.proc.stdin.write(c)
        self.proc.stdin.flush()

    def cerrar(self):
        self.proc.kill()


class Panel:
    def __init__(self, titulo):
        self.titulo = titulo
        self.eventos = collections.deque(maxlen=40)
        self.estado_ia = "-"
        # Estado de Elver deducido de lo que el sketch imprime por Serial
        self.elver = {"conversacion": "Reposo", "animo": "Tranquilo", "dormido": False, "quieto": False}
        self.lock = threading.Lock()

    def evento(self, tipo, texto):
        with self.lock:
            self.eventos.append((time.strftime("%H:%M:%S"), tipo, texto))
            if tipo == "mega":
                if texto.startswith("Animo: "):
                    self.elver["animo"] = texto[7:]
                elif texto.startswith("Conversacion -> "):
                    self.elver["conversacion"] = texto[16:]
                elif texto == "Durmiendo...":
                    self.elver["dormido"] = True
                    self.elver["conversacion"] = "Reposo"
                elif texto == "Despierto.":
                    self.elver["dormido"] = False
                elif texto in ("Modo quieto", "Fin del modo quieto"):
                    self.elver["quieto"] = texto == "Modo quieto"

    def dibujar(self, mega):
        filas = ["\x1b[H\x1b[2J", f"\x1b[1m{self.titulo}\x1b[0m   Placa IA: {self.estado_ia}", ""]
        for grupo, servos in SERVOS:
            filas.append(f"\x1b[1m{grupo}\x1b[0m")
            for pin, nombre in servos:
                ang = mega.angulos.get(pin)
                if ang is None:
                    filas.append(f"  {nombre:<22} pin {pin:<3}   ---")
                    continue
                barra = "█" * round(ang / 180 * 30)
                filas.append(f"  {nombre:<22} pin {pin:<3} {ang:>4}°  {barra:<30}|")
        filas.append("")
        filas.append("\x1b[1mEVENTOS\x1b[0m")
        colores = {"tu": "36", "ia": "35", "cmd": "33", "mega": "32", "info": "90"}
        etiquetas = {"tu": "Tú     ", "ia": "IA     ", "cmd": "→ Mega ", "mega": "Mega   ", "info": "       "}
        with self.lock:
            for hora, tipo, texto in list(self.eventos)[-14:]:
                filas.append(f"  {hora} \x1b[{colores[tipo]}m{etiquetas[tipo]}{texto}\x1b[0m")
        sys.stdout.write("\n".join(filas) + "\n")
        sys.stdout.flush()


def iniciar_visor(mega, panel, puerto_http, abrir):
    """Sirve visor.html y le envía el estado de Elver ~20 veces por segundo."""
    import http.server
    import json
    import webbrowser

    class Manejador(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass                                          # sin ruido en la terminal

        def do_GET(self):
            if self.path == "/":
                cuerpo = (AQUI / "visor.html").read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(cuerpo)))
                self.end_headers()
                self.wfile.write(cuerpo)
            elif self.path == "/estado":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                try:
                    while True:
                        with panel.lock:
                            datos = {"angulos": dict(mega.angulos), "eventos": list(panel.eventos),
                                     "ia": panel.estado_ia, "elver": dict(panel.elver)}
                        self.wfile.write(f"data: {json.dumps(datos)}\n\n".encode("utf-8"))
                        self.wfile.flush()
                        time.sleep(0.05)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass                                  # se cerró la página
            else:
                self.send_error(404)

        def do_POST(self):
            if self.path != "/comando":
                self.send_error(404)
                return
            c = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8")
            if c in COMANDOS:
                panel.evento("cmd", f"'{c}'  ({COMANDOS[c]})  [botón del visor]")
                mega.enviar(c)
                self.send_response(204)
            else:
                self.send_response(400)
            self.end_headers()

    servidor = http.server.ThreadingHTTPServer(("127.0.0.1", puerto_http), Manejador)
    servidor.daemon_threads = True
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{puerto_http}/"
    panel.evento("info", f"Visor en {url}")
    if abrir:
        webbrowser.open(url)


def buscar_puerto_esp32():
    from serial.tools import list_ports
    for p in list_ports.comports():
        if p.vid == 0x303A:          # Espressif (USB JTAG/serial del ESP32-C3)
            return p.device
    return None


def escuchar_placa(puerto, mega, panel):
    """Lee el log USB de la placa XiaoZhi y reenvía al Mega los comandos del robot."""
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = puerto, 115200, 0.5
    s.dtr = False      # que abrir el puerto no reinicie la placa
    s.rts = False
    s.open()
    panel.evento("info", f"Escuchando la placa en {puerto}")
    while True:
        linea = ANSI.sub("", s.readline().decode("utf-8", "replace")).strip()
        if not linea:
            continue
        m = re.search(r"Comando enviado al Mega: (.)", linea)
        if m:
            c = m.group(1)
            panel.evento("cmd", f"'{c}'  ({COMANDOS.get(c, '?')})")
            mega.enviar(c)
        elif m := re.search(r"Application: >> (.*)", linea):
            panel.evento("tu", m.group(1))
        elif m := re.search(r"Application: << (.*)", linea):
            panel.evento("ia", m.group(1))
        elif m := re.search(r"Application: STATE: (\w+)", linea):
            panel.estado_ia = m.group(1)
        elif "tools/call" in linea:
            panel.evento("info", linea)


def leer_teclado(mega, panel):
    import msvcrt
    panel.evento("info", "Pulsa una tecla de comando: " + " ".join(COMANDOS))
    while True:
        try:
            c = msvcrt.getwch()
        except Exception:
            return                                       # sin consola (p. ej. lanzado en segundo plano)
        if c in COMANDOS:
            panel.evento("cmd", f"'{c}'  ({COMANDOS[c]})")
            mega.enviar(c)


def modo_vivo(args):
    os.system("")      # activa los colores ANSI en la consola de Windows
    if args.teclado:
        panel = Panel("Mega simulado — comandos por teclado")
    else:
        puerto = args.puerto or buscar_puerto_esp32()
        if not puerto:
            sys.exit("No encuentro la placa XiaoZhi por USB. Enciéndela (PWR) o usa --teclado.")
        panel = Panel(f"Placa XiaoZhi ({puerto}) → Mega simulado")
    mega = MegaSimulado(panel.evento)
    if args.visual:
        iniciar_visor(mega, panel, args.puerto_visor, not args.no_abrir)
    destino = leer_teclado if args.teclado else escuchar_placa
    destino_args = (mega, panel) if args.teclado else (puerto, mega, panel)
    threading.Thread(target=destino, args=destino_args, daemon=True).start()
    try:
        while True:
            panel.dibujar(mega)
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        mega.cerrar()


# ----------------------------------------------------------------------------
# Prueba automática: envía cada comando y comprueba la respuesta del sketch
# ----------------------------------------------------------------------------

def modo_prueba():
    mega = MegaSimulado(lambda *_: None)
    time.sleep(1.5)
    resultados = []

    def esperar_usb(texto, desde, segundos):
        fin = time.time() + segundos
        while time.time() < fin:
            if any(t >= desde and texto in l for t, l in list(mega.lineas_usb)):
                return True
            time.sleep(0.05)
        return False

    def valores(pin, desde):
        return [a for t, a in mega.historial[pin] if t >= desde]

    def comprobar(nombre, ok, detalle=""):
        resultados.append(ok)
        print(f"  {'OK   ' if ok else 'FALLO'}  {nombre}  {detalle}")

    print("Gestos de la mano")
    gestos = {"h": "Saludar", "v": "Ven aqui", "s": "Silencio", "m": "Asi asi", "t": "Tamborilear"}
    for c, nombre in gestos.items():
        t0 = time.time()
        mega.enviar(c)
        ok = esperar_usb(f"Gesto -> {nombre}", t0, 2)
        time.sleep(4.5)                                  # que termine el gesto
        muneca, indice = valores(35, t0), valores(31, t0)
        detalle = ""
        if c == "h":
            ok = ok and 105 in muneca and 150 in muneca
            detalle = f"(muñeca llega a 105° y 150°: {105 in muneca and 150 in muneca})"
        elif c == "t":
            ok = ok and 45 in indice
            detalle = f"(índice da el golpecito a 45°: {45 in indice})"
        comprobar(f"'{c}' {nombre}", ok, detalle)

    print("Cabeza")
    poses = {"L": ("Girar poco a la izquierda", {38: 55}), "R": ("Girar poco a la derecha", {38: 125}),
             "I": ("Inclinar poco a la izquierda", {36: 115, 37: 78}),
             "D": ("Inclinar poco a la derecha", {36: 78, 37: 115}), "C": ("Centro", {36: 90, 37: 90, 38: 90})}
    for c, (nombre, esperado) in poses.items():
        t0 = time.time()
        mega.enviar(c)
        # puede esperar a que acabe el movimiento automático en curso (hasta 3,4 s)
        ok = esperar_usb(f"Nuevo movimiento de cuello -> {nombre}", t0, 5)
        time.sleep(4)                                    # 150 ms + 85 pasos x 40 ms
        finales = {pin: mega.angulos.get(pin) for pin in esperado}
        comprobar(f"'{c}' {nombre}", ok and finales == esperado, f"(ángulos {finales}, esperado {esperado})")

    print("Ánimos")
    for c, nombre in {"1": "Curioso", "3": "Alerta", "4": "Timido", "2": "Somnoliento", "0": "Tranquilo"}.items():
        t0 = time.time()
        mega.enviar(c)
        comprobar(f"'{c}' {nombre}", esperar_usb(f"Animo: {nombre}", t0, 2))

    print("Dormir y despertar")
    t0 = time.time()
    mega.enviar("d")
    ok = esperar_usb("Durmiendo...", t0, 2)
    time.sleep(2.5)
    parpados = (mega.angulos.get(39), mega.angulos.get(40))
    comprobar("'d' dormir", ok and parpados == (90, 90), f"(párpados superiores {parpados}, cerrados = 90)")
    t0 = time.time()
    mega.enviar("h")                                     # dormido no debe gesticular
    comprobar("gesto ignorado mientras duerme", not esperar_usb("Gesto ->", t0, 1.5))
    t0 = time.time()
    mega.enviar("a")
    comprobar("'a' despertar", esperar_usb("Despierto.", t0, 8))

    def hay_usb(texto, desde, hasta):
        return any(desde <= t <= hasta and texto in l for t, l in list(mega.lineas_usb))

    print("Órdenes de cuello sin esperar")
    mega.enviar("L")
    time.sleep(1.0)                                      # a mitad del giro a la izquierda...
    t0 = time.time()
    mega.enviar("R")                                     # ...pide la derecha
    ok = esperar_usb("Girar poco a la derecha", t0, 0.5)
    time.sleep(0.6)
    giro = [a for t, a in mega.historial[38] if t >= t0]
    comprobar("'R' interrumpe a 'L' al momento", ok and giro and giro[-1] > giro[0],
              f"(el giro ya va hacia la derecha: {giro[:1]} -> {giro[-1:]})")
    time.sleep(4)

    print("Negar con la cabeza")
    mega.enviar("C")
    time.sleep(4.5)
    t0 = time.time()
    mega.enviar("N")
    time.sleep(3.5)
    giro = valores(38, t0)
    amplitud = (min(giro), max(giro)) if giro else None
    comprobar("'N' niega (90 ± 8°)", amplitud == (82, 98) and mega.angulos.get(38) == 90,
              f"(giro entre {amplitud}, termina en {mega.angulos.get(38)}°)")

    print("Poses de mano a la orden")
    poses = {"n": ("Puno cerrado", {31: 5, 32: 5, 33: 5}),       # piedra
             "b": ("Mano abierta", {31: 175, 32: 175, 33: 175}),  # papel
             "x": ("Senal de paz", {31: 175, 32: 175, 33: 5}),     # tijera
             "u": ("Pulgar arriba", {30: 175, 31: 5}),
             "r": ("Mano relajada", {31: 90, 32: 90, 33: 90})}
    for c, (nombre, esperado) in poses.items():
        t0 = time.time()
        mega.enviar(c)
        ok = esperar_usb(f"Pose pedida -> {nombre}", t0, 1)
        time.sleep(2.5)
        finales = {pin: mega.angulos.get(pin) for pin in esperado}
        comprobar(f"'{c}' {nombre}", ok and finales == esperado, f"(dedos {finales})")

    print("Guiño")
    t0 = time.time()
    mega.enviar("j")
    time.sleep(2)
    # Línea de tiempo de los dos párpados superiores: ¿hay un instante con uno
    # cerrado (90°) y el otro abierto? (un parpadeo normal cierra los dos)
    eventos = sorted([(t, 39, a) for t, a in mega.historial[39]] + [(t, 40, a) for t, a in mega.historial[40]])
    estado, guino = {}, False
    for t, pin, a in eventos:
        estado[pin] = a
        if t >= t0 and 39 in estado and 40 in estado:
            izq_cerrado, der_cerrado = estado[39] == 90, estado[40] == 90
            izq_abierto, der_abierto = estado[39] >= 115, estado[40] <= 65
            guino |= (izq_cerrado and der_abierto) or (der_cerrado and izq_abierto)
    comprobar("'j' cierra un solo ojo", esperar_usb("Guino", t0, 0.1) and guino, f"(un ojo cerrado y el otro abierto: {guino})")

    print("Conversación")
    t0 = time.time()
    mega.enviar("E")
    ok = esperar_usb("Conversacion -> Escuchando", t0, 1)
    time.sleep(12)
    t1 = time.time()
    cuello_quieto = len(valores(38, t0 + 4)) == 0 and len(valores(36, t0 + 4)) == 0
    sin_poses = not hay_usb("Nueva pose", t0, t1) and not hay_usb("Gesto ->", t0, t1)
    horiz = valores(44, t0 + 1)
    comprobar("'E' escuchando: cabeza quieta y sin gestos", ok and cuello_quieto and sin_poses,
              f"(cuello quieto: {cuello_quieto}, sin poses/gestos al azar: {sin_poses})")
    comprobar("    ojos al frente", all(83 <= a <= 97 for a in horiz), f"(horizontal {min(horiz, default=None)}-{max(horiz, default=None)}°)")

    t0 = time.time()
    mega.enviar("H")
    ok = esperar_usb("Conversacion -> Hablando", t0, 1)
    time.sleep(15)
    giro, incl = valores(38, t0), valores(36, t0)
    movs = len(giro) + len(incl)
    rango_ok = all(76 <= a <= 104 for a in giro)        # como mucho el 40 % del giro de una pose
    muneca = valores(35, t0)
    comprobar("'H' hablando: la cabeza acompaña con poco recorrido", ok and movs > 0 and rango_ok,
              f"({movs} escrituras, giro entre {min(giro, default=None)}-{max(giro, default=None)}°)")
    comprobar("    la muñeca marca el ritmo", len(set(muneca)) > 3, f"({len(set(muneca))} posiciones distintas)")
    comprobar("    sin poses ni gestos al azar", not hay_usb("Nueva pose", t0, time.time()) and not hay_usb("Gesto ->", t0, time.time()))

    t0 = time.time()
    mega.enviar("Z")
    ok = esperar_usb("Conversacion -> Reposo", t0, 1)
    comprobar("'Z' reposo: vuelve a su vida normal", ok and esperar_usb("Nueva", t0, 15))

    print("Modo quieto")
    mega.enviar("q")
    time.sleep(4)                                        # acaba lo que estuviera haciendo
    t0 = time.time()
    time.sleep(10)
    quieto_ok = not hay_usb("Nuevo movimiento", t0, time.time()) and not hay_usb("Nueva pose", t0, time.time())
    comprobar("'q' quieto: ni cuello ni mano en 10 s", quieto_ok)
    t0 = time.time()
    mega.enviar("h")
    comprobar("    pero obedece órdenes", esperar_usb("Gesto -> Saludar", t0, 1))
    time.sleep(4.5)
    t0 = time.time()
    mega.enviar("Q")
    comprobar("'Q' fin de quieto", esperar_usb("Fin del modo quieto", t0, 1) and esperar_usb("Nuev", t0, 15))

    print("Dormido ignora la conversación")
    mega.enviar("d")
    time.sleep(4)                                        # termina el movimiento de cuello en curso
    t0 = time.time()
    mega.enviar("H")
    mega.enviar("N")
    time.sleep(2)
    comprobar("dormido no habla ni niega", not hay_usb("Conversacion", t0, time.time()) and not valores(38, t0))
    mega.enviar("a")
    esperar_usb("Despierto.", t0, 8)

    mega.cerrar()
    total, bien = len(resultados), sum(resultados)
    print(f"\n{bien}/{total} comprobaciones correctas")
    sys.exit(0 if bien == total else 1)


# ----------------------------------------------------------------------------
# Auditoría de límites: bombardea a Elver con comandos al azar y revisa TODAS
# las escrituras en los servos contra los límites definidos en el sketch.
# ----------------------------------------------------------------------------

def leer_limites():
    """Saca los límites de las constantes del propio sketch (no hay números a mano)."""
    ino = (AQUI.parents[1] / "arduino" / "humanoide-automatico" / "humanoide-automatico.ino").read_text(encoding="utf-8")
    c = {}
    for nombre, expr in re.findall(r"^const\s+(?:int|uint8_t|uint16_t)\s+([A-Z_]+)\s*=\s*([^;]+);", ino, re.M):
        try:
            c[nombre] = int(eval(expr.split("//")[0].replace("/", "//"), {}, c))
        except Exception:
            pass
    rango = lambda a, b: (min(a, b), max(a, b))
    inclinaciones = [c["NEUTRO"], c["INCLINACION_IZQ_ACTIVA"], c["INCLINACION_IZQ_RELAJADA"],
                     c["INCLINACION_DER_ACTIVA"], c["INCLINACION_DER_RELAJADA"]]
    return {
        # Cuello: no tiene LIM_*; su rango es el de sus poses
        36: (min(inclinaciones), max(inclinaciones)),
        37: (min(inclinaciones), max(inclinaciones)),
        38: (c["ROTACION_IZQUIERDA"], c["ROTACION_DERECHA"]),
        39: rango(c["PARPADO_SUP_IZQ_ABIERTO"], c["PARPADO_SUP_IZQ_CERRADO"]),
        40: rango(c["PARPADO_SUP_DER_ABIERTO"], c["PARPADO_SUP_DER_CERRADO"]),
        41: rango(c["PARPADO_INF_IZQ_ABIERTO"], c["PARPADO_INF_IZQ_CERRADO"]),
        42: rango(c["PARPADO_INF_DER_ABIERTO"], c["PARPADO_INF_DER_CERRADO"]),
        43: (c["OJOS_VERTICAL_ARRIBA"], c["OJOS_VERTICAL_ABAJO"]),
        44: (c["OJOS_HORIZONTAL_IZQUIERDA"], c["OJOS_HORIZONTAL_DERECHA"]),
        30: (c["LIM_MIN_PULGAR_FLEX"], c["LIM_MAX_PULGAR_FLEX"]),
        31: (c["LIM_MIN_INDICE"], c["LIM_MAX_INDICE"]),
        32: (c["LIM_MIN_CORAZON"], c["LIM_MAX_CORAZON"]),
        33: (c["LIM_MIN_ANULAR_MENIQUE"], c["LIM_MAX_ANULAR_MENIQUE"]),
        34: (c["LIM_MIN_PULGAR_VERTICAL"], c["LIM_MAX_PULGAR_VERTICAL"]),
        35: (c["LIM_MIN_MUNECA"], c["LIM_MAX_MUNECA"]),
        45: (c["LIM_MIN_CODO"], c["LIM_MAX_CODO"]),
    }, c


def modo_limites(segundos):
    import random
    limites, c = leer_limites()
    # Velocidad máxima del cuello: su movimiento de siempre más rápido
    # (el giro completo de una pose a otra en 85 pasos de 40 ms)
    vmax_cuello = (c["ROTACION_DERECHA"] - c["ROTACION_IZQUIERDA"]) / (c["PASOS_MOVIMIENTO_SUAVE_CUELLO"] * c["RETARDO_PASO_MS_CUELLO"] / 1000)
    print("Límites leídos del sketch:")
    for grupo, servos in SERVOS:
        print("  " + ", ".join(f"{n} {limites[p][0]}-{limites[p][1]}°" for p, n in servos))
    print(f"  Velocidad máx. del cuello (la de siempre): {vmax_cuello:.1f}°/s")

    mega = MegaSimulado(lambda *_: None)
    teclas = [k for k in COMANDOS] + [k for k in "EHZNjqQnbxuwor"]
    fin = time.time() + segundos
    enviados = 0
    print(f"\nEnviando comandos al azar durante {segundos} s...")
    while time.time() < fin:
        mega.enviar(random.choice(teclas))
        enviados += 1
        time.sleep(random.choice([0.05, 0.2, 0.5, 1, 2, 3]))
    time.sleep(1)
    mega.cerrar()

    errores = 0
    for grupo, servos in SERVOS:
        for pin, nombre in servos:
            muestras = mega.historial[pin]
            lo, hi = limites[pin]
            fuera = [a for _, a in muestras if not lo <= a <= hi]
            vistos = (min(a for _, a in muestras), max(a for _, a in muestras)) if muestras else None
            linea = f"  {nombre:<22} {len(muestras):>6} escrituras  rango usado {vistos}  límite ({lo}, {hi})"
            if fuera:
                errores += 1
                linea += f"  FUERA DE LÍMITES: {sorted(set(fuera))}"
            if pin in (36, 37, 38):
                # ventana de 400 ms: margen de 1 paso por el redondeo de map()
                peor = 0.0
                for i, (t, a) in enumerate(muestras):
                    for t2, a2 in muestras[i + 1:]:
                        if t2 - t > 0.4:
                            break
                        peor = max(peor, abs(a2 - a) - 1)
                vel = peor / 0.4
                linea += f"  vel. máx {vel:.1f}°/s"
                if vel > vmax_cuello * 1.15:
                    errores += 1
                    linea += "  DEMASIADO RÁPIDO"
            print(linea)
    print(f"\n{enviados} comandos enviados. "
          + ("TODO DENTRO DE LÍMITES" if errores == 0 else f"{errores} PROBLEMAS"))
    sys.exit(0 if errores == 0 else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--puerto", help="puerto COM de la placa XiaoZhi (por defecto se busca solo)")
    ap.add_argument("--teclado", action="store_true", help="sin placa: comandos por teclado")
    ap.add_argument("--prueba", action="store_true", help="prueba automática de todos los comandos")
    ap.add_argument("--limites", type=int, metavar="SEG", help="auditoría de límites con comandos al azar")
    ap.add_argument("--visual", action="store_true", help="abre el visor de Elver en el navegador")
    ap.add_argument("--puerto-visor", type=int, default=8765, help="puerto local del visor (8765)")
    ap.add_argument("--no-abrir", action="store_true", help="con --visual, no abrir el navegador")
    args = ap.parse_args()
    if args.prueba:
        modo_prueba()
    elif args.limites:
        modo_limites(args.limites)
    else:
        modo_vivo(args)


if __name__ == "__main__":
    main()
