/*
  ojos-calibracion.ino
  Herramienta para calibrar los límites de cada servo de los ojos (Arduino Mega 2560).
  Permite mover un servo concreto a un ángulo concreto desde el Monitor Serie,
  para encontrar visualmente el mínimo y máximo real de cada mecanismo
  (párpados abiertos/cerrados, límites de mirada arriba/abajo/izquierda/derecha).

  USO:
  1. Sube este programa y abre el Monitor Serie a 9600 baudios (fin de línea: "Nueva línea").
  2. Escribe el número de servo (1-6) y pulsa Enter para seleccionarlo.
  3. Escribe un ángulo (0-180) y pulsa Enter para mover el servo seleccionado a ese ángulo.
  4. Anota los ángulos que te convenzan como límites y llévalos a ojos-automatic.ino.

  Servos:
  1 -> pin 39 -> Párpado arriba izquierdo
  2 -> pin 40 -> Párpado arriba derecho
  3 -> pin 41 -> Párpado abajo izquierdo
  4 -> pin 42 -> Párpado abajo derecho
  5 -> pin 43 -> Movimiento vertical de ojos (ambos)
  6 -> pin 44 -> Movimiento horizontal de ojos (ambos)
*/

#include <Servo.h>

const uint8_t NUM_SERVOS = 6;
const uint8_t PINES[NUM_SERVOS] = {39, 40, 41, 42, 43, 44};
const char* NOMBRES[NUM_SERVOS] = {
  "Parpado arriba izquierdo",
  "Parpado arriba derecho",
  "Parpado abajo izquierdo",
  "Parpado abajo derecho",
  "Movimiento vertical (arriba/abajo)",
  "Movimiento horizontal (izquierda/derecha)"
};

Servo servos[NUM_SERVOS];
const int ANGULO_INICIAL = 90;
int servoSeleccionado = -1; // -1 = ninguno seleccionado todavía

void setup() {
  Serial.begin(9600);
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(PINES[i]);
    servos[i].write(ANGULO_INICIAL);
  }

  Serial.println(F("=== Calibracion de servos de ojos ==="));
  mostrarMenu();
}

void loop() {
  if (Serial.available() > 0) {
    int valor = Serial.parseInt();
    // parseInt descarta caracteres no numéricos; limpiamos el resto de la línea (\n, etc.)
    while (Serial.available() > 0) {
      Serial.read();
    }

    if (servoSeleccionado == -1) {
      seleccionarServo(valor);
    } else {
      moverServoSeleccionado(valor);
    }
  }
}

void mostrarMenu() {
  Serial.println();
  Serial.println(F("Selecciona un servo (1-6):"));
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    Serial.print(i + 1);
    Serial.print(F(" -> pin "));
    Serial.print(PINES[i]);
    Serial.print(F(" -> "));
    Serial.println(NOMBRES[i]);
  }
}

void seleccionarServo(int valor) {
  if (valor < 1 || valor > NUM_SERVOS) {
    Serial.println(F("Numero de servo invalido. Intenta de nuevo."));
    return;
  }

  servoSeleccionado = valor - 1;
  Serial.print(F("Servo seleccionado: "));
  Serial.print(valor);
  Serial.print(F(" ("));
  Serial.print(NOMBRES[servoSeleccionado]);
  Serial.println(F(")"));
  Serial.println(F("Escribe un angulo (0-180) para moverlo. Escribe 999 para volver al menu."));
}

void moverServoSeleccionado(int valor) {
  if (valor == 999) {
    servoSeleccionado = -1;
    mostrarMenu();
    return;
  }

  if (valor < 0 || valor > 180) {
    Serial.println(F("Angulo invalido. Debe estar entre 0 y 180 (o 999 para volver)."));
    return;
  }

  servos[servoSeleccionado].write(valor);
  Serial.print(F("Angulo aplicado: "));
  Serial.println(valor);
}
