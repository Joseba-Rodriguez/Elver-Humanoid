/*
  cuello-automatico.ino
  Control automático del cuello (humanoide 3D) - Arduino Mega 2560.
  3 servos que mueven la cabeza. La cabeza pesa, así que todos los movimientos
  son LENTOS y de RECORRIDO PEQUEÑO para no forzar los servos ni el mecanismo.

  Tabla de conexionado:
  Motor -> Pin digital -> Función
  7     -> 36           -> Inclinación de cuello hacia la izquierda
  8     -> 37           -> Inclinación de cuello hacia la derecha
  9     -> 38           -> Giro rotatorio del cuello (izquierda/derecha)

  CONVENCIÓN DE ÁNGULOS (0-180°, servos estándar):
  Para evitar sobrecargar la cabeza, esta primera versión NO usa el rango 0-180 completo:
  se mueve poco alrededor de una posición neutra (90°). Motor 7 y motor 8 son antagonistas
  (tiran cada uno hacia un lado), así que normalmente cuando uno se activa un poco, el otro
  se relaja un poco, en vez de moverlos a fondo.

  IMPORTANTE:
  - No sabemos todavía cómo se comportará mecánicamente el cuello con el peso real de la
    cabeza, así que estos rangos son intencionadamente conservadores (movimiento pequeño).
  - Ajusta AMPLITUD_INCLINACION y AMPLITUD_ROTACION poco a poco, probando primero con
    valores bajos, y solo auméntalos si el mecanismo lo soporta sin esfuerzo.
  - Si en algún momento el servo fuerza o vibra, BAJA la amplitud o sube el tiempo de
    movimiento; nunca fuerces el mecanismo contra un tope.
*/

#include <Servo.h>

// ---------- Pines ----------
const uint8_t PIN_INCLINACION_IZQUIERDA = 36;
const uint8_t PIN_INCLINACION_DERECHA   = 37;
const uint8_t PIN_ROTACION              = 38;

// ---------- Objetos Servo ----------
Servo servoInclinacionIzquierda;
Servo servoInclinacionDerecha;
Servo servoRotacion;

// ---------- Posición neutra y amplitud de movimiento ----------
const int NEUTRO = 90;

// Cuánto se separa cada servo de la posición neutra al moverse (grados).
// Valores bajos a propósito: "que lo mueva poco". Sube esto solo tras probar en vacío.
const int AMPLITUD_INCLINACION = 25; // ej: 90 -> 102 (activo) / 90 -> 78 (relajado)
const int AMPLITUD_ROTACION    = 35; // ej: 90 -> 105 (derecha) / 90 -> 75 (izquierda)

const int INCLINACION_IZQ_ACTIVA  = NEUTRO + AMPLITUD_INCLINACION;
const int INCLINACION_IZQ_RELAJADA = NEUTRO - (AMPLITUD_INCLINACION / 2);
const int INCLINACION_DER_ACTIVA  = NEUTRO + AMPLITUD_INCLINACION;
const int INCLINACION_DER_RELAJADA = NEUTRO - (AMPLITUD_INCLINACION / 2);

const int ROTACION_IZQUIERDA = NEUTRO - AMPLITUD_ROTACION;
const int ROTACION_DERECHA   = NEUTRO + AMPLITUD_ROTACION;

// ---------- Definición de una pose de cuello ----------
struct PoseCuello {
  const char* nombre;
  int inclinacionIzquierda;
  int inclinacionDerecha;
  int rotacion;
};

// Movimientos suaves y pequeños de la cabeza. Añade más poses cuando compruebes que el
// mecanismo responde bien a estas.
const PoseCuello POSES[] = {
  { "Centro",                    NEUTRO,                 NEUTRO,                 NEUTRO           },
  { "Inclinar poco a la izquierda", INCLINACION_IZQ_ACTIVA, INCLINACION_DER_RELAJADA, NEUTRO           },
  { "Inclinar poco a la derecha",   INCLINACION_IZQ_RELAJADA, INCLINACION_DER_ACTIVA, NEUTRO           },
  { "Girar poco a la izquierda",    NEUTRO,                 NEUTRO,                 ROTACION_IZQUIERDA },
  { "Girar poco a la derecha",      NEUTRO,                 NEUTRO,                 ROTACION_DERECHA   },
};
const uint8_t NUM_POSES = sizeof(POSES) / sizeof(POSES[0]);

// ---------- Temporización (todo LENTO por el peso de la cabeza) ----------
const uint16_t TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS = 3000;
const uint16_t TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS = 7000;
const uint16_t PASOS_MOVIMIENTO_SUAVE = 85; // muchos pasos pequeños = movimiento muy suave
const uint8_t  RETARDO_PASO_MS = 40;         // retardo alto entre pasos = movimiento lento

// ---------- Estado ----------
PoseCuello poseActual = POSES[0];
unsigned long proximoMovimiento = 0;
uint8_t indicePoseAnterior = 0;

void setup() {
  Serial.begin(9600);

  servoInclinacionIzquierda.attach(PIN_INCLINACION_IZQUIERDA);
  servoInclinacionDerecha.attach(PIN_INCLINACION_DERECHA);
  servoRotacion.attach(PIN_ROTACION);

  randomSeed(analogRead(A0));

  Serial.println(F("=== Cuello automatico iniciado (movimiento lento y pequeno) ==="));
  aplicarPoseInstantanea(POSES[0]);
  poseActual = POSES[0];
  imprimirPose(POSES[0]);

  proximoMovimiento = millis() + random(TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS, TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS);
}

void loop() {
  if (millis() >= proximoMovimiento) {
    uint8_t indiceNuevaPose = elegirPoseDistinta(indicePoseAnterior);
    Serial.print(F("Nuevo movimiento de cuello -> "));
    Serial.println(POSES[indiceNuevaPose].nombre);

    moverAPoseSuave(POSES[indiceNuevaPose]);
    poseActual = POSES[indiceNuevaPose];
    indicePoseAnterior = indiceNuevaPose;

    imprimirPose(POSES[indiceNuevaPose]);

    proximoMovimiento = millis() + random(TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS, TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS);
  }
}

// Imprime por el Monitor Serie el nombre de la pose y el angulo aplicado a cada servo
void imprimirPose(const PoseCuello &pose) {
  Serial.print(F("Pose de cuello aplicada: "));
  Serial.println(pose.nombre);
  Serial.print(F("  Inclinacion izquierda: ")); Serial.println(pose.inclinacionIzquierda);
  Serial.print(F("  Inclinacion derecha: ")); Serial.println(pose.inclinacionDerecha);
  Serial.print(F("  Rotacion: ")); Serial.println(pose.rotacion);
  Serial.println(F("----------------------------------"));
}

// Elige un índice de pose aleatorio distinto al anterior, para no repetir la misma dos veces seguidas
uint8_t elegirPoseDistinta(uint8_t indiceAnterior) {
  uint8_t indiceNuevo;
  do {
    indiceNuevo = random(0, NUM_POSES);
  } while (indiceNuevo == indiceAnterior && NUM_POSES > 1);
  return indiceNuevo;
}

// Aplica una pose directamente, sin interpolación (uso solo en el arranque, ya en posicion neutra)
void aplicarPoseInstantanea(const PoseCuello &pose) {
  servoInclinacionIzquierda.write(pose.inclinacionIzquierda);
  servoInclinacionDerecha.write(pose.inclinacionDerecha);
  servoRotacion.write(pose.rotacion);
}

// Interpola los 3 servos del cuello desde la pose actual hasta la pose destino,
// con muchos pasos pequeños y retardo alto para que el movimiento sea muy lento y suave
void moverAPoseSuave(const PoseCuello &destino) {
  PoseCuello origen = poseActual;

  for (uint16_t paso = 1; paso <= PASOS_MOVIMIENTO_SUAVE; paso++) {
    servoInclinacionIzquierda.write(map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.inclinacionIzquierda, destino.inclinacionIzquierda));
    servoInclinacionDerecha.write(map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.inclinacionDerecha, destino.inclinacionDerecha));
    servoRotacion.write(map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.rotacion, destino.rotacion));
    delay(RETARDO_PASO_MS);
  }
}
