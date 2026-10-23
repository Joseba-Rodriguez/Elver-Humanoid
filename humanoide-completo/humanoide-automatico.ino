/*
  humanoide-completo.ino  (v3) - ELVER
  Programa unificado del humanoide 3D - Arduino Mega 2560.
    - CUELLO  (3 servos)  -> mismos valores, poses y tiempos de siempre
    - OJOS    (6 servos)  -> comportamiento natural, con ánimos
    - MANO    (7 servos)  -> transiciones en cascada, gestos, pulgar protegido

  NOVEDADES DE ESTA VERSIÓN (sin hardware nuevo):
   1. Nada bloquea: ya no hay delay(). Cuello, ojos y mano se mueven a la vez.
   2. Coordinación: los ojos se adelantan al cuello y luego se recentran;
      a veces los ojos miran a la mano cuando hace un gesto.
   3. Ánimos: Tranquilo, Curioso, Somnoliento, Alerta, Tímido. Cambian solos
      cada 1-3 minutos y modifican parpadeo, mirada y velocidad de la mano.
   4. Dormir / despertar (por Serial, o automático si activas SUENO_AUTOMATICO).
   5. Mano: los dedos se cierran/abren en cascada, con curvas suaves, y hay
      gestos con significado (saludar, ven aquí, silencio, así así, tamborilear).
   6. Aleatoriedad más natural (campana de Gauss y tiempos sesgados).

  NOVEDADES v3 (Elver con asistente de voz):
   7. Conversación: escuchando presta atención (cabeza quieta, ojos al frente,
      sin gestos al azar); hablando mueve un poco la cabeza, mira a otro lado
      de vez en cuando, parpadea más y acompaña con la muñeca.
   8. Las órdenes de cuello interrumpen el movimiento en curso (sin esperar).
   9. Poses de mano a la orden (piedra/papel/tijera...), negar con la cabeza,
      guiñar un ojo y modo "quieto".
   LÍMITES: todo lo nuevo usa solo poses y constantes ya definidas. El cuello
   no tenía límites propios: ahora se calculan de POSES_CUELLO (rango y
   velocidad máxima de sus movimientos de siempre) y TODA escritura en el
   cuello pasa por escribirCuello(), que los aplica.

  COMANDOS POR EL MONITOR SERIE (9600 baudios, un carácter y Enter):
    0 Tranquilo   1 Curioso   2 Somnoliento   3 Alerta   4 Tímido
    d dormir      a despertar    g gesto de mano ahora    ? ayuda
    Gestos: h saludar  v ven aqui  s silencio  m asi asi  t tamborilear
    Cuello: C centro  I inclinar izq  D inclinar der  L girar izq  R girar der
            N negar con la cabeza
    Mano:   n puño (piedra)  b abierta (papel)  x paz (tijera)  u pulgar arriba
            w señalar  o OK  r relajada
    Otros:  j guiño   q quieto   Q fin de quieto
    Conversación (los envía el ESP32): E escuchando  H hablando  Z reposo

  Los mismos comandos llegan también desde el ESP32 (asistente de voz) por
  Serial1 a 115200: pin 19 (RX1) <- IO2 del ESP32 (conector "RGB" de la
  placa Toy-AI-Core-C3) y GND común. Solo va en ese sentido; el pin 18 (TX1)
  no se conecta (el "OK x" que se responde no lo lee nadie, es inofensivo).

  Tabla de conexionado completa:
  Módulo  Motor -> Pin -> Función
  CUELLO  7     -> 36  -> Inclinación de cuello hacia la izquierda
  CUELLO  8     -> 37  -> Inclinación de cuello hacia la derecha
  CUELLO  9     -> 38  -> Giro rotatorio del cuello (izquierda/derecha)
  OJOS    10    -> 39  -> Párpado arriba izquierdo
  OJOS    11    -> 40  -> Párpado arriba derecho
  OJOS    12    -> 41  -> Párpado abajo izquierdo
  OJOS    13    -> 42  -> Párpado abajo derecho
  OJOS    14    -> 43  -> Movimiento vertical de ojos (ambos)
  OJOS    15    -> 44  -> Movimiento horizontal de ojos (ambos)
  MANO    1     -> 30  -> Pulgar, flexión
  MANO    2     -> 31  -> Índice
  MANO    3     -> 32  -> Corazón
  MANO    4     -> 33  -> Anular + meñique
  MANO    5     -> 34  -> Pulgar, eje vertical / oposición (RECORRIDO MUY REDUCIDO)
  MANO    6     -> 35  -> Muñeca, giro (solo hacia dentro)
  MANO    16    -> 45  -> Codo, movimiento arriba/abajo
*/

#include <Servo.h>

// ======================================================================
// ======================== TIPOS Y UTILIDADES ==========================
// ======================================================================

// Mensajes detallados por Serial (los ángulos de cada pose). Desactivado por
// defecto porque imprimir mucho a 9600 baudios frena el programa.
const bool DEBUG_DETALLE = false;

// Un "canal" mueve un valor de forma suave (curva en S) sin bloquear el programa.
struct Canal {
  float valor;             // valor actual
  float desde;
  float hasta;
  unsigned long inicio;    // instante en que empieza (permite retrasos)
  unsigned long dur;       // duración en ms
  bool activo;
};

// Ánimos del robot
enum Animo {
  ANIMO_TRANQUILO = 0,
  ANIMO_CURIOSO,
  ANIMO_SOMNOLIENTO,
  ANIMO_ALERTA,
  ANIMO_TIMIDO,
  NUM_ANIMOS
};

struct PerfilAnimo {
  const char* nombre;
  float   factorParpadeo;   // x tiempo entre parpadeos (mayor = parpadea menos)
  float   factorFijacion;   // x tiempo que mantiene la mirada
  float   factorVelOjos;    // x duración de los movimientos oculares (mayor = más lento)
  uint8_t caidaParpado;     // % de cierre base de los párpados (0 = muy abiertos)
  int8_t  sesgoVertical;    // grados: + mira algo hacia abajo, - hacia arriba
  uint8_t probAmplia;       // % de miradas a un punto lejano
  uint8_t probReojo;        // % de miradas de reojo
  uint8_t probFija;         // % de miradas fijas y largas
  uint8_t probLento;        // % de parpadeos lentos
  float   factorPoses;      // x tiempo entre poses de la mano
  float   factorVelMano;    // x duración de las poses de la mano
  uint8_t probGesto;        // % de veces que en vez de una pose hace un gesto
};

const PerfilAnimo PERFILES[NUM_ANIMOS] = {
  //  nombre         parp  fij   velO  caida sesgo amp reo fija lento poses velM gesto
  { "Tranquilo",     1.0,  1.0,  1.0,   0,    0,   12,  8,  12,   8,   1.0,  1.0, 20 },
  { "Curioso",       1.4,  1.3,  0.9,   0,   -3,   20,  8,  25,   5,   0.9,  1.0, 30 },
  { "Somnoliento",   0.7,  1.5,  1.7,  25,    4,    5,  2,   8,  40,   1.6,  1.6,  8 },
  { "Alerta",        1.6,  0.6,  0.7,   0,    0,   35,  5,   3,   0,   0.7,  0.8, 30 },
  { "Timido",        0.9,  1.0,  1.2,   8,    6,    8, 20,   5,  10,   1.3,  1.3, 10 },
};

// Probabilidad (%) de cada ánimo al cambiar (suman 100)
const uint8_t PESO_ANIMO[NUM_ANIMOS] = { 40, 20, 15, 15, 10 };

// Si es true, en ánimo Somnoliento el robot a veces se duerme solo y despierta después.
const bool SUENO_AUTOMATICO = false;

// ---------- Estado global de ánimo y vida ----------
uint8_t animoActual = ANIMO_TRANQUILO;
const PerfilAnimo* perfil = &PERFILES[ANIMO_TRANQUILO];
unsigned long proximoCambioAnimo = 0;

bool durmiendo = false;
uint8_t faseDespertar = 0;            // 0 = no está despertando
unsigned long tFaseDespertar = 0;
bool despertarAutomatico = false;
unsigned long tDespertarAutomatico = 0;

// ======================================================================
// =========================== CUELLO ==================================
// ============ (valores, poses y tiempos SIN CAMBIOS) ==================
// ======================================================================

// ---------- Pines (cuello) ----------
const uint8_t PIN_INCLINACION_IZQUIERDA = 36;
const uint8_t PIN_INCLINACION_DERECHA   = 37;
const uint8_t PIN_ROTACION              = 38;

// ---------- Objetos Servo (cuello) ----------
Servo servoInclinacionIzquierda;
Servo servoInclinacionDerecha;
Servo servoRotacion;

// ---------- Posición neutra y amplitud de movimiento (cuello) ----------
const int NEUTRO = 90;

const int AMPLITUD_INCLINACION = 25;
const int AMPLITUD_ROTACION    = 35;

const int INCLINACION_IZQ_ACTIVA   = NEUTRO + AMPLITUD_INCLINACION;
const int INCLINACION_IZQ_RELAJADA = NEUTRO - (AMPLITUD_INCLINACION / 2);
const int INCLINACION_DER_ACTIVA   = NEUTRO + AMPLITUD_INCLINACION;
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

const PoseCuello POSES_CUELLO[] = {
  { "Centro",                       NEUTRO,                   NEUTRO,                   NEUTRO           },
  { "Inclinar poco a la izquierda", INCLINACION_IZQ_ACTIVA,   INCLINACION_DER_RELAJADA, NEUTRO           },
  { "Inclinar poco a la derecha",   INCLINACION_IZQ_RELAJADA, INCLINACION_DER_ACTIVA,   NEUTRO           },
  { "Girar poco a la izquierda",    NEUTRO,                   NEUTRO,                   ROTACION_IZQUIERDA },
  { "Girar poco a la derecha",      NEUTRO,                   NEUTRO,                   ROTACION_DERECHA   },
};
const uint8_t NUM_POSES_CUELLO = sizeof(POSES_CUELLO) / sizeof(POSES_CUELLO[0]);

// ---------- Temporización (cuello) ----------
const uint16_t TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS = 3000;
const uint16_t TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS = 7000;
const uint16_t PASOS_MOVIMIENTO_SUAVE_CUELLO = 85;
const uint8_t  RETARDO_PASO_MS_CUELLO = 40;

// ---------- Coordinación con los ojos (nuevo; no cambia los valores del cuello) ----------
const uint16_t ADELANTO_OJOS_MS = 150;          // los ojos miran hacia el giro antes de que empiece el cuello
const uint16_t PASO_CUELLO_RECENTRAR_OJOS = 45; // a mitad de giro los ojos vuelven al centro

// ---------- Estado (cuello) ----------
enum EstadoCuello { CUELLO_ESPERA, CUELLO_PREPARANDO, CUELLO_MOVIENDO };

// Qué tipo de movimiento hace el cuello (decide la espera al terminar)
enum TipoMovCuello { MOV_AUTOMATICO, MOV_PEDIDO, MOV_CONVERSACION, MOV_SECUENCIA };

EstadoCuello estadoCuello = CUELLO_ESPERA;
PoseCuello poseActualCuello = POSES_CUELLO[0];
PoseCuello origenCuello = POSES_CUELLO[0];
PoseCuello destinoCuello = POSES_CUELLO[0];
PoseCuello cuelloEscrito = POSES_CUELLO[0];     // lo último escrito en los servos (posición real)
TipoMovCuello tipoMovCuello = MOV_AUTOMATICO;
uint16_t pasosMovCuello = PASOS_MOVIMIENTO_SUAVE_CUELLO;
uint8_t indiceDestinoCuello = 0;
uint16_t pasoCuello = 0;
unsigned long tInicioCuello = 0;
unsigned long tProximoPasoCuello = 0;
unsigned long proximoMovimientoCuello = 0;
uint8_t indicePoseAnteriorCuello = 0;
int8_t poseCuelloPedida = -1;                   // pose pedida por comando (-1 = ninguna)

// ---------- Límites del cuello (calculados de POSES_CUELLO al arrancar) ----------
// El cuello no tiene constantes de límite: su rango es el que cubren sus poses
// y su velocidad máxima la de su movimiento de siempre más rápido (el mayor
// recorrido entre dos poses en PASOS_MOVIMIENTO_SUAVE_CUELLO pasos).
int limInclIzqMin, limInclIzqMax;
int limInclDerMin, limInclDerMax;
int limRotMin, limRotMax;
float maxGradosPasoCuello;                       // grados por paso de RETARDO_PASO_MS_CUELLO

// Secuencia corta de posiciones (para negar con la cabeza)
const uint8_t MAX_SECUENCIA_CUELLO = 5;
const int AMPLITUD_NEGAR = 8;                    // grados a cada lado (dentro de los límites)
PoseCuello secuenciaCuello[MAX_SECUENCIA_CUELLO];
uint8_t numSecuenciaCuello = 0;
uint8_t idxSecuenciaCuello = 0;

// ======================================================================
// ===================== CONVERSACIÓN (asistente de voz) ================
// ======================================================================

enum ModoConversacion { CONV_REPOSO, CONV_ESCUCHANDO, CONV_HABLANDO };
ModoConversacion modoConv = CONV_REPOSO;
unsigned long tUltimoAvisoConv = 0;
// Si el ESP32 deja de avisar (desconectado), Elver vuelve solo a su vida normal.
// El ESP32 repite el estado cada 20 s, así que 60 s sin nada = desconectado.
const unsigned long TIMEOUT_CONVERSACION_MS = 60000UL;
bool cuelloEscuchaPendiente = false;             // al empezar a escuchar, mira al frente

// Hablando: pequeños movimientos de cabeza hacia una fracción de una pose
const float FRACCION_CUELLO_HABLANDO_MIN = 0.15f;
const float FRACCION_CUELLO_HABLANDO_MAX = 0.40f;
const uint16_t ESPERA_CUELLO_HABLANDO_MIN_MS = 1200;
const uint16_t ESPERA_CUELLO_HABLANDO_MAX_MS = 3200;
// Escuchando: a veces ladea un poco la cabeza (interés)
const float FRACCION_CUELLO_ESCUCHANDO_MAX = 0.30f;
const uint8_t PROB_LADEAR_ESCUCHANDO = 50;       // %

// Hablando: mira a otro lado de vez en cuando (como al pensar lo que dice)
const uint8_t PROB_APARTAR_MIRADA_HABLANDO = 25; // %
const float FACTOR_PARPADEO_HABLANDO = 0.75f;    // parpadea algo más al hablar
const uint8_t CAIDA_PARPADO_ESCUCHANDO = 10;     // % menos de cierre: ojos más abiertos

// Hablando: la muñeca acompaña (pequeños golpes de ritmo alrededor de su pose)
const uint8_t AMPLITUD_BEAT_MUNECA = 6;          // grados (los límites se aplican igual)

// Modo quieto: sin movimientos voluntarios (solo parpadea y micro-movimientos)
bool quieto = false;

bool guinoPedido = false;                        // guiño a la orden pendiente de hacerse

// Pose pedida (mano o cuello): se mantiene un rato antes de volver a lo automático
const uint16_t MANTENER_POSE_PEDIDA_MS = 5000;
unsigned long cuelloReservadoHasta = 0;
unsigned long manoReservadaHasta = 0;

// ======================================================================
// ============================ OJOS ====================================
// ======================================================================

// ---------- Pines (ojos) ----------
const uint8_t PIN_PARPADO_SUP_IZQ = 39;
const uint8_t PIN_PARPADO_SUP_DER = 40;
const uint8_t PIN_PARPADO_INF_IZQ = 41;
const uint8_t PIN_PARPADO_INF_DER = 42;
const uint8_t PIN_OJOS_VERTICAL   = 43;
const uint8_t PIN_OJOS_HORIZONTAL = 44;

// ---------- Objetos Servo (ojos) ----------
Servo servoParpadoSupIzq;
Servo servoParpadoSupDer;
Servo servoParpadoInfIzq;
Servo servoParpadoInfDer;
Servo servoOjosVertical;
Servo servoOjosHorizontal;

// ---------- Ángulos de los párpados (LÍMITES: no se tocan) ----------
const int PARPADO_SUP_IZQ_ABIERTO = 130;
const int PARPADO_SUP_IZQ_CERRADO = 90;
const int PARPADO_SUP_DER_ABIERTO = 50;
const int PARPADO_SUP_DER_CERRADO = 90;

const int PARPADO_INF_IZQ_ABIERTO = 50;
const int PARPADO_INF_IZQ_CERRADO = 90;
const int PARPADO_INF_DER_ABIERTO = 130;
const int PARPADO_INF_DER_CERRADO = 90;

// ---------- Ángulos de movimiento ocular (LÍMITES: no se tocan) ----------
const int OJOS_VERTICAL_CENTRO   = 90;
const int OJOS_VERTICAL_ARRIBA   = 70;
const int OJOS_VERTICAL_ABAJO    = 110;

const int OJOS_HORIZONTAL_CENTRO    = 90;
const int OJOS_HORIZONTAL_IZQUIERDA = 70;
const int OJOS_HORIZONTAL_DERECHA   = 100;

// ---------- Temporización natural (ojos) ----------
const uint16_t PARPADEO_MIN_MS = 4000;
const uint16_t PARPADEO_MAX_MS = 10000;
const uint8_t  PROB_PARPADEO_DOBLE = 12;         // % de parpadeos dobles

const unsigned long GUINO_MIN_MS = 60000UL;      // un guiño cada 1-2,5 min aprox.
const unsigned long GUINO_MAX_MS = 150000UL;

const uint16_t MIRADA_MIN_MS = 1200;
const uint16_t MIRADA_MAX_MS = 4500;
const uint16_t MIRADA_FIJA_MIN_MS = 6000;
const uint16_t MIRADA_FIJA_MAX_MS = 11000;
const uint8_t  PROB_PARPADEO_CON_MIRADA = 45;    // % de cambios grandes de mirada con parpadeo

const uint8_t  MICRO_AMPLITUD = 2;               // micro-movimientos en grados (0 = desactivado)
const uint8_t  PARPADO_BAJADA_MIRADA_PCT = 20;   // el párpado baja algo al mirar hacia abajo

// Hacia dónde miran los ojos cuando la mano hace un gesto.
// Si la mano está en el otro lado, cambia OJOS_HORIZONTAL_DERECHA por OJOS_HORIZONTAL_IZQUIERDA.
const int MIRAR_MANO_HORIZONTAL = OJOS_HORIZONTAL_DERECHA;
const int MIRAR_MANO_VERTICAL   = OJOS_VERTICAL_ABAJO - 8;

// ---------- Estado (ojos) ----------
Canal cVert;        // grados
Canal cHoriz;       // grados
Canal cParpIzq;     // % de cierre: 0 abierto ... 100 cerrado
Canal cParpDer;

int ultVertical = -1, ultHorizontal = -1;
int ultSupIzq = -1, ultSupDer = -1, ultInfIzq = -1, ultInfDer = -1;

int objetivoV = OJOS_VERTICAL_CENTRO;      // punto que se está fijando
int objetivoH = OJOS_HORIZONTAL_CENTRO;
float parpBase = 0;                         // cierre "en reposo" de los párpados

unsigned long proximoParpadeo = 0;
unsigned long proximoGuino = 0;
unsigned long proximaMirada = 0;
unsigned long proximoMicro = 0;

bool miradaFija = false;
bool reojoPendiente = false;
int reojoRetornoV = OJOS_VERTICAL_CENTRO;
int reojoRetornoH = OJOS_HORIZONTAL_CENTRO;

// Gesto de párpados (parpadeo normal/lento/doble o guiño)
uint8_t faseParp = 0;            // 0 libre, 1 cerrando, 2 cerrado, 3 abriendo, 4 pausa entre parpadeos
bool gestoIzq = false;
bool gestoDer = false;
unsigned long durCierreParp = 0;
unsigned long mantenerParp = 0;
unsigned long durAperturaParp = 0;
unsigned long pausaParp = 0;
uint8_t repeticionesParp = 0;
unsigned long finFaseParp = 0;

// ======================================================================
// ============================ MANO ====================================
// ======================================================================

// ---------- Pines (mano) ----------
const uint8_t PIN_PULGAR_FLEXION  = 30;
const uint8_t PIN_INDICE          = 31;
const uint8_t PIN_CORAZON         = 32;
const uint8_t PIN_ANULAR_MENIQUE  = 33;
const uint8_t PIN_PULGAR_VERTICAL = 34;   // con recorrido MUY reducido
const uint8_t PIN_MUNECA          = 35;
const uint8_t PIN_CODO            = 45;

const uint8_t NUM_SERVOS = 7;

// Índices de los canales de la mano
enum { M_PULGAR_FLEX = 0, M_INDICE, M_CORAZON, M_ANULAR, M_PULGAR_VERT, M_MUNECA, M_CODO };

// ---------- Objetos Servo (mano) ----------
Servo servoPulgarFlexion;
Servo servoIndice;
Servo servoCorazon;
Servo servoAnularMenique;
Servo servoPulgarVertical;
Servo servoMuneca;
Servo servoCodo;

// ---------- Ángulos de referencia (mano) ----------
const int DEDO_CERRADO = 0;
const int DEDO_ABIERTO = 180;
const int DEDO_MEDIO   = 90;
const int DEDO_TAP     = 45;     // "golpecito" al tamborilear

const int PULGAR_FLEX_CERRADO = 0;
const int PULGAR_FLEX_ABIERTO = 180;
const int PULGAR_FLEX_MEDIO   = 90;
const int PULGAR_FLEX_OK      = 25;

// Pulgar vertical (motor 5): recorrido MUY pequeño (80-100).
// Si al probarlo va al revés de lo esperado, intercambia PLANO y ARRIBA.
const int PULGAR_VERT_PLANO   = 80;
const int PULGAR_VERT_MEDIO   = 90;
const int PULGAR_VERT_ARRIBA  = 100;
const int PULGAR_VERT_OK      = 85;

// Muñeca: ÁNGULO REAL del servo. 90 = recta. Solo hacia DENTRO (90 -> 175).
const int MUNECA_RECTA        = 90;
const int MUNECA_DENTRO_POCO  = 110;
const int MUNECA_DENTRO_MEDIO = 135;
const int MUNECA_DENTRO_MAX   = 175;
const int MUNECA_SALUDO_A     = 105;     // extremos del "adiós" con la mano
const int MUNECA_SALUDO_B     = 150;

const int CODO_ABAJO = 0;
const int CODO_MEDIO = 90;
const int CODO_ARRIBA = 180;

// ---------- Límites de seguridad de los servos (mano) ----------
const int LIM_MIN_PULGAR_FLEX = 5;
const int LIM_MAX_PULGAR_FLEX = 175;

const int LIM_MIN_INDICE = 5;
const int LIM_MAX_INDICE = 175;

const int LIM_MIN_CORAZON = 5;
const int LIM_MAX_CORAZON = 175;

const int LIM_MIN_ANULAR_MENIQUE = 5;
const int LIM_MAX_ANULAR_MENIQUE = 175;

const int LIM_MIN_PULGAR_VERTICAL = 75;
const int LIM_MAX_PULGAR_VERTICAL = 105;

const int LIM_MIN_MUNECA = 90;           // nunca hacia fuera
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
  int muneca;        // ángulo real del servo (90 recta ... 175 máximo hacia dentro)
  int codo;
};

const PoseMano POSES_MANO[] = {
  { "Mano relajada",     PULGAR_FLEX_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    PULGAR_VERT_MEDIO,  MUNECA_DENTRO_POCO,  CODO_MEDIO   },
  { "Puno cerrado",      PULGAR_FLEX_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_DENTRO_MEDIO, CODO_ARRIBA  },
  { "Mano abierta",      PULGAR_FLEX_ABIERTO,  DEDO_ABIERTO,  DEDO_ABIERTO,  DEDO_ABIERTO,  PULGAR_VERT_ARRIBA, MUNECA_DENTRO_POCO,  CODO_ABAJO   },
  { "Senal de paz",      PULGAR_FLEX_CERRADO,  DEDO_ABIERTO,  DEDO_ABIERTO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_DENTRO_MEDIO, CODO_MEDIO   },
  { "Pulgar arriba",     PULGAR_FLEX_ABIERTO,  DEDO_CERRADO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_ARRIBA, MUNECA_DENTRO_POCO,  CODO_ARRIBA  },
  { "Senalar",           PULGAR_FLEX_CERRADO,  DEDO_ABIERTO,  DEDO_CERRADO,  DEDO_CERRADO,  PULGAR_VERT_PLANO,  MUNECA_DENTRO_MAX,   CODO_ABAJO   },
  { "OK",                PULGAR_FLEX_OK,       DEDO_MEDIO,    DEDO_ABIERTO,  DEDO_ABIERTO,  PULGAR_VERT_OK,     MUNECA_DENTRO_POCO,  CODO_MEDIO   },
  { "Garra",             PULGAR_FLEX_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    DEDO_MEDIO,    PULGAR_VERT_MEDIO,  MUNECA_DENTRO_MEDIO, CODO_ARRIBA  },
};
const uint8_t NUM_POSES_MANO = sizeof(POSES_MANO) / sizeof(POSES_MANO[0]);

// ---------- Poses de los gestos ----------
// (CODO_ARRIBA = brazo levantado; si no queda bien en el robot, cámbialo aquí)
const PoseMano POSE_SALUDO_A = { "Saludo A", PULGAR_FLEX_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, PULGAR_VERT_ARRIBA, MUNECA_SALUDO_A, CODO_ARRIBA };
const PoseMano POSE_SALUDO_B = { "Saludo B", PULGAR_FLEX_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, PULGAR_VERT_ARRIBA, MUNECA_SALUDO_B, CODO_ARRIBA };

const PoseMano POSE_VEN_A = { "Ven A", PULGAR_FLEX_CERRADO, DEDO_ABIERTO, DEDO_CERRADO, DEDO_CERRADO, PULGAR_VERT_PLANO, MUNECA_DENTRO_MEDIO, CODO_MEDIO };
const PoseMano POSE_VEN_B = { "Ven B", PULGAR_FLEX_CERRADO, DEDO_MEDIO,   DEDO_CERRADO, DEDO_CERRADO, PULGAR_VERT_PLANO, MUNECA_DENTRO_MEDIO, CODO_MEDIO };

const PoseMano POSE_SILENCIO = { "Silencio", PULGAR_FLEX_CERRADO, DEDO_ABIERTO, DEDO_CERRADO, DEDO_CERRADO, PULGAR_VERT_PLANO, MUNECA_DENTRO_POCO, CODO_ARRIBA };

const PoseMano POSE_ASI_A = { "Asi asi A", PULGAR_FLEX_MEDIO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, PULGAR_VERT_MEDIO, MUNECA_SALUDO_A, CODO_MEDIO };
const PoseMano POSE_ASI_B = { "Asi asi B", PULGAR_FLEX_MEDIO, DEDO_ABIERTO, DEDO_ABIERTO, DEDO_ABIERTO, PULGAR_VERT_MEDIO, MUNECA_SALUDO_B, CODO_MEDIO };

const PoseMano POSE_TAP_INDICE  = { "Tap indice",  PULGAR_FLEX_MEDIO, DEDO_TAP,   DEDO_MEDIO, DEDO_MEDIO, PULGAR_VERT_MEDIO, MUNECA_DENTRO_POCO, CODO_MEDIO };
const PoseMano POSE_TAP_CORAZON = { "Tap corazon", PULGAR_FLEX_MEDIO, DEDO_MEDIO, DEDO_TAP,   DEDO_MEDIO, PULGAR_VERT_MEDIO, MUNECA_DENTRO_POCO, CODO_MEDIO };
const PoseMano POSE_TAP_ANULAR  = { "Tap anular",  PULGAR_FLEX_MEDIO, DEDO_MEDIO, DEDO_MEDIO, DEDO_TAP,   PULGAR_VERT_MEDIO, MUNECA_DENTRO_POCO, CODO_MEDIO };

// Un paso de un gesto: a qué pose ir, en cuánto tiempo y cuánto esperar después
struct PasoGesto {
  const PoseMano* pose;
  uint16_t durMs;
  uint16_t pausaMs;
};

struct Gesto {
  const char* nombre;
  const PasoGesto* pasos;
  uint8_t numPasos;
};

const PasoGesto PASOS_SALUDAR[] = {
  { &POSE_SALUDO_A, 900, 100 },
  { &POSE_SALUDO_B, 300, 0 },
  { &POSE_SALUDO_A, 300, 0 },
  { &POSE_SALUDO_B, 300, 0 },
  { &POSE_SALUDO_A, 300, 200 },
  { &POSES_MANO[0], 1000, 0 },
};

const PasoGesto PASOS_VEN_AQUI[] = {
  { &POSE_VEN_A, 900, 100 },
  { &POSE_VEN_B, 260, 0 },
  { &POSE_VEN_A, 260, 0 },
  { &POSE_VEN_B, 260, 0 },
  { &POSE_VEN_A, 260, 150 },
  { &POSES_MANO[0], 900, 0 },
};

const PasoGesto PASOS_SILENCIO[] = {
  { &POSE_SILENCIO, 900, 1600 },
  { &POSES_MANO[0], 900, 0 },
};

const PasoGesto PASOS_ASI_ASI[] = {
  { &POSE_ASI_A, 800, 0 },
  { &POSE_ASI_B, 450, 0 },
  { &POSE_ASI_A, 450, 0 },
  { &POSE_ASI_B, 450, 0 },
  { &POSE_ASI_A, 450, 100 },
  { &POSES_MANO[0], 900, 0 },
};

const PasoGesto PASOS_TAMBORILEAR[] = {
  { &POSE_TAP_INDICE,  300, 0 },
  { &POSE_TAP_CORAZON, 180, 0 },
  { &POSE_TAP_ANULAR,  180, 0 },
  { &POSE_TAP_INDICE,  180, 0 },
  { &POSE_TAP_CORAZON, 180, 0 },
  { &POSE_TAP_ANULAR,  180, 0 },
  { &POSES_MANO[0],    500, 0 },
};

const Gesto GESTOS[] = {
  { "Saludar",     PASOS_SALUDAR,    sizeof(PASOS_SALUDAR)    / sizeof(PASOS_SALUDAR[0])    },
  { "Ven aqui",    PASOS_VEN_AQUI,   sizeof(PASOS_VEN_AQUI)   / sizeof(PASOS_VEN_AQUI[0])   },
  { "Silencio",    PASOS_SILENCIO,   sizeof(PASOS_SILENCIO)   / sizeof(PASOS_SILENCIO[0])   },
  { "Asi asi",     PASOS_ASI_ASI,    sizeof(PASOS_ASI_ASI)    / sizeof(PASOS_ASI_ASI[0])    },
  { "Tamborilear", PASOS_TAMBORILEAR, sizeof(PASOS_TAMBORILEAR) / sizeof(PASOS_TAMBORILEAR[0]) },
};
const uint8_t NUM_GESTOS = sizeof(GESTOS) / sizeof(GESTOS[0]);

// ---------- Canal + servo de la mano ----------
struct CanalServoMano {
  Servo* servo;
  int limMin;
  int limMax;
  Canal c;
  int ultimo;
};

CanalServoMano canalesMano[NUM_SERVOS];
int objetivoMano[NUM_SERVOS];

// Micro-movimientos de los dedos en reposo (grados). 0 = desactivado.
const uint8_t MICRO_MANO_AMPLITUD = 4;

// ---------- Temporización (mano) ----------
const uint16_t TIEMPO_MIN_ENTRE_POSES_MS = 3000;
const uint16_t TIEMPO_MAX_ENTRE_POSES_MS = 7000;

// ---------- Estado (mano) ----------
unsigned long proximoCambioPose = 0;
unsigned long proximoMicroMano = 0;
uint8_t indicePoseAnteriorMano = 0;
uint8_t indiceGestoAnterior = 255;

const Gesto* gestoActual = NULL;
uint8_t pasoGestoActual = 0;
unsigned long tSiguientePasoGesto = 0;
unsigned long proximoBeatMano = 0;              // muñeca acompañando al hablar

// ======================================================================
// =========================== SETUP / LOOP =============================
// ======================================================================

void setup() {
  Serial.begin(9600);
  Serial1.begin(115200);    // enlace con el ESP32
  pinMode(19, INPUT_PULLUP); // RX1 en reposo si el ESP32 no está conectado (evita comandos fantasma por ruido)

  // --- Cuello: enganchar servos ---
  servoInclinacionIzquierda.attach(PIN_INCLINACION_IZQUIERDA);
  servoInclinacionDerecha.attach(PIN_INCLINACION_DERECHA);
  servoRotacion.attach(PIN_ROTACION);

  // --- Ojos: enganchar servos ---
  servoParpadoSupIzq.attach(PIN_PARPADO_SUP_IZQ);
  servoParpadoSupDer.attach(PIN_PARPADO_SUP_DER);
  servoParpadoInfIzq.attach(PIN_PARPADO_INF_IZQ);
  servoParpadoInfDer.attach(PIN_PARPADO_INF_DER);
  servoOjosVertical.attach(PIN_OJOS_VERTICAL);
  servoOjosHorizontal.attach(PIN_OJOS_HORIZONTAL);

  // --- Mano: enganchar servos ---
  servoPulgarFlexion.attach(PIN_PULGAR_FLEXION);
  servoIndice.attach(PIN_INDICE);
  servoCorazon.attach(PIN_CORAZON);
  servoAnularMenique.attach(PIN_ANULAR_MENIQUE);
  servoPulgarVertical.attach(PIN_PULGAR_VERTICAL);
  servoMuneca.attach(PIN_MUNECA);
  servoCodo.attach(PIN_CODO);

  randomSeed(analogRead(A0));

  Serial.println(F("=== Elver iniciado: cuello + ojos + mano ==="));

  unsigned long ahora = millis();

  // --- Cuello: límites (de sus poses) y posición inicial ---
  calcularLimitesCuello();
  aplicarPoseInstantaneaCuello(POSES_CUELLO[0]);
  poseActualCuello = POSES_CUELLO[0];
  imprimirPoseCuello(POSES_CUELLO[0]);
  proximoMovimientoCuello = ahora + random(TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS, TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS);

  // --- Ojos y mano ---
  iniciarOjos(ahora);
  iniciarMano(ahora);

  // --- Ánimo inicial ---
  proximoCambioAnimo = ahora + random(60000L, 180000L);
  Serial.print(F("Animo: "));
  Serial.println(perfil->nombre);
  mostrarAyuda();
}

void loop() {
  unsigned long ahora = millis();

  leerSerie(ahora);
  actualizarAnimo(ahora);
  actualizarConversacion(ahora);

  loopCuello(ahora);
  loopOjos(ahora);
  loopMano(ahora);
}

// ======================================================================
// ===================== FUNCIONES: UTILIDADES ==========================
// ======================================================================

int redondear(float x) {
  return (int)(x >= 0 ? x + 0.5f : x - 0.5f);
}

unsigned long escalar(unsigned long ms, float factor) {
  return (unsigned long)(ms * factor);
}

// Tiempo aleatorio sesgado hacia valores cortos: muchos tiempos cortos y
// de vez en cuando uno largo (más natural que un reparto uniforme).
unsigned long duracionAzar(unsigned long minMs, unsigned long maxMs, float factor) {
  float u = random(0, 1001) / 1000.0f;
  float d = minMs + (maxMs - minMs) * u * u;
  return (unsigned long)(d * factor);
}

// Número aleatorio con forma de campana (la mayoría cerca de la media).
float gauss(float media, float desviacion) {
  float suma = 0;
  for (uint8_t i = 0; i < 4; i++) {
    suma += random(0, 1001) / 1000.0f;
  }
  return media + (suma - 2.0f) * 1.73f * desviacion;
}

void fijarCanal(Canal &c, float v) {
  c.valor = v;
  c.desde = v;
  c.hasta = v;
  c.inicio = 0;
  c.dur = 1;
  c.activo = false;
}

void moverCanalConRetraso(Canal &c, float destino, unsigned long dur, unsigned long ahora, unsigned long retraso) {
  c.desde = c.valor;
  c.hasta = destino;
  c.inicio = ahora + retraso;
  c.dur = (dur < 1) ? 1 : dur;
  c.activo = true;
}

void moverCanal(Canal &c, float destino, unsigned long dur, unsigned long ahora) {
  moverCanalConRetraso(c, destino, dur, ahora, 0);
}

// Avanza el canal. Devuelve true si el valor ha cambiado.
bool actualizarCanal(Canal &c, unsigned long ahora) {
  if (!c.activo) {
    return false;
  }
  if ((long)(ahora - c.inicio) < 0) {
    return false;              // todavía en el retraso inicial
  }
  unsigned long t = ahora - c.inicio;
  float nuevo;
  if (t >= c.dur) {
    nuevo = c.hasta;
    c.activo = false;
  } else {
    float x = (float)t / (float)c.dur;
    float s = x * x * (3.0f - 2.0f * x);   // arranque y frenada suaves
    nuevo = c.desde + (c.hasta - c.desde) * s;
  }
  bool cambio = (nuevo != c.valor);
  c.valor = nuevo;
  return cambio;
}

void escribirSiCambia(Servo &servo, int &ultimo, int angulo) {
  if (angulo != ultimo) {
    servo.write(angulo);
    ultimo = angulo;
  }
}

// ======================================================================
// ============ FUNCIONES: ÁNIMO, SERIE, DORMIR Y DESPERTAR =============
// ======================================================================

void mostrarAyuda() {
  Serial.println(F("Comandos: 0 Tranquilo, 1 Curioso, 2 Somnoliento, 3 Alerta, 4 Timido"));
  Serial.println(F("          d dormir, a despertar, g gesto de mano, ? ayuda"));
  Serial.println(F("Gestos:   h saludar, v ven aqui, s silencio, m asi asi, t tamborilear"));
  Serial.println(F("Cuello:   C centro, I inclinar izq, D inclinar der, L girar izq, R girar der, N negar"));
  Serial.println(F("Mano:     n puno, b abierta, x paz, u pulgar arriba, w senalar, o OK, r relajada"));
  Serial.println(F("Otros:    j guino, q quieto, Q fin de quieto  |  E escuchando, H hablando, Z reposo"));
}

// Índice en POSES_MANO[] para cada letra de pose de mano (-1 si no es una pose)
int8_t indicePoseManoComando(char c) {
  switch (c) {
    case 'r': return 0;   // Mano relajada
    case 'n': return 1;   // Puno cerrado   (piedra)
    case 'b': return 2;   // Mano abierta   (papel)
    case 'x': return 3;   // Senal de paz   (tijera)
    case 'u': return 4;   // Pulgar arriba
    case 'w': return 5;   // Senalar
    case 'o': return 6;   // OK
  }
  return -1;
}

// Índice en GESTOS[] para cada letra de gesto (-1 si no es un gesto)
int8_t indiceGestoComando(char c) {
  switch (c) {
    case 'h': return 0;   // Saludar
    case 'v': return 1;   // Ven aqui
    case 's': return 2;   // Silencio
    case 'm': return 3;   // Asi asi
    case 't': return 4;   // Tamborilear
  }
  return -1;
}

// Índice en POSES_CUELLO[] para cada letra de cuello (-1 si no es del cuello)
int8_t indiceCuelloComando(char c) {
  switch (c) {
    case 'C': return 0;   // Centro
    case 'I': return 1;   // Inclinar poco a la izquierda
    case 'D': return 2;   // Inclinar poco a la derecha
    case 'L': return 3;   // Girar poco a la izquierda
    case 'R': return 4;   // Girar poco a la derecha
  }
  return -1;
}

// Ejecuta un comando. Devuelve true si se ha reconocido.
bool procesarComando(char c, unsigned long ahora) {
  int8_t g = indiceGestoComando(c);
  int8_t p = indiceCuelloComando(c);
  int8_t pm = indicePoseManoComando(c);

  if (c >= '0' && c < ('0' + NUM_ANIMOS)) {
    if (durmiendo) {
      Serial.println(F("Esta durmiendo. Usa 'a' para despertarlo."));
    } else {
      cambiarAnimo(c - '0', ahora);
    }
  } else if (c == 'd') {
    dormir(ahora);
  } else if (c == 'a') {
    despertar(ahora);
  } else if (c == 'g') {
    if (!durmiendo && gestoActual == NULL) {
      iniciarGestoAleatorio(ahora);
    }
  } else if (g >= 0) {
    if (!durmiendo) {
      iniciarGesto(g, ahora);
    }
  } else if (p >= 0) {
    if (!durmiendo) {
      poseCuelloPedida = p;
      numSecuenciaCuello = 0;            // cancela un "negar" en curso
      interrumpirCuello(ahora);          // sin esperar a que acabe el movimiento en curso
    }
  } else if (c == 'N') {
    if (!durmiendo) {
      iniciarNegar(ahora);
    }
  } else if (pm >= 0) {
    if (!durmiendo) {
      mostrarPoseManoPedida(pm, ahora);
    }
  } else if (c == 'j') {
    if (!durmiendo) {
      Serial.println(F("Guino"));
      if (faseParp == 0) {
        iniciarGuino(ahora);
      } else {
        guinoPedido = true;              // en cuanto acabe el parpadeo en curso
      }
    }
  } else if (c == 'q' || c == 'Q') {
    quieto = (c == 'q');
    Serial.println(quieto ? F("Modo quieto") : F("Fin del modo quieto"));
    if (!quieto) {
      reanudarVidaNormal(ahora);
    }
  } else if (c == 'E') {
    cambiarModoConversacion(CONV_ESCUCHANDO, ahora);
  } else if (c == 'H') {
    cambiarModoConversacion(CONV_HABLANDO, ahora);
  } else if (c == 'Z') {
    cambiarModoConversacion(CONV_REPOSO, ahora);
  } else if (c == '?') {
    mostrarAyuda();
  } else {
    return false;
  }
  return true;
}

void leerSerie(unsigned long ahora) {
  while (Serial.available() > 0) {
    procesarComando(Serial.read(), ahora);
  }
  // Comandos del ESP32 (asistente de voz)
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (procesarComando(c, ahora)) {
      Serial1.print(F("OK "));
      Serial1.println(c);
    }
  }
}

uint8_t elegirAnimo() {
  uint8_t nuevo = 0;
  do {
    uint8_t r = random(100);
    uint8_t acumulado = 0;
    for (uint8_t i = 0; i < NUM_ANIMOS; i++) {
      acumulado += PESO_ANIMO[i];
      if (r < acumulado) {
        nuevo = i;
        break;
      }
    }
  } while (nuevo == animoActual);
  return nuevo;
}

// Nivel de cierre "en reposo" de los párpados según hacia dónde mira y el ánimo.
float calcularParpBase(int vertical) {
  float caida = perfil->caidaParpado;
  if (vertical > OJOS_VERTICAL_CENTRO) {
    caida += (float)(vertical - OJOS_VERTICAL_CENTRO) * PARPADO_BAJADA_MIRADA_PCT / (float)(OJOS_VERTICAL_ABAJO - OJOS_VERTICAL_CENTRO);
  }
  if (modoConv == CONV_ESCUCHANDO) {
    caida -= CAIDA_PARPADO_ESCUCHANDO;     // prestando atención: ojos más abiertos
  }
  if (caida > 60) {
    caida = 60;
  }
  if (caida < 0) {
    caida = 0;
  }
  return caida;
}

void cambiarAnimo(uint8_t nuevo, unsigned long ahora) {
  animoActual = nuevo;
  perfil = &PERFILES[nuevo];
  proximoCambioAnimo = ahora + random(60000L, 180000L);

  parpBase = calcularParpBase(objetivoV);
  if (faseParp == 0) {
    moverParpadosABase(900, ahora);
  }
  proximoParpadeo = ahora + duracionAzar(PARPADEO_MIN_MS, PARPADEO_MAX_MS, factorParpadeoActual());

  Serial.print(F("Animo: "));
  Serial.println(perfil->nombre);
}

// Al hablar parpadea algo más que en reposo
float factorParpadeoActual() {
  if (modoConv == CONV_HABLANDO) {
    return perfil->factorParpadeo * FACTOR_PARPADEO_HABLANDO;
  }
  return perfil->factorParpadeo;
}

void actualizarAnimo(unsigned long ahora) {
  if (durmiendo) {
    // Despertar automático (solo si se durmió solo)
    if (despertarAutomatico && faseDespertar == 0 && ahora >= tDespertarAutomatico) {
      despertar(ahora);
    }
    return;
  }
  if (ahora >= proximoCambioAnimo) {
    if (SUENO_AUTOMATICO && animoActual == ANIMO_SOMNOLIENTO && random(100) < 40) {
      dormir(ahora);
      despertarAutomatico = true;
      tDespertarAutomatico = ahora + random(25000L, 50000L);
    } else {
      cambiarAnimo(elegirAnimo(), ahora);
    }
  }
}

void dormir(unsigned long ahora) {
  if (durmiendo) {
    return;
  }
  durmiendo = true;
  despertarAutomatico = false;
  faseDespertar = 0;
  modoConv = CONV_REPOSO;          // dormido no sigue la conversación
  numSecuenciaCuello = 0;
  poseCuelloPedida = -1;
  Serial.println(F("Durmiendo..."));

  // Ojos: se cierran despacio y quedan mirando algo hacia abajo
  faseParp = 0;
  reojoPendiente = false;
  miradaFija = false;
  parpBase = 100;
  moverCanal(cParpIzq, 100, 1600, ahora);
  moverCanal(cParpDer, 100, 1600, ahora);
  int v = OJOS_VERTICAL_CENTRO + 6;
  moverCanal(cVert, v, 1600, ahora);
  moverCanal(cHoriz, OJOS_HORIZONTAL_CENTRO, 1600, ahora);
  objetivoV = v;
  objetivoH = OJOS_HORIZONTAL_CENTRO;

  // Mano: se relaja despacio
  gestoActual = NULL;
  aplicarPoseMano(POSES_MANO[0], 2.5f, ahora);
  indicePoseAnteriorMano = 0;
}

void despertar(unsigned long ahora) {
  if (!durmiendo || faseDespertar != 0) {
    return;
  }
  faseDespertar = 1;
  tFaseDespertar = ahora;
  Serial.println(F("Despertando..."));
}

// Secuencia: entreabre los ojos, parpadeo lento, mira alrededor y queda somnoliento.
void actualizarDespertar(unsigned long ahora) {
  if (faseDespertar == 0 || ahora < tFaseDespertar) {
    return;
  }
  switch (faseDespertar) {
    case 1:
      moverCanal(cParpIzq, 55, 900, ahora);
      moverCanal(cParpDer, 55, 900, ahora);
      tFaseDespertar = ahora + 1400;
      faseDespertar = 2;
      break;
    case 2:
      moverCanal(cParpIzq, 90, 350, ahora);
      moverCanal(cParpDer, 90, 350, ahora);
      tFaseDespertar = ahora + 800;
      faseDespertar = 3;
      break;
    case 3: {
      moverCanal(cParpIzq, 45, 900, ahora);
      moverCanal(cParpDer, 45, 900, ahora);
      int v = redondear(gauss(OJOS_VERTICAL_CENTRO, 4));
      int h = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 5));
      v = constrain(v, OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO);
      h = constrain(h, OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA);
      moverCanal(cVert, v, 900, ahora);
      moverCanal(cHoriz, h, 900, ahora);
      objetivoV = v;
      objetivoH = h;
      tFaseDespertar = ahora + 1600;
      faseDespertar = 4;
      break;
    }
    case 4:
      durmiendo = false;
      faseDespertar = 0;
      despertarAutomatico = false;
      cambiarAnimo(ANIMO_SOMNOLIENTO, ahora);
      proximoCambioAnimo = ahora + 40000UL;      // un rato somnoliento y luego cambia solo
      proximaMirada = ahora + 800;
      proximoGuino = ahora + random(GUINO_MIN_MS, GUINO_MAX_MS);
      proximoCambioPose = ahora + 3000;
      proximoMovimientoCuello = ahora + random(TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS, TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS);
      Serial.println(F("Despierto."));
      break;
  }
}

// ======================================================================
// ================= FUNCIONES: CONVERSACIÓN Y ÓRDENES ===================
// ======================================================================

void cambiarModoConversacion(ModoConversacion nuevo, unsigned long ahora) {
  tUltimoAvisoConv = ahora;
  if (durmiendo || nuevo == modoConv) {
    return;                       // dormido no sigue la conversación
  }
  modoConv = nuevo;

  switch (nuevo) {
    case CONV_ESCUCHANDO:
      Serial.println(F("Conversacion -> Escuchando"));
      // Mira al frente, a quien le habla, con los ojos algo más abiertos
      miradaFija = false;
      reojoPendiente = false;
      iniciarSacada(redondear(gauss(OJOS_VERTICAL_CENTRO, 2)), redondear(gauss(OJOS_HORIZONTAL_CENTRO, 2)), false, ahora);
      proximaMirada = ahora + random(2500, 5000);
      if (!quieto && numSecuenciaCuello == 0 && ahora >= cuelloReservadoHasta) {
        cuelloEscuchaPendiente = true;
        interrumpirCuello(ahora);
      }
      break;

    case CONV_HABLANDO:
      Serial.println(F("Conversacion -> Hablando"));
      cuelloEscuchaPendiente = false;
      parpBase = calcularParpBase(objetivoV);
      if (faseParp == 0) {
        moverParpadosABase(400, ahora);
      }
      if (proximoMovimientoCuello > ahora + 900) {
        proximoMovimientoCuello = ahora + random(300, 900);
      }
      proximoBeatMano = ahora + random(600, 1500);
      break;

    case CONV_REPOSO:
      Serial.println(F("Conversacion -> Reposo"));
      parpBase = calcularParpBase(objetivoV);
      if (faseParp == 0) {
        moverParpadosABase(900, ahora);
      }
      reanudarVidaNormal(ahora);
      break;
  }
}

// Sin avisos del ESP32 durante un rato (desconectado): vuelve a su vida normal
void actualizarConversacion(unsigned long ahora) {
  if (modoConv != CONV_REPOSO && ahora - tUltimoAvisoConv > TIMEOUT_CONVERSACION_MS) {
    Serial.println(F("Sin avisos de la conversacion"));
    cambiarModoConversacion(CONV_REPOSO, ahora);
  }
}

// Tras escuchar/hablar o salir del modo quieto, retoma poco a poco lo automático
void reanudarVidaNormal(unsigned long ahora) {
  cuelloEscuchaPendiente = false;
  unsigned long t = ahora + random(1500, 4000);
  if (proximoMovimientoCuello < t) {
    proximoMovimientoCuello = t;
  }
  t = ahora + random(1500, 4000);
  if (proximoCambioPose < t) {
    proximoCambioPose = t;
  }
  // La muñeca vuelve a su sitio si estaba marcando el ritmo al hablar
  if (!manoMoviendose() && gestoActual == NULL) {
    moverCanal(canalesMano[M_MUNECA].c, objetivoMano[M_MUNECA], 500, ahora);
  }
}

// Pose de mano a la orden (solo poses de POSES_MANO: sus límites se aplican igual)
void mostrarPoseManoPedida(uint8_t i, unsigned long ahora) {
  gestoActual = NULL;              // sustituye a un gesto en curso
  Serial.print(F("Pose pedida -> "));
  Serial.println(POSES_MANO[i].nombre);
  unsigned long dur = aplicarPoseMano(POSES_MANO[i], perfil->factorVelMano, ahora);
  indicePoseAnteriorMano = i;
  imprimirPoseMano(POSES_MANO[i]);
  // La mantiene un rato antes de volver a lo automático (o de marcar el ritmo)
  manoReservadaHasta = ahora + dur + MANTENER_POSE_PEDIDA_MS;
  if (proximoCambioPose < manoReservadaHasta) {
    proximoCambioPose = manoReservadaHasta;
  }
  if (!quieto && random(100) < 50) {
    ojosMirarMano(ahora);
  }
}

// ======================================================================
// ======================= FUNCIONES: CUELLO =============================
// (Mismas poses, mismos ángulos, mismos 85 pasos de 40 ms y mismos tiempos
//  de espera en el movimiento automático. Lo nuevo (conversación, negar)
//  nunca sale del rango de las poses ni va más rápido que lo de siempre.)
// ======================================================================

// Rango de cada servo del cuello = el que cubren sus poses. Velocidad máxima =
// el mayor recorrido entre dos poses repartido en PASOS_MOVIMIENTO_SUAVE_CUELLO.
void calcularLimitesCuello() {
  limInclIzqMin = limInclIzqMax = POSES_CUELLO[0].inclinacionIzquierda;
  limInclDerMin = limInclDerMax = POSES_CUELLO[0].inclinacionDerecha;
  limRotMin = limRotMax = POSES_CUELLO[0].rotacion;
  int maxRecorrido = 0;
  for (uint8_t i = 0; i < NUM_POSES_CUELLO; i++) {
    const PoseCuello &p = POSES_CUELLO[i];
    limInclIzqMin = min(limInclIzqMin, p.inclinacionIzquierda);
    limInclIzqMax = max(limInclIzqMax, p.inclinacionIzquierda);
    limInclDerMin = min(limInclDerMin, p.inclinacionDerecha);
    limInclDerMax = max(limInclDerMax, p.inclinacionDerecha);
    limRotMin = min(limRotMin, p.rotacion);
    limRotMax = max(limRotMax, p.rotacion);
    for (uint8_t j = 0; j < NUM_POSES_CUELLO; j++) {
      maxRecorrido = max(maxRecorrido, recorridoCuello(p, POSES_CUELLO[j]));
    }
  }
  maxGradosPasoCuello = (float)maxRecorrido / PASOS_MOVIMIENTO_SUAVE_CUELLO;
}

// El mayor desplazamiento (en grados) de los tres servos entre dos posiciones
int recorridoCuello(const PoseCuello &a, const PoseCuello &b) {
  int d = abs(a.inclinacionIzquierda - b.inclinacionIzquierda);
  d = max(d, abs(a.inclinacionDerecha - b.inclinacionDerecha));
  d = max(d, abs(a.rotacion - b.rotacion));
  return d;
}

// Pasos necesarios para ir de "desde" a "hasta" sin superar la velocidad máxima.
// factor > 1 lo hace más lento todavía.
uint16_t pasosLimitadosCuello(const PoseCuello &desde, const PoseCuello &hasta, uint8_t factor) {
  const uint16_t PASOS_MIN = 8;
  uint16_t pasos = (uint16_t)(recorridoCuello(desde, hasta) / maxGradosPasoCuello + 0.999f) * factor;
  return (pasos < PASOS_MIN) ? PASOS_MIN : pasos;
}

// ÚNICO sitio que escribe en los servos del cuello: aplica SIEMPRE sus límites.
void escribirCuello(int izq, int der, int rot) {
  izq = constrain(izq, limInclIzqMin, limInclIzqMax);
  der = constrain(der, limInclDerMin, limInclDerMax);
  rot = constrain(rot, limRotMin, limRotMax);
  servoInclinacionIzquierda.write(izq);
  servoInclinacionDerecha.write(der);
  servoRotacion.write(rot);
  cuelloEscrito.inclinacionIzquierda = izq;
  cuelloEscrito.inclinacionDerecha = der;
  cuelloEscrito.rotacion = rot;
}

// Punto intermedio entre dos poses (f = 0 -> "base", f = 1 -> "hacia").
// Siempre queda dentro del rango de las poses.
PoseCuello mezclaCuello(const PoseCuello &base, const PoseCuello &hacia, float f) {
  PoseCuello p;
  p.nombre = "Conversacion";
  p.inclinacionIzquierda = redondear(base.inclinacionIzquierda + (hacia.inclinacionIzquierda - base.inclinacionIzquierda) * f);
  p.inclinacionDerecha = redondear(base.inclinacionDerecha + (hacia.inclinacionDerecha - base.inclinacionDerecha) * f);
  p.rotacion = redondear(base.rotacion + (hacia.rotacion - base.rotacion) * f);
  return p;
}

// Escuchando: al frente o ladeando un poco la cabeza (interés)
PoseCuello poseEscuchando() {
  float f = 0;
  uint8_t hacia = 1 + random(2);                 // inclinar izquierda o derecha
  if (random(100) < PROB_LADEAR_ESCUCHANDO) {
    f = random(10, (long)(FRACCION_CUELLO_ESCUCHANDO_MAX * 100) + 1) / 100.0f;
  }
  return mezclaCuello(POSES_CUELLO[0], POSES_CUELLO[hacia], f);
}

// Hablando: pequeños movimientos hacia una fracción de cualquier pose
PoseCuello poseHablando() {
  uint8_t hacia = random(1, NUM_POSES_CUELLO);
  float f = random((long)(FRACCION_CUELLO_HABLANDO_MIN * 100), (long)(FRACCION_CUELLO_HABLANDO_MAX * 100) + 1) / 100.0f;
  if (random(100) < 25) {
    f = random(0, 10) / 100.0f;                  // a veces vuelve casi al centro
  }
  return mezclaCuello(POSES_CUELLO[0], POSES_CUELLO[hacia], f);
}

void iniciarMovimientoCuello(const PoseCuello &destino, uint16_t pasos, TipoMovCuello tipo,
                             bool anticiparOjos, unsigned long ahora) {
  destinoCuello = destino;
  pasosMovCuello = (pasos < 1) ? 1 : pasos;
  tipoMovCuello = tipo;
  if (anticiparOjos) {
    // Los ojos se adelantan un instante hacia donde va a girar la cabeza
    ojosAnticiparCuello(destino.rotacion, ahora);
    tInicioCuello = ahora + ADELANTO_OJOS_MS;
  } else {
    tInicioCuello = ahora;
  }
  estadoCuello = CUELLO_PREPARANDO;
}

// Corta el movimiento en curso: el siguiente sale desde donde está ahora mismo
void interrumpirCuello(unsigned long ahora) {
  estadoCuello = CUELLO_ESPERA;
  proximoMovimientoCuello = ahora;
}

// Negar con la cabeza: giro a un lado y a otro alrededor de donde está,
// sin salir de los límites de giro y a la velocidad máxima de siempre.
void iniciarNegar(unsigned long ahora) {
  PoseCuello base = cuelloEscrito;
  base.nombre = "Negar";
  int centro = constrain(base.rotacion, limRotMin + AMPLITUD_NEGAR, limRotMax - AMPLITUD_NEGAR);
  int giros[] = { centro - AMPLITUD_NEGAR, centro + AMPLITUD_NEGAR, centro - AMPLITUD_NEGAR, centro };
  numSecuenciaCuello = 0;
  for (uint8_t i = 0; i < 4; i++) {
    secuenciaCuello[numSecuenciaCuello] = base;
    secuenciaCuello[numSecuenciaCuello].rotacion = giros[i];
    numSecuenciaCuello++;
  }
  idxSecuenciaCuello = 0;
  poseCuelloPedida = -1;
  cuelloEscuchaPendiente = false;
  cuelloReservadoHasta = ahora + 4000UL;         // que no lo pise la conversación
  Serial.println(F("Negar con la cabeza"));
  interrumpirCuello(ahora);
}

void loopCuello(unsigned long ahora) {
  switch (estadoCuello) {

    case CUELLO_ESPERA:
      if (durmiendo) {
        return;
      }
      if (numSecuenciaCuello > 0 && idxSecuenciaCuello >= numSecuenciaCuello) {
        numSecuenciaCuello = 0;                  // secuencia terminada
        idxSecuenciaCuello = 0;
      }

      if (poseCuelloPedida >= 0) {
        // Orden: una de las poses de siempre, con sus 85 pasos de siempre
        uint8_t i = poseCuelloPedida;
        poseCuelloPedida = -1;
        Serial.print(F("Nuevo movimiento de cuello -> "));
        Serial.println(POSES_CUELLO[i].nombre);
        indiceDestinoCuello = i;
        cuelloReservadoHasta = ahora + ADELANTO_OJOS_MS
                               + (unsigned long)PASOS_MOVIMIENTO_SUAVE_CUELLO * RETARDO_PASO_MS_CUELLO
                               + MANTENER_POSE_PEDIDA_MS;
        iniciarMovimientoCuello(POSES_CUELLO[i], PASOS_MOVIMIENTO_SUAVE_CUELLO, MOV_PEDIDO, true, ahora);

      } else if (idxSecuenciaCuello < numSecuenciaCuello) {
        const PoseCuello &p = secuenciaCuello[idxSecuenciaCuello++];
        iniciarMovimientoCuello(p, pasosLimitadosCuello(cuelloEscrito, p, 1), MOV_SECUENCIA, false, ahora);

      } else if (quieto || ahora < proximoMovimientoCuello || ahora < cuelloReservadoHasta) {
        return;

      } else if (modoConv == CONV_ESCUCHANDO) {
        // Se coloca una vez mirando a quien le habla y se queda quieto
        if (cuelloEscuchaPendiente) {
          cuelloEscuchaPendiente = false;
          PoseCuello p = poseEscuchando();
          iniciarMovimientoCuello(p, pasosLimitadosCuello(cuelloEscrito, p, 2), MOV_CONVERSACION, false, ahora);
        }

      } else if (modoConv == CONV_HABLANDO) {
        PoseCuello p = poseHablando();
        iniciarMovimientoCuello(p, pasosLimitadosCuello(cuelloEscrito, p, 2), MOV_CONVERSACION, false, ahora);

      } else {
        // Vida normal: el movimiento automático de siempre
        uint8_t i = elegirPoseDistintaCuello(indicePoseAnteriorCuello);
        Serial.print(F("Nuevo movimiento de cuello -> "));
        Serial.println(POSES_CUELLO[i].nombre);
        indiceDestinoCuello = i;
        iniciarMovimientoCuello(POSES_CUELLO[i], PASOS_MOVIMIENTO_SUAVE_CUELLO, MOV_AUTOMATICO, true, ahora);
      }
      break;

    case CUELLO_PREPARANDO:
      if (ahora >= tInicioCuello) {
        origenCuello = cuelloEscrito;            // sale desde donde está realmente
        pasoCuello = 0;
        tProximoPasoCuello = ahora;
        estadoCuello = CUELLO_MOVIENDO;
      }
      break;

    case CUELLO_MOVIENDO:
      if (ahora >= tProximoPasoCuello) {
        pasoCuello++;
        escribirCuello(map(pasoCuello, 0, pasosMovCuello, origenCuello.inclinacionIzquierda, destinoCuello.inclinacionIzquierda),
                       map(pasoCuello, 0, pasosMovCuello, origenCuello.inclinacionDerecha, destinoCuello.inclinacionDerecha),
                       map(pasoCuello, 0, pasosMovCuello, origenCuello.rotacion, destinoCuello.rotacion));
        tProximoPasoCuello = ahora + RETARDO_PASO_MS_CUELLO;

        bool movPose = (tipoMovCuello == MOV_AUTOMATICO || tipoMovCuello == MOV_PEDIDO);
        if (movPose && pasoCuello == PASO_CUELLO_RECENTRAR_OJOS) {
          ojosRecentrarConCabeza(ahora);
        }

        if (pasoCuello >= pasosMovCuello) {
          poseActualCuello = destinoCuello;
          if (movPose) {
            indicePoseAnteriorCuello = indiceDestinoCuello;
            imprimirPoseCuello(destinoCuello);
          }
          if (tipoMovCuello == MOV_CONVERSACION) {
            proximoMovimientoCuello = ahora + random(ESPERA_CUELLO_HABLANDO_MIN_MS, ESPERA_CUELLO_HABLANDO_MAX_MS);
          } else {
            proximoMovimientoCuello = ahora + random(TIEMPO_MIN_ENTRE_MOVIMIENTOS_MS, TIEMPO_MAX_ENTRE_MOVIMIENTOS_MS);
          }
          estadoCuello = CUELLO_ESPERA;
        }
      }
      break;
  }
}

void imprimirPoseCuello(const PoseCuello &pose) {
  if (!DEBUG_DETALLE) {
    return;
  }
  Serial.print(F("Pose de cuello aplicada: "));
  Serial.println(pose.nombre);
  Serial.print(F("  Inclinacion izquierda: ")); Serial.println(pose.inclinacionIzquierda);
  Serial.print(F("  Inclinacion derecha: ")); Serial.println(pose.inclinacionDerecha);
  Serial.print(F("  Rotacion: ")); Serial.println(pose.rotacion);
  Serial.println(F("----------------------------------"));
}

uint8_t elegirPoseDistintaCuello(uint8_t indiceAnterior) {
  uint8_t indiceNuevo;
  do {
    indiceNuevo = random(0, NUM_POSES_CUELLO);
  } while (indiceNuevo == indiceAnterior && NUM_POSES_CUELLO > 1);
  return indiceNuevo;
}

void aplicarPoseInstantaneaCuello(const PoseCuello &pose) {
  escribirCuello(pose.inclinacionIzquierda, pose.inclinacionDerecha, pose.rotacion);
}

// ======================================================================
// ======================== FUNCIONES: OJOS ===============================
// ======================================================================

void iniciarOjos(unsigned long ahora) {
  fijarCanal(cVert, OJOS_VERTICAL_CENTRO);
  fijarCanal(cHoriz, OJOS_HORIZONTAL_CENTRO);
  fijarCanal(cParpIzq, 0);
  fijarCanal(cParpDer, 0);
  objetivoV = OJOS_VERTICAL_CENTRO;
  objetivoH = OJOS_HORIZONTAL_CENTRO;
  parpBase = 0;
  escribirOjos();

  proximoParpadeo = ahora + duracionAzar(PARPADEO_MIN_MS, PARPADEO_MAX_MS, 1.0f);
  proximoGuino = ahora + random(GUINO_MIN_MS, GUINO_MAX_MS);
  proximaMirada = ahora + random(MIRADA_MIN_MS, MIRADA_MAX_MS);
  proximoMicro = ahora + random(700, 2200);
}

// Ángulo de un párpado según su % de cierre. Siempre entre "abierto" y "cerrado".
int anguloParpado(int abierto, int cerrado, float pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  return redondear(abierto + (cerrado - abierto) * pct / 100.0f);
}

// Escribe en los servos de los ojos (solo si el ángulo ha cambiado).
// Los límites se aplican SIEMPRE aquí, pase lo que pase antes.
void escribirOjos() {
  int v = redondear(cVert.valor);
  int h = redondear(cHoriz.valor);
  v = constrain(v, OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO);
  h = constrain(h, OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA);
  escribirSiCambia(servoOjosVertical, ultVertical, v);
  escribirSiCambia(servoOjosHorizontal, ultHorizontal, h);

  escribirSiCambia(servoParpadoSupIzq, ultSupIzq, anguloParpado(PARPADO_SUP_IZQ_ABIERTO, PARPADO_SUP_IZQ_CERRADO, cParpIzq.valor));
  escribirSiCambia(servoParpadoInfIzq, ultInfIzq, anguloParpado(PARPADO_INF_IZQ_ABIERTO, PARPADO_INF_IZQ_CERRADO, cParpIzq.valor));
  escribirSiCambia(servoParpadoSupDer, ultSupDer, anguloParpado(PARPADO_SUP_DER_ABIERTO, PARPADO_SUP_DER_CERRADO, cParpDer.valor));
  escribirSiCambia(servoParpadoInfDer, ultInfDer, anguloParpado(PARPADO_INF_DER_ABIERTO, PARPADO_INF_DER_CERRADO, cParpDer.valor));
}

void moverParpadosABase(unsigned long dur, unsigned long ahora) {
  moverCanal(cParpIzq, parpBase, dur, ahora);
  moverCanal(cParpDer, parpBase, dur, ahora);
}

// ---------- Gestos de párpados (parpadeos y guiño) ----------

void iniciarGestoParpados(bool izq, bool der, unsigned long durCierre, unsigned long mantener,
                          unsigned long durApertura, uint8_t repeticiones, unsigned long pausa,
                          unsigned long ahora) {
  gestoIzq = izq;
  gestoDer = der;
  durCierreParp = durCierre;
  mantenerParp = mantener;
  durAperturaParp = durApertura;
  repeticionesParp = repeticiones;
  pausaParp = pausa;
  faseParp = 1;
  if (izq) {
    moverCanal(cParpIzq, 100, durCierre, ahora);
  }
  if (der) {
    moverCanal(cParpDer, 100, durCierre, ahora);
  }
}

bool parpadosMoviendose() {
  return (gestoIzq && cParpIzq.activo) || (gestoDer && cParpDer.activo);
}

void actualizarGestoParpados(unsigned long ahora) {
  switch (faseParp) {
    case 1:   // cerrando
      if (!parpadosMoviendose()) {
        faseParp = 2;
        finFaseParp = ahora + mantenerParp;
      }
      break;
    case 2:   // cerrado un instante
      if (ahora >= finFaseParp) {
        if (gestoIzq) {
          moverCanal(cParpIzq, parpBase, durAperturaParp, ahora);
        }
        if (gestoDer) {
          moverCanal(cParpDer, parpBase, durAperturaParp, ahora);
        }
        faseParp = 3;
      }
      break;
    case 3:   // abriendo
      if (!parpadosMoviendose()) {
        if (repeticionesParp > 1) {
          repeticionesParp--;
          faseParp = 4;
          finFaseParp = ahora + pausaParp;
        } else {
          faseParp = 0;
        }
      }
      break;
    case 4:   // pausa entre parpadeos (parpadeo doble)
      if (ahora >= finFaseParp) {
        if (gestoIzq) {
          moverCanal(cParpIzq, 100, durCierreParp, ahora);
        }
        if (gestoDer) {
          moverCanal(cParpDer, 100, durCierreParp, ahora);
        }
        faseParp = 1;
      }
      break;
  }
}

// Parpadeo normal (rápido), doble o lento, según el ánimo.
void iniciarParpadeoNatural(unsigned long ahora) {
  uint8_t r = random(100);
  if (r < PROB_PARPADEO_DOBLE) {
    iniciarGestoParpados(true, true, 70, random(30, 60), 110, 2, random(120, 220), ahora);
  } else if (r < PROB_PARPADEO_DOBLE + perfil->probLento) {
    iniciarGestoParpados(true, true, 220, random(150, 300), 260, 1, 0, ahora);
  } else {
    iniciarGestoParpados(true, true, 70, random(40, 90), 110, 1, 0, ahora);
  }
}

// Guiño: cierra solo un ojo (izquierdo o derecho al azar) un momento.
void iniciarGuino(unsigned long ahora) {
  bool ojoIzquierdo = (random(2) == 0);
  iniciarGestoParpados(ojoIzquierdo, !ojoIzquierdo, 90, random(300, 500), 160, 1, 0, ahora);
}

// ---------- Mirada ----------

int amplitudMirada(int v, int h) {
  int dv = abs(v - redondear(cVert.valor));
  int dh = abs(h - redondear(cHoriz.valor));
  return (dv > dh) ? dv : dh;
}

// Movimiento rápido y suave hacia un punto. Respeta siempre los límites.
void iniciarSacada(int v, int h, bool conParpadeo, unsigned long ahora) {
  v = constrain(v, OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO);
  h = constrain(h, OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA);

  int amplitud = amplitudMirada(v, h);
  unsigned long dur = escalar(70 + amplitud * 3, perfil->factorVelOjos);
  moverCanal(cVert, v, dur, ahora);
  moverCanal(cHoriz, h, dur, ahora);
  objetivoV = v;
  objetivoH = h;

  parpBase = calcularParpBase(v);
  if (faseParp == 0) {
    if (conParpadeo) {
      iniciarGestoParpados(true, true, 60, random(30, 60), 100, 1, 0, ahora);
    } else {
      moverParpadosABase(dur + 100, ahora);
    }
  }
}

// Elige a dónde mirar y cuánto tiempo se queda ahí.
void nuevaMirada(unsigned long ahora) {
  int v;
  int h;
  unsigned long fijacion;
  bool esReojo = false;
  int centroV = OJOS_VERTICAL_CENTRO + perfil->sesgoVertical;

  if (modoConv == CONV_ESCUCHANDO || quieto) {
    // Atento: mira al frente (a quien le habla) con pequeñas correcciones
    reojoPendiente = false;
    miradaFija = false;
    esReojo = true;                              // sin parpadeo asociado al cambio
    v = redondear(gauss(centroV, 2));
    h = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 3));
    fijacion = random(2500, 5000);
  } else if (modoConv == CONV_HABLANDO && !reojoPendiente) {
    miradaFija = false;
    if (random(100) < PROB_APARTAR_MIRADA_HABLANDO) {
      // Aparta la mirada un momento (arriba y a un lado, como pensando) y vuelve
      esReojo = true;
      reojoRetornoV = redondear(gauss(centroV, 2));
      reojoRetornoH = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 3));
      reojoPendiente = true;
      v = random(OJOS_VERTICAL_ARRIBA, centroV);
      h = (random(2) == 0) ? random(OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_CENTRO - 4)
                           : random(OJOS_HORIZONTAL_CENTRO + 4, OJOS_HORIZONTAL_DERECHA + 1);
      fijacion = random(700, 1500);
    } else {
      // Mira a quien le escucha
      v = redondear(gauss(centroV, 3));
      h = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 4));
      fijacion = duracionAzar(MIRADA_MIN_MS, MIRADA_MAX_MS, perfil->factorFijacion);
    }
  } else if (reojoPendiente) {
    // Vuelve al punto que miraba antes del vistazo de reojo
    v = reojoRetornoV;
    h = reojoRetornoH;
    reojoPendiente = false;
    miradaFija = false;
    fijacion = duracionAzar(MIRADA_MIN_MS, MIRADA_MAX_MS, perfil->factorFijacion);
  } else {
    uint8_t r = random(100);

    if (r < perfil->probReojo) {
      // Vistazo rápido de reojo a un lado, y luego vuelve
      esReojo = true;
      h = (random(2) == 0) ? OJOS_HORIZONTAL_IZQUIERDA : OJOS_HORIZONTAL_DERECHA;
      v = objetivoV + random(-3, 4);
      reojoRetornoV = objetivoV;
      reojoRetornoH = objetivoH;
      reojoPendiente = true;
      miradaFija = false;
      fijacion = random(600, 1300);
    } else if (r < perfil->probReojo + perfil->probAmplia) {
      // Mirada amplia a cualquier punto permitido
      v = random(OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO + 1);
      h = random(OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA + 1);
    } else if (random(100) < 45) {
      // Vuelve cerca del centro (mirada "en reposo"), con la campana de Gauss
      v = redondear(gauss(centroV, 4));
      h = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 5));
    } else {
      // Pequeño desplazamiento desde donde está mirando
      v = redondear(gauss(objetivoV, 6));
      h = redondear(gauss(objetivoH, 8));
    }

    if (!esReojo) {
      if (random(100) < perfil->probFija) {
        miradaFija = true;
        fijacion = random(MIRADA_FIJA_MIN_MS, MIRADA_FIJA_MAX_MS);
      } else {
        miradaFija = false;
        fijacion = duracionAzar(MIRADA_MIN_MS, MIRADA_MAX_MS, perfil->factorFijacion);
      }
    }
  }

  // Nunca salir de los límites
  v = constrain(v, OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO);
  h = constrain(h, OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA);

  // En cambios grandes de mirada a veces se parpadea a la vez
  bool conParpadeo = (!esReojo && faseParp == 0 && amplitudMirada(v, h) >= 12 && random(100) < PROB_PARPADEO_CON_MIRADA);

  iniciarSacada(v, h, conParpadeo, ahora);

  if (conParpadeo) {
    proximoParpadeo = ahora + duracionAzar(PARPADEO_MIN_MS, PARPADEO_MAX_MS, perfil->factorParpadeo);
  }
  proximaMirada = ahora + fijacion;
  proximoMicro = ahora + random(700, 2200);
}

// ---------- Coordinación con cuello y mano ----------

// Antes de que el cuello gire, los ojos ya miran hacia allí.
void ojosAnticiparCuello(int rotacionDestino, unsigned long ahora) {
  if (durmiendo) {
    return;
  }
  int h;
  if (rotacionDestino < NEUTRO) {
    h = OJOS_HORIZONTAL_IZQUIERDA + 2;
  } else if (rotacionDestino > NEUTRO) {
    h = OJOS_HORIZONTAL_DERECHA - 2;
  } else {
    return;                              // sin giro: los ojos siguen a lo suyo
  }
  miradaFija = false;
  reojoPendiente = false;
  iniciarSacada(objetivoV, h, false, ahora);
  proximaMirada = ahora + 4500;          // se mantienen ahí hasta que el cuello los recentre
}

// Cuando la cabeza ya va llegando, los ojos vuelven cerca del centro.
void ojosRecentrarConCabeza(unsigned long ahora) {
  if (durmiendo) {
    return;
  }
  int v = redondear(gauss(OJOS_VERTICAL_CENTRO + perfil->sesgoVertical, 3));
  int h = redondear(gauss(OJOS_HORIZONTAL_CENTRO, 3));
  miradaFija = false;
  reojoPendiente = false;
  iniciarSacada(v, h, false, ahora);
  proximaMirada = ahora + duracionAzar(MIRADA_MIN_MS, MIRADA_MAX_MS, perfil->factorFijacion);
}

// Los ojos echan un vistazo a la mano cuando hace un gesto.
void ojosMirarMano(unsigned long ahora) {
  if (durmiendo) {
    return;
  }
  miradaFija = false;
  reojoPendiente = false;
  iniciarSacada(MIRAR_MANO_VERTICAL, MIRAR_MANO_HORIZONTAL, false, ahora);
  proximaMirada = ahora + random(1500, 2800);
}

// ---------- Bucle de los ojos ----------

void loopOjos(unsigned long ahora) {
  // Mueve todos los canales
  actualizarCanal(cVert, ahora);
  actualizarCanal(cHoriz, ahora);
  actualizarCanal(cParpIzq, ahora);
  actualizarCanal(cParpDer, ahora);

  if (durmiendo) {
    actualizarDespertar(ahora);
    escribirOjos();
    return;
  }

  actualizarGestoParpados(ahora);

  // Guiño pedido (aunque esté somnoliento), guiño espontáneo (raro) o parpadeo
  if (faseParp == 0) {
    if (guinoPedido) {
      guinoPedido = false;
      iniciarGuino(ahora);
    } else if (ahora >= proximoGuino) {
      if (animoActual != ANIMO_SOMNOLIENTO) {
        iniciarGuino(ahora);
      }
      proximoGuino = ahora + random(GUINO_MIN_MS, GUINO_MAX_MS);
      proximoParpadeo = ahora + duracionAzar(PARPADEO_MIN_MS, PARPADEO_MAX_MS, factorParpadeoActual());
    } else if (ahora >= proximoParpadeo) {
      iniciarParpadeoNatural(ahora);
      proximoParpadeo = ahora + duracionAzar(PARPADEO_MIN_MS, PARPADEO_MAX_MS, factorParpadeoActual());
    }
  }

  // Cambio de mirada o micro-movimientos mientras fija un punto
  if (ahora >= proximaMirada) {
    nuevaMirada(ahora);
  } else if (MICRO_AMPLITUD > 0 && !miradaFija && !reojoPendiente && ahora >= proximoMicro
             && !cVert.activo && !cHoriz.activo) {
    int v = objetivoV + random(-(int)MICRO_AMPLITUD, (int)MICRO_AMPLITUD + 1);
    int h = objetivoH + random(-(int)MICRO_AMPLITUD, (int)MICRO_AMPLITUD + 1);
    v = constrain(v, OJOS_VERTICAL_ARRIBA, OJOS_VERTICAL_ABAJO);
    h = constrain(h, OJOS_HORIZONTAL_IZQUIERDA, OJOS_HORIZONTAL_DERECHA);
    moverCanal(cVert, v, 120, ahora);
    moverCanal(cHoriz, h, 120, ahora);
    proximoMicro = ahora + random(700, 2200);
  }

  escribirOjos();
}

// ======================================================================
// ======================== FUNCIONES: MANO ===============================
// ======================================================================

void iniciarMano(unsigned long ahora) {
  canalesMano[M_PULGAR_FLEX]  = { &servoPulgarFlexion,  LIM_MIN_PULGAR_FLEX,     LIM_MAX_PULGAR_FLEX,     {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_INDICE]       = { &servoIndice,         LIM_MIN_INDICE,          LIM_MAX_INDICE,          {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_CORAZON]      = { &servoCorazon,        LIM_MIN_CORAZON,         LIM_MAX_CORAZON,         {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_ANULAR]       = { &servoAnularMenique,  LIM_MIN_ANULAR_MENIQUE,  LIM_MAX_ANULAR_MENIQUE,  {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_PULGAR_VERT]  = { &servoPulgarVertical, LIM_MIN_PULGAR_VERTICAL, LIM_MAX_PULGAR_VERTICAL, {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_MUNECA]       = { &servoMuneca,         LIM_MIN_MUNECA,          LIM_MAX_MUNECA,          {0, 0, 0, 0, 1, false}, -1 };
  canalesMano[M_CODO]         = { &servoCodo,           LIM_MIN_CODO,            LIM_MAX_CODO,            {0, 0, 0, 0, 1, false}, -1 };

  aplicarPoseInstantaneaMano(POSES_MANO[0]);
  imprimirPoseMano(POSES_MANO[0]);

  proximoCambioPose = ahora + random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS);
  proximoMicroMano = ahora + random(4000, 9000);
}

void imprimirPoseMano(const PoseMano &pose) {
  if (!DEBUG_DETALLE) {
    return;
  }
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

uint8_t elegirPoseDistintaMano(uint8_t indiceAnterior) {
  uint8_t indiceNuevo;
  do {
    indiceNuevo = random(0, NUM_POSES_MANO);
  } while (indiceNuevo == indiceAnterior && NUM_POSES_MANO > 1);
  return indiceNuevo;
}

// Pose de la mano a un array ordenado según los índices M_*
void poseAArray(const PoseMano &p, int destino[]) {
  destino[M_PULGAR_FLEX] = p.pulgarFlexion;
  destino[M_INDICE]      = p.indice;
  destino[M_CORAZON]     = p.corazon;
  destino[M_ANULAR]      = p.anularMenique;
  destino[M_PULGAR_VERT] = p.pulgarVertical;
  destino[M_MUNECA]      = p.muneca;
  destino[M_CODO]        = p.codo;
}

// Coloca la mano en una pose al instante (solo al arrancar).
void aplicarPoseInstantaneaMano(const PoseMano &pose) {
  int destino[NUM_SERVOS];
  poseAArray(pose, destino);
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    int a = constrain(destino[i], canalesMano[i].limMin, canalesMano[i].limMax);
    fijarCanal(canalesMano[i].c, a);
    canalesMano[i].servo->write(a);
    canalesMano[i].ultimo = a;
    objetivoMano[i] = a;
  }
}

// Transición natural a una pose: brazo primero, muñeca después y los dedos en cascada
// (al cerrar: meñique -> índice -> pulgar; al abrir: índice -> meñique). El pulgar vertical
// va siempre despacio. Devuelve cuánto tarda en total (ms).
unsigned long aplicarPoseMano(const PoseMano &p, float factorVel, unsigned long ahora) {
  static const uint16_t DUR_BASE[NUM_SERVOS] = { 550, 500, 500, 500, 1100, 750, 1000 };

  int destino[NUM_SERVOS];
  poseAArray(p, destino);

  float sumaActual = canalesMano[M_INDICE].c.valor + canalesMano[M_CORAZON].c.valor + canalesMano[M_ANULAR].c.valor;
  float sumaDestino = destino[M_INDICE] + destino[M_CORAZON] + destino[M_ANULAR];
  bool cerrando = (sumaDestino < sumaActual);

  uint16_t retraso[NUM_SERVOS];
  retraso[M_CODO] = 0;
  retraso[M_MUNECA] = 60;
  retraso[M_PULGAR_VERT] = 120;
  if (cerrando) {
    retraso[M_ANULAR] = 100;
    retraso[M_CORAZON] = 150;
    retraso[M_INDICE] = 200;
    retraso[M_PULGAR_FLEX] = 260;
  } else {
    retraso[M_INDICE] = 100;
    retraso[M_CORAZON] = 150;
    retraso[M_ANULAR] = 200;
    retraso[M_PULGAR_FLEX] = 120;
  }

  unsigned long fin = 0;
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    int a = constrain(destino[i], canalesMano[i].limMin, canalesMano[i].limMax);
    unsigned long dur = escalar(DUR_BASE[i], factorVel);
    unsigned long ret = escalar(retraso[i], factorVel) + random(0, 30);
    moverCanalConRetraso(canalesMano[i].c, a, dur, ahora, ret);
    objetivoMano[i] = a;
    if (ret + dur > fin) {
      fin = ret + dur;
    }
  }
  return fin;
}

// Paso de un gesto: todos los servos con la misma duración (el pulgar vertical nunca
// baja de 600 ms para protegerlo).
void aplicarPasoGesto(const PoseMano &p, unsigned long durMs, unsigned long ahora) {
  int destino[NUM_SERVOS];
  poseAArray(p, destino);
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    int a = constrain(destino[i], canalesMano[i].limMin, canalesMano[i].limMax);
    unsigned long dur = durMs;
    if (i == M_PULGAR_VERT && dur < 600) {
      dur = 600;
    }
    moverCanal(canalesMano[i].c, a, dur, ahora);
    objetivoMano[i] = a;
  }
}

// Escribe en los servos de la mano. Los límites de seguridad se aplican SIEMPRE aquí.
void actualizarCanalesMano(unsigned long ahora) {
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    CanalServoMano &cs = canalesMano[i];
    if (actualizarCanal(cs.c, ahora)) {
      int a = redondear(cs.c.valor);
      a = constrain(a, cs.limMin, cs.limMax);
      if (a != cs.ultimo) {
        cs.servo->write(a);
        cs.ultimo = a;
      }
    }
  }
}

bool manoMoviendose() {
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    if (canalesMano[i].c.activo) {
      return true;
    }
  }
  return false;
}

// ---------- Gestos ----------

void ejecutarPasoGesto(unsigned long ahora) {
  const PasoGesto &paso = gestoActual->pasos[pasoGestoActual];
  aplicarPasoGesto(*paso.pose, paso.durMs, ahora);
  tSiguientePasoGesto = ahora + paso.durMs + paso.pausaMs;
}

void iniciarGestoAleatorio(unsigned long ahora) {
  uint8_t i;
  do {
    i = random(0, NUM_GESTOS);
  } while (i == indiceGestoAnterior && NUM_GESTOS > 1);
  iniciarGesto(i, ahora);
}

// Lanza un gesto concreto (si ya había uno en curso, lo sustituye)
void iniciarGesto(uint8_t i, unsigned long ahora) {
  indiceGestoAnterior = i;

  Serial.print(F("Gesto -> "));
  Serial.println(GESTOS[i].nombre);

  if (random(100) < 50) {
    ojosMirarMano(ahora);
  }

  gestoActual = &GESTOS[i];
  pasoGestoActual = 0;
  ejecutarPasoGesto(ahora);
}

void actualizarGesto(unsigned long ahora) {
  if (ahora < tSiguientePasoGesto) {
    return;
  }
  pasoGestoActual++;
  if (pasoGestoActual >= gestoActual->numPasos) {
    gestoActual = NULL;
    indicePoseAnteriorMano = 0;    // el gesto termina en "Mano relajada"
    proximoCambioPose = ahora + escalar(random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS), perfil->factorPoses);
  } else {
    ejecutarPasoGesto(ahora);
  }
}

// ---------- Micro-movimientos de los dedos en reposo ----------

void microMovimientoMano(unsigned long ahora) {
  if (MICRO_MANO_AMPLITUD == 0 || ahora < proximoMicroMano) {
    return;
  }
  proximoMicroMano = ahora + random(4000, 9000);
  if (manoMoviendose()) {
    return;
  }
  uint8_t i = random(M_INDICE, M_ANULAR + 1);      // solo índice, corazón o anular
  int v = objetivoMano[i] + random(-(int)MICRO_MANO_AMPLITUD, (int)MICRO_MANO_AMPLITUD + 1);
  v = constrain(v, canalesMano[i].limMin, canalesMano[i].limMax);
  moverCanal(canalesMano[i].c, v, 900, ahora);
}

// ---------- Ritmo de la muñeca al hablar ----------

// Pequeño movimiento de la muñeca alrededor de su pose actual (nunca la deja
// "a la deriva": siempre se calcula desde el objetivo de la pose).
void beatMano(unsigned long ahora) {
  if (ahora < proximoBeatMano) {
    return;
  }
  proximoBeatMano = ahora + random(700, 1800);
  if (ahora < manoReservadaHasta || manoMoviendose()) {
    return;                        // no estropea una pose pedida ni un movimiento
  }
  CanalServoMano &m = canalesMano[M_MUNECA];
  int v = objetivoMano[M_MUNECA] + random(-(int)AMPLITUD_BEAT_MUNECA, (int)AMPLITUD_BEAT_MUNECA + 1);
  v = constrain(v, m.limMin, m.limMax);
  moverCanal(m.c, v, random(350, 600), ahora);
}

// ---------- Bucle de la mano ----------

void loopMano(unsigned long ahora) {
  actualizarCanalesMano(ahora);

  if (gestoActual != NULL) {
    actualizarGesto(ahora);
    return;
  }
  if (durmiendo) {
    return;
  }

  // Hablando: la muñeca acompaña con pequeños golpes de ritmo
  if (modoConv == CONV_HABLANDO && !quieto) {
    beatMano(ahora);
  }

  // Poses y gestos al azar solo en su vida normal (no al escuchar, hablar o quieto)
  bool vidaNormal = (modoConv == CONV_REPOSO && !quieto);
  if (vidaNormal && ahora >= proximoCambioPose) {
    if (random(100) < perfil->probGesto) {
      iniciarGestoAleatorio(ahora);
    } else {
      uint8_t indiceNuevaPose = elegirPoseDistintaMano(indicePoseAnteriorMano);
      Serial.print(F("Nueva pose -> "));
      Serial.println(POSES_MANO[indiceNuevaPose].nombre);

      unsigned long duracion = aplicarPoseMano(POSES_MANO[indiceNuevaPose], perfil->factorVelMano, ahora);
      indicePoseAnteriorMano = indiceNuevaPose;
      imprimirPoseMano(POSES_MANO[indiceNuevaPose]);

      proximoCambioPose = ahora + duracion + escalar(random(TIEMPO_MIN_ENTRE_POSES_MS, TIEMPO_MAX_ENTRE_POSES_MS), perfil->factorPoses);
    }
  }

  microMovimientoMano(ahora);
}
