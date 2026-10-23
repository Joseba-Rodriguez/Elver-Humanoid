/*
  mano-automatica.ino
  Control automático de la mano animatrónica (humanoide 3D) - Arduino Mega 2560.
  5 servos de dedos que van formando poses ("formas") de la mano de manera
  automática y cíclica, con movimiento suave entre cada pose.

  Tabla de conexionado:
  Motor -> Pin digital -> Función
  1     -> 30           -> Pulgar, flexión (recto/extendido <-> flexionado hacia la palma)
  2     -> 31            -> Índice
  3     -> 32            -> Corazón
  4     -> 33            -> Anular + meñique (juntos, un solo servo mueve ambos dedos)
  5     -> 34            -> Pulgar, eje vertical / oposición (debe ir sincronizado con el motor 1)
  6     -> 35            -> Muñeca, giro izquierda/derecha
  16    -> 45            -> Codo, movimiento arriba/abajo

  CONVENCIÓN DE ÁNGULOS (0-180°, igual que en mano-flex/mano-flex.ino):
  - Dedos (índice, corazón, anular+meñique): 0° = dedo cerrado/flexionado, 180° = dedo abierto/recto.
  - Pulgar flexión (motor 1): 0° = pulgar flexionado hacia la palma, 180° = pulgar recto/extendido.
  - Pulgar vertical/oposición (motor 5): 0° = pulgar plano, pegado al plano de la mano,
    180° = pulgar levantado en posición vertical (separado, como en un "OK" o un "like").
  - Muñeca (motor 6): a nivel LÓGICO, 0° = "izquierda" y 180° = "derecha" (igual que antes,
    para que las poses no cambien de significado). A nivel FÍSICO el servo ahora gira al
    sentido contrario: el ángulo se invierte justo antes de escribirlo (ver escribirMuneca()).
  - Codo (motor 16): 0° = codo totalmente abajo (brazo extendido), 180° = codo totalmente arriba (flexionado).

  IMPORTANTE: Estos ángulos son de partida. Ajusta las poses en POSES[] según lo que
  veas físicamente en tu mano/brazo una vez montada (evita forzar los servos contra el tope mecánico).

  SEGURIDAD DE LOS SERVOS: todos los servos se escriben ahora a través de
  escribirServoSeguro(), que limita (constrain) el ángulo a un rango seguro por servo,
  para que ninguno llegue a forzarse contra su tope mecánico. El motor 5 (pulgar vertical),
  que era el que se estaba forzando y arriesgaba romperse, tiene un rango más estrecho
  todavía (ver LIM_MIN_PULGAR_VERTICAL / LIM_MAX_PULGAR_VERTICAL). Ajusta estos límites
  según lo que veas físicamente en tu mano.

  No modifica ni depende de la carpeta mano-flex (esa mano se controla por sensores flex).

  MODOS DE FUNCIONAMIENTO (Monitor Serie a 9600 baudios):
  Escribe un número y pulsa Enter para cambiar de modo en cualquier momento:
    0 -> Modo por defecto: poses aleatorias (comportamiento original).
    1 -> Modo "Piedra, papel o tijera": la mano juega sola una jugada aleatoria cada cierto tiempo.
*/

#include <Servo.h>

// ---------- Pines ----------
const uint8_t PIN_PULGAR_FLEXION = 30;
const uint8_t PIN_INDICE         = 31;
const uint8_t PIN_CORAZON        = 32;
const uint8_t PIN_ANULAR_MENIQUE = 33;
const uint8_t PIN_PULGAR_VERTICAL = 34;
const uint8_t PIN_MUNECA          = 35;
const uint8_t PIN_CODO            = 45;

const uint8_t NUM_SERVOS = 7;

// ---------- Objetos Servo ----------
Servo servoPulgarFlexion;
Servo servoIndice;
Servo servoCorazon;
Servo servoAnularMenique;
Servo servoPulgarVertical;
Servo servoMuneca;
Servo servoCodo;

// ---------- Ángulos de referencia ----------
const int DEDO_CERRADO = 0;
const int DEDO_ABIERTO = 180;
const int DEDO_MEDIO   = 90;

const int PULGAR_FLEX_CERRADO = 0;
const int PULGAR_FLEX_ABIERTO = 180;
const int PULGAR_FLEX_MEDIO   = 90;
const int PULGAR_FLEX_OK      = 25; // más cerrado que MEDIO, para pellizcar con el índice en la pose "OK"

const int PULGAR_VERT_PLANO   = 0;
const int PULGAR_VERT_ARRIBA  = 180;
const int PULGAR_VERT_MEDIO   = 90;
const int PULGAR_VERT_OK      = 25; // más cerrado/plano que MEDIO, para la pose "OK"

const int MUNECA_IZQUIERDA = 0;
const int MUNECA_CENTRO    = 90;
const int MUNECA_DERECHA   = 180;

const int CODO_ABAJO = 0;
const int CODO_MEDIO = 90;
const int CODO_ARRIBA = 180;

// ---------- Límites de seguridad de los servos ----------
// Cada servo se limita (constrain) a un rango algo más estrecho que 0-180 para que
// nunca llegue a forzarse contra su tope mecánico. Ajusta estos valores según lo
// que veas físicamente al mover cada motor en tu mano.
const int LIM_MIN_PULGAR_FLEX = 5;
const int LIM_MAX_PULGAR_FLEX = 175;

const int LIM_MIN_INDICE = 5;
const int LIM_MAX_INDICE = 175;

const int LIM_MIN_CORAZON = 5;
const int LIM_MAX_CORAZON = 175;

const int LIM_MIN_ANULAR_MENIQUE = 5;
const int LIM_MAX_ANULAR_MENIQUE = 175;

// Motor 5 (pulgar vertical/oposición): este era el que se estaba forzando contra el
// tope mecánico. Se le da un margen bastante más amplio (rango más estrecho) para
// que nunca llegue a los extremos físicos 0°/180°. Si sigue apretando, estrecha aún
// más este rango (por ejemplo 30-150).
const int LIM_MIN_PULGAR_VERTICAL = 20;
const int LIM_MAX_PULGAR_VERTICAL = 160;

const int LIM_MIN_MUNECA = 5;
const int LIM_MAX_MUNECA = 175;

const int LIM_MIN_CODO = 5;
const int LIM_MAX_CODO = 175;

// ---------- Definición de una pose de mano ----------
struct PoseMano {
  const char* nombre;
  int pulgarFlexion;
  int indice;
  int corazon;
  int anularMenique;
  int pulgarVertical;
  int muneca;
  int codo;
};

// Cada pose define el ángulo de los 7 servos (dedos + muñeca + codo). Añade o ajusta poses libremente.
const PoseMano POSES[] = {
  { "Mano relajada",     PULGAR_FLEX_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    PULGAR_VERT_MEDIO,  MUNECA_CENTRO,    CODO_MEDIO   },
  { "Puno cerrado",      PULGAR_FLEX_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_CENTRO,    CODO_ARRIBA  },
  { "Mano abierta",      PULGAR_FLEX_ABIERTO,  DEDO_ABIERTO,  DEDO_ABIERTO,  DEDO_ABIERTO,  PULGAR_VERT_ARRIBA, MUNECA_CENTRO,    CODO_ABAJO   },
  { "Senal de paz",      PULGAR_FLEX_CERRADO,  DEDO_ABIERTO,  DEDO_ABIERTO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_DERECHA,   CODO_MEDIO   },
  { "Pulgar arriba",     PULGAR_FLEX_ABIERTO,  DEDO_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_ARRIBA, MUNECA_CENTRO,    CODO_ARRIBA  },
  { "Senalar",           PULGAR_FLEX_CERRADO,  DEDO_ABIERTO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_IZQUIERDA, CODO_ABAJO   },
  { "OK",                PULGAR_FLEX_OK,       DEDO_MEDIO,    DEDO_ABIERTO,  DEDO_ABIERTO,  PULGAR_VERT_OK,     MUNECA_CENTRO,    CODO_MEDIO   },
  { "Garra",             PULGAR_FLEX_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    PULGAR_VERT_MEDIO,  MUNECA_DERECHA,   CODO_ARRIBA  },
};
const uint8_t NUM_POSES = sizeof(POSES) / sizeof(POSES[0]);

// Poses del juego "Piedra, papel o tijera" (mismo formato que POSES[])
const PoseMano JUGADAS_RPS[] = {
  { "Piedra",  PULGAR_FLEX_CERRADO, DEDO_CERRADO, DEDO_CERRADO, DEDO_CERRADO, PULGAR_VERT_PLANO,  MUNECA_CENTRO, CODO_ARRIBA },
  { "Papel",   PULGAR_FLEX_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, PULGAR_VERT_ARRIBA, MUNECA_CENTRO, CODO_MEDIO  },
  { "Tijera",  PULGAR_FLEX_CERRADO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_CERRADO, PULGAR_VERT_PLANO,  MUNECA_CENTRO, CODO_MEDIO  },
};
const uint8_t NUM_JUGADAS_RPS = sizeof(JUGADAS_RPS) / sizeof(JUGADAS_RPS[0]);

// ---------- Modos de funcionamiento ----------
const uint8_t MODO_POSES_ALEATORIAS = 0; // modo por defecto
const uint8_t MODO_PIEDRA_PAPEL_TIJERA = 1;
uint8_t modoActual = MODO_POSES_ALEATORIAS;

// ---------- Temporización ----------
const uint16_t TIEMPO_MIN_ENTRE_POSES_MS = 3000;
const uint16_t TIEMPO_MAX_ENTRE_POSES_MS = 7000;
const uint16_t TIEMPO_MIN_ENTRE_JUGADAS_MS = 4000;
const uint16_t TIEMPO_MAX_ENTRE_JUGADAS_MS = 8000;
const uint8_t  PASOS_MOVIMIENTO_SUAVE = 30; // nº de pasos para interpolar entre poses
const uint8_t  RETARDO_PASO_MS = 20;        // retardo entre pasos de interpolación

// Sacudida del brazo (arriba/abajo) antes de sacar la jugada, como al contar "piedra, papel o tijera..."
const uint8_t  SACUDIDAS_CODO = 3;      // nº de veces que sube y baja el brazo antes de la jugada
const uint8_t  SACUDIDA_PASOS = 10;     // pasos de interpolacion de cada subida/bajada
const uint8_t  SACUDIDA_RETARDO_MS = 18; // retardo entre pasos de la sacudida

// ---------- Estado ----------
PoseMano poseActual = POSES[0];
unsigned long proximoCambioPose = 0;
unsigned long proximaJugadaRPS = 0;
uint8_t indicePoseAnterior = 0;
uint8_t indiceJugadaAnterior = 0;

void setup() {
  Serial.begin(9600);

  servoPulgarFlexion.attach(PIN_PULGAR_FLEXION);
  servoIndice.attach(PIN_INDICE);
  servoCorazon.attach(PIN_CORAZON);
  servoAnularMenique.attach(PIN_ANULAR_MENIQUE);
  servoPulgarVertical.attach(PIN_PULGAR_VERTICAL);
  servoMuneca.attach(PIN_MUNECA);
  servoCodo.attach(PIN_CODO);

  randomSeed(analogRead(A0));

  Serial.println(F("=== Mano automatica iniciada ==="));
  mostrarMenuModos();

  aplicarPoseInstantanea(POSES[0]);
  poseActual = POSES[0];
  imprimirPose(POSES[0]);

  proximoCambioPose = millis() + random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS);
  proximaJugadaRPS = millis() + random(TIEMPO_MIN_ENTRE_JUGADAS_MS, TIEMPO_MAX_ENTRE_JUGADAS_MS);
}

void loop() {
  leerCambioDeModo();

  if (modoActual == MODO_PIEDRA_PAPEL_TIJERA) {
    loopPiedraPapelTijera();
  } else {
    loopPosesAleatorias();
  }
}

// Muestra los modos disponibles por el Monitor Serie
void mostrarMenuModos() {
  Serial.println(F("Escribe un numero y pulsa Enter para cambiar de modo:"));
  Serial.println(F("  0 -> Poses aleatorias (por defecto)"));
  Serial.println(F("  1 -> Piedra, papel o tijera"));
}

// Comprueba si ha llegado un numero por el Monitor Serie y cambia de modo si es valido
void leerCambioDeModo() {
  if (Serial.available() == 0) {
    return;
  }

  int valor = Serial.parseInt();
  while (Serial.available() > 0) {
    Serial.read(); // descarta el resto de la linea (salto de linea, etc.)
  }

  if (valor != MODO_POSES_ALEATORIAS && valor != MODO_PIEDRA_PAPEL_TIJERA) {
    Serial.println(F("Modo invalido. Usa 0 (poses aleatorias) o 1 (piedra, papel o tijera)."));
    return;
  }

  modoActual = valor;
  Serial.print(F("Modo cambiado a: "));
  Serial.println(modoActual == MODO_PIEDRA_PAPEL_TIJERA ? F("Piedra, papel o tijera") : F("Poses aleatorias"));

  proximoCambioPose = millis() + random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS);
  proximaJugadaRPS = millis() + random(TIEMPO_MIN_ENTRE_JUGADAS_MS, TIEMPO_MAX_ENTRE_JUGADAS_MS);
}

// Comportamiento del modo por defecto: poses aleatorias de la mano
void loopPosesAleatorias() {
  if (millis() >= proximoCambioPose) {
    uint8_t indiceNuevaPose = elegirPoseDistinta(indicePoseAnterior);
    Serial.print(F("Nueva pose seleccionada -> "));
    Serial.println(POSES[indiceNuevaPose].nombre);

    moverAPoseSuave(POSES[indiceNuevaPose]);
    poseActual = POSES[indiceNuevaPose];
    indicePoseAnterior = indiceNuevaPose;

    imprimirPose(POSES[indiceNuevaPose]);

    proximoCambioPose = millis() + random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS);
  }
}

// Comportamiento del modo "Piedra, papel o tijera": la mano juega sola una jugada aleatoria
void loopPiedraPapelTijera() {
  if (millis() >= proximaJugadaRPS) {
    uint8_t indiceNuevaJugada;
    do {
      indiceNuevaJugada = random(0, NUM_JUGADAS_RPS);
    } while (indiceNuevaJugada == indiceJugadaAnterior && NUM_JUGADAS_RPS > 1);

    sacudirBrazoAntesDeJugar();

    Serial.print(F("Jugando... -> "));
    Serial.println(JUGADAS_RPS[indiceNuevaJugada].nombre);

    moverAPoseSuave(JUGADAS_RPS[indiceNuevaJugada]);
    poseActual = JUGADAS_RPS[indiceNuevaJugada];
    indiceJugadaAnterior = indiceNuevaJugada;

    imprimirPose(JUGADAS_RPS[indiceNuevaJugada]);

    proximaJugadaRPS = millis() + random(TIEMPO_MIN_ENTRE_JUGADAS_MS, TIEMPO_MAX_ENTRE_JUGADAS_MS);
  }
}

// Sube y baja el brazo (codo) varias veces antes de mostrar la jugada, como al contar
// "piedra, papel o tijera". Solo mueve el codo; el resto de la mano no cambia todavia.
void sacudirBrazoAntesDeJugar() {
  Serial.println(F("Preparando jugada..."));
  for (uint8_t i = 0; i < SACUDIDAS_CODO; i++) {
    moverCodoSuave(CODO_ARRIBA);
    moverCodoSuave(CODO_MEDIO);
  }
  poseActual.codo = CODO_MEDIO;
}

// Interpola solo el servo del codo hasta el angulo indicado (para la sacudida del brazo)
void moverCodoSuave(int destino) {
  int origen = servoCodo.read();
  for (uint8_t paso = 1; paso <= SACUDIDA_PASOS; paso++) {
    servoCodo.write(map(paso, 0, SACUDIDA_PASOS, origen, destino));
    delay(SACUDIDA_RETARDO_MS);
  }
}

// Imprime por el Monitor Serie el nombre de la pose y el angulo aplicado a cada servo
void imprimirPose(const PoseMano &pose) {
  Serial.print(F("Pose aplicada: "));
  Serial.println(pose.nombre);
  Serial.print(F("  Pulgar flexion: ")); Serial.println(pose.pulgarFlexion);
  Serial.print(F("  Indice: ")); Serial.println(pose.indice);
  Serial.print(F("  Corazon: ")); Serial.println(pose.corazon);
  Serial.print(F("  Anular+Menique: ")); Serial.println(pose.anularMenique);
  Serial.print(F("  Pulgar vertical: ")); Serial.println(pose.pulgarVertical);
  Serial.print(F("  Muneca: ")); Serial.println(pose.muneca);
  Serial.print(F("  Codo: ")); Serial.println(pose.codo);
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

// Escribe un ángulo en un servo, pero primero lo limita (constrain) a un rango seguro,
// para que el servo nunca se fuerce contra su tope mecánico.
void escribirServoSeguro(Servo &servo, int angulo, int limMin, int limMax) {
  int anguloSeguro = constrain(angulo, limMin, limMax);
  servo.write(anguloSeguro);
}

// La muñeca ahora gira al sentido contrario al original: invertimos el ángulo lógico
// (180 - angulo) justo antes de escribirlo en el servo físico. Así las poses (que usan
// MUNECA_IZQUIERDA/CENTRO/DERECHA) no hay que tocarlas, solo se invierte el giro real.
void escribirMuneca(int anguloLogico) {
  int anguloFisico = 180 - anguloLogico;
  escribirServoSeguro(servoMuneca, anguloFisico, LIM_MIN_MUNECA, LIM_MAX_MUNECA);
}

// Aplica una pose directamente, sin interpolación (uso solo en el arranque)
void aplicarPoseInstantanea(const PoseMano &pose) {
  escribirServoSeguro(servoPulgarFlexion, pose.pulgarFlexion, LIM_MIN_PULGAR_FLEX, LIM_MAX_PULGAR_FLEX);
  escribirServoSeguro(servoIndice, pose.indice, LIM_MIN_INDICE, LIM_MAX_INDICE);
  escribirServoSeguro(servoCorazon, pose.corazon, LIM_MIN_CORAZON, LIM_MAX_CORAZON);
  escribirServoSeguro(servoAnularMenique, pose.anularMenique, LIM_MIN_ANULAR_MENIQUE, LIM_MAX_ANULAR_MENIQUE);
  escribirServoSeguro(servoPulgarVertical, pose.pulgarVertical, LIM_MIN_PULGAR_VERTICAL, LIM_MAX_PULGAR_VERTICAL);
  escribirMuneca(pose.muneca);
  escribirServoSeguro(servoCodo, pose.codo, LIM_MIN_CODO, LIM_MAX_CODO);
}

// Interpola los 7 servos desde la pose actual hasta la pose destino, moviéndolos
// todos a la vez (pulgar, muñeca y codo quedan sincronizados con el resto de dedos)
void moverAPoseSuave(const PoseMano &destino) {
  PoseMano origen = poseActual;

  for (uint8_t paso = 1; paso <= PASOS_MOVIMIENTO_SUAVE; paso++) {
    escribirServoSeguro(servoPulgarFlexion, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.pulgarFlexion, destino.pulgarFlexion), LIM_MIN_PULGAR_FLEX, LIM_MAX_PULGAR_FLEX);
    escribirServoSeguro(servoIndice, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.indice, destino.indice), LIM_MIN_INDICE, LIM_MAX_INDICE);
    escribirServoSeguro(servoCorazon, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.corazon, destino.corazon), LIM_MIN_CORAZON, LIM_MAX_CORAZON);
    escribirServoSeguro(servoAnularMenique, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.anularMenique, destino.anularMenique), LIM_MIN_ANULAR_MENIQUE, LIM_MAX_ANULAR_MENIQUE);
    escribirServoSeguro(servoPulgarVertical, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.pulgarVertical, destino.pulgarVertical), LIM_MIN_PULGAR_VERTICAL, LIM_MAX_PULGAR_VERTICAL);
    escribirMuneca(map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.muneca, destino.muneca));
    escribirServoSeguro(servoCodo, map(paso, 0, PASOS_MOVIMIENTO_SUAVE, origen.codo, destino.codo), LIM_MIN_CODO, LIM_MAX_CODO);
    delay(RETARDO_PASO_MS);
  }
}
