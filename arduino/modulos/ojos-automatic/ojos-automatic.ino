/*
  ojos-automatic.ino
  Control automático de ojos animatrónicos (humanoide 3D) - Arduino Mega 2560
  6 servos: 2 párpados superiores, 2 párpados inferiores, 1 eje vertical (arriba/abajo),
  1 eje horizontal (izquierda/derecha).

  Tabla de conexionado:
  Servo -> Pin digital -> Función
  10    -> 39           -> Párpado arriba izquierdo
  11    -> 40           -> Párpado arriba derecho
  12    -> 41           -> Párpado abajo izquierdo
  13    -> 42           -> Párpado abajo derecho
  14    -> 43           -> Movimiento vertical de ojos (ambos)
  15    -> 44           -> Movimiento horizontal de ojos (ambos)

  IMPORTANTE: Los ángulos definidos abajo (ABIERTO/CERRADO, CENTRO/LIMITES) son valores
  de partida. Ajusta las constantes según el rango físico real de tus servos y mecanismos
  antes de dejarlo funcionando sin supervisión.

  NOTA: los 4 párpados estaban montados/girados al revés de lo que marcaban las constantes
  (lo que el código llamaba "abierto" se veía físicamente cerrado, y viceversa). Se han
  intercambiado los valores de ABIERTO/CERRADO de los 4 servos de párpados para que
  coincidan con lo que se ve en el robot real.
*/

#include <Servo.h>

// ---------- Pines ----------
const uint8_t PIN_PARPADO_SUP_IZQ = 39;
const uint8_t PIN_PARPADO_SUP_DER = 40;
const uint8_t PIN_PARPADO_INF_IZQ = 41;
const uint8_t PIN_PARPADO_INF_DER = 42;
const uint8_t PIN_OJOS_VERTICAL   = 43;
const uint8_t PIN_OJOS_HORIZONTAL = 44;

// ---------- Objetos Servo ----------
Servo servoParpadoSupIzq;
Servo servoParpadoSupDer;
Servo servoParpadoInfIzq;
Servo servoParpadoInfDer;
Servo servoOjosVertical;
Servo servoOjosHorizontal;

// ---------- Ángulos de los párpados (AJUSTAR según montaje) ----------
// Valores intercambiados respecto al original: el giro físico de estos 4 servos
// estaba invertido, así que ahora ABIERTO/CERRADO se corresponden con lo que se ve.
const int PARPADO_SUP_IZQ_ABIERTO = 130;
const int PARPADO_SUP_IZQ_CERRADO = 90;
const int PARPADO_SUP_DER_ABIERTO = 50;
const int PARPADO_SUP_DER_CERRADO = 90;

const int PARPADO_INF_IZQ_ABIERTO = 50;
const int PARPADO_INF_IZQ_CERRADO = 90;
const int PARPADO_INF_DER_ABIERTO = 130;
const int PARPADO_INF_DER_CERRADO = 90;

// ---------- Ángulos de movimiento ocular (AJUSTAR según montaje) ----------
const int OJOS_VERTICAL_CENTRO   = 90;
const int OJOS_VERTICAL_ARRIBA   = 70;
const int OJOS_VERTICAL_ABAJO    = 110;

const int OJOS_HORIZONTAL_CENTRO   = 90;
const int OJOS_HORIZONTAL_IZQUIERDA = 70;
const int OJOS_HORIZONTAL_DERECHA   = 100;

// Párpados "entornados" (a medio cerrar), para la mirada de sospecha/enfado.
// Se calculan como punto medio entre abierto y cerrado, no hace falta tocarlos.
const int PARPADO_SUP_IZQ_ENTORNADO = (PARPADO_SUP_IZQ_ABIERTO + PARPADO_SUP_IZQ_CERRADO) / 2;
const int PARPADO_SUP_DER_ENTORNADO = (PARPADO_SUP_DER_ABIERTO + PARPADO_SUP_DER_CERRADO) / 2;
const int PARPADO_INF_IZQ_ENTORNADO = (PARPADO_INF_IZQ_ABIERTO + PARPADO_INF_IZQ_CERRADO) / 2;
const int PARPADO_INF_DER_ENTORNADO = (PARPADO_INF_DER_ABIERTO + PARPADO_INF_DER_CERRADO) / 2;

// ---------- Temporización ----------
const uint16_t PARPADEO_MIN_MS = 2000;   // tiempo mínimo entre parpadeos
const uint16_t PARPADEO_MAX_MS = 6000;   // tiempo máximo entre parpadeos
const uint16_t PARPADEO_DURACION_MS = 120; // duración de cerrado/abierto de párpado
const uint8_t  PROBABILIDAD_DOBLE_PARPADEO = 20; // % de veces que parpadea dos veces seguidas

const uint16_t MIRADA_MIN_MS = 1500;  // tiempo mínimo entre cambios de mirada
const uint16_t MIRADA_MAX_MS = 4500;  // tiempo máximo entre cambios de mirada
const uint8_t  PASOS_MOVIMIENTO_SUAVE = 20; // nº de pasos para interpolar el movimiento normal
const uint8_t  RETARDO_PASO_MS = 15;        // retardo entre pasos del movimiento normal

// Movimiento "snap" robótico: muy pocos pasos y muy rápido, como un servo digital brusco
const uint8_t PASOS_SNAP = 3;
const uint8_t RETARDO_SNAP_MS = 4;

// Barrido tipo radar/escáner: muchos pasos y retardo alto para que se vea muy deliberado
const uint8_t  PASOS_ESCANEO = 40;
const uint8_t  RETARDO_ESCANEO_MS = 12;

// ---------- Estado ----------
unsigned long proximoParpadeo = 0;
unsigned long proximaMirada = 0;
int verticalActual = OJOS_VERTICAL_CENTRO;
int horizontalActual = OJOS_HORIZONTAL_CENTRO;

void setup() {
  Serial.begin(9600);

  servoParpadoSupIzq.attach(PIN_PARPADO_SUP_IZQ);
  servoParpadoSupDer.attach(PIN_PARPADO_SUP_DER);
  servoParpadoInfIzq.attach(PIN_PARPADO_INF_IZQ);
  servoParpadoInfDer.attach(PIN_PARPADO_INF_DER);
  servoOjosVertical.attach(PIN_OJOS_VERTICAL);
  servoOjosHorizontal.attach(PIN_OJOS_HORIZONTAL);

  randomSeed(analogRead(A0));

  Serial.println(F("=== Ojos automaticos iniciados (modo robotico/exagerado) ==="));

  // Posición inicial: ojos abiertos y mirando al centro
  abrirParpados();
  servoOjosVertical.write(OJOS_VERTICAL_CENTRO);
  servoOjosHorizontal.write(OJOS_HORIZONTAL_CENTRO);
  verticalActual = OJOS_VERTICAL_CENTRO;
  horizontalActual = OJOS_HORIZONTAL_CENTRO;

  proximoParpadeo = millis() + random(PARPADEO_MIN_MS, PARPADEO_MAX_MS);
  proximaMirada = millis() + random(MIRADA_MIN_MS, MIRADA_MAX_MS);
}

void loop() {
  unsigned long ahora = millis();

  if (ahora >= proximoParpadeo) {
    parpadear();
    if (random(100) < PROBABILIDAD_DOBLE_PARPADEO) {
      delay(150);
      parpadear();
    }
    proximoParpadeo = millis() + random(PARPADEO_MIN_MS, PARPADEO_MAX_MS);
  }

  if (ahora >= proximaMirada) {
    moverMiradaAleatoria();
    proximaMirada = millis() + random(MIRADA_MIN_MS, MIRADA_MAX_MS);
  }
}

// Cierra y vuelve a abrir los cuatro párpados de forma sincronizada
void parpadear() {
  cerrarParpados();
  delay(PARPADEO_DURACION_MS);
  abrirParpados();
}

void cerrarParpados() {
  servoParpadoSupIzq.write(PARPADO_SUP_IZQ_CERRADO);
  servoParpadoSupDer.write(PARPADO_SUP_DER_CERRADO);
  servoParpadoInfIzq.write(PARPADO_INF_IZQ_CERRADO);
  servoParpadoInfDer.write(PARPADO_INF_DER_CERRADO);
}

void abrirParpados() {
  servoParpadoSupIzq.write(PARPADO_SUP_IZQ_ABIERTO);
  servoParpadoSupDer.write(PARPADO_SUP_DER_ABIERTO);
  servoParpadoInfIzq.write(PARPADO_INF_IZQ_ABIERTO);
  servoParpadoInfDer.write(PARPADO_INF_DER_ABIERTO);
}

// Entorna los párpados a medio camino (mirada de sospecha/enfado)
void entornarParpados() {
  servoParpadoSupIzq.write(PARPADO_SUP_IZQ_ENTORNADO);
  servoParpadoSupDer.write(PARPADO_SUP_DER_ENTORNADO);
  servoParpadoInfIzq.write(PARPADO_INF_IZQ_ENTORNADO);
  servoParpadoInfDer.write(PARPADO_INF_DER_ENTORNADO);
}

// Elige al azar uno de varios "comportamientos" de mirada, para que parezca más
// robótico e imprevisible: movimiento normal, snap brusco, barrido tipo radar,
// mirada de sospecha (entornada) o sorpresa (ojos muy abiertos + parpadeo rápido).
void moverMiradaAleatoria() {
  int azar = random(100);

  if (azar < 40) {
    Serial.println(F("Ojos: movimiento normal"));
    int v = random(OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO + 1);
    int h = random(OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA + 1);
    moverMiradaSuave(v, h, PASOS_MOVIMIENTO_SUAVE, RETARDO_PASO_MS);
  } else if (azar < 65) {
    Serial.println(F("Ojos: snap robotico"));
    miradaSnap();
  } else if (azar < 82) {
    Serial.println(F("Ojos: barrido tipo radar"));
    miradaEscaneo();
  } else if (azar < 93) {
    Serial.println(F("Ojos: mirada de sospecha"));
    miradaSospecha();
  } else {
    Serial.println(F("Ojos: sorpresa!"));
    miradaSorpresa();
  }
}

// Movimiento brusco e instantáneo, como un servo digital sin suavizado: efecto muy robótico
void miradaSnap() {
  int v = random(OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO + 1);
  int h = random(OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA + 1);
  moverMiradaSuave(v, h, PASOS_SNAP, RETARDO_SNAP_MS);
}

// Barrido lento de extremo a extremo, como un escáner/radar: muy vistoso y deliberado
void miradaEscaneo() {
  moverMiradaSuave(OJOS_VERTICAL_CENTRO, OJOS_HORIZONTAL_IZQUIERDA, PASOS_ESCANEO, RETARDO_ESCANEO_MS);
  moverMiradaSuave(OJOS_VERTICAL_CENTRO, OJOS_HORIZONTAL_DERECHA, PASOS_ESCANEO * 2, RETARDO_ESCANEO_MS);
  moverMiradaSuave(OJOS_VERTICAL_CENTRO, OJOS_HORIZONTAL_CENTRO, PASOS_ESCANEO, RETARDO_ESCANEO_MS);
}

// Mira de reojo a un lado con los párpados entornados y se mantiene así un momento
void miradaSospecha() {
  int h = (random(2) == 0) ? OJOS_HORIZONTAL_IZQUIERDA : OJOS_HORIZONTAL_DERECHA;
  moverMiradaSuave(OJOS_VERTICAL_CENTRO, h, PASOS_MOVIMIENTO_SUAVE, RETARDO_PASO_MS);
  entornarParpados();
  delay(900);
  abrirParpados();
}

// Abre bien los ojos, mira hacia arriba de golpe y da un parpadeo rápido triple: efecto "susto"
void miradaSorpresa() {
  abrirParpados();
  moverMiradaSuave(OJOS_VERTICAL_ARRIBA, OJOS_HORIZONTAL_CENTRO, PASOS_SNAP, RETARDO_SNAP_MS);
  for (uint8_t i = 0; i < 3; i++) {
    parpadear();
    delay(90);
  }
}

// Interpola el movimiento de los dos servos oculares con el número de pasos y la
// velocidad indicados, para poder reutilizar la misma función en todos los estilos
// de movimiento (normal, snap, escaneo, etc.)
void moverMiradaSuave(int destinoVertical, int destinoHorizontal, uint8_t pasos, uint8_t retardoPorPaso) {
  int origenVertical = verticalActual;
  int origenHorizontal = horizontalActual;

  for (uint8_t paso = 1; paso <= pasos; paso++) {
    int v = map(paso, 0, pasos, origenVertical, destinoVertical);
    int h = map(paso, 0, pasos, origenHorizontal, destinoHorizontal);
    servoOjosVertical.write(v);
    servoOjosHorizontal.write(h);
    delay(retardoPorPaso);
  }

  verticalActual = destinoVertical;
  horizontalActual = destinoHorizontal;
}