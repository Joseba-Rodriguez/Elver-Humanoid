#include <Servo.h>

// ---------- SERVOS ----------
Servo servoDedo1; // Pulgar          - pin 8
Servo servoDedo2; // Índice          - pin 9
Servo servoDedo3; // Corazón         - pin 10
Servo servoDedo4; // Meñique+Anular  - pin 11
Servo servoMuneca; // Muñeca         - pin 12
Servo servoCodo;   // Codo           - pin 13

// ---------- PINES ANALÓGICOS ----------
const int PIN_FLEX1 = A0;
const int PIN_FLEX2 = A1;
const int PIN_FLEX3 = A2;
const int PIN_FLEX4 = A3;
const int PIN_POT_MUNECA = A4;
const int PIN_POT_CODO   = A5;

// ---------- CALIBRACIÓN INDIVIDUAL (con guante puesto) ----------
const int DEDO1_CERRADO = 708, DEDO1_ABIERTO = 722;
const int DEDO2_CERRADO = 731, DEDO2_ABIERTO = 739;
const int DEDO3_CERRADO = 734, DEDO3_ABIERTO = 745;
const int DEDO4_CERRADO = 710, DEDO4_ABIERTO = 717;

// ---------- ZONA MUERTA INDIVIDUAL POR DEDO ----------
const int DEADBAND_DEDO1 = 4;
const int DEADBAND_DEDO2 = 7;
const int DEADBAND_DEDO3 = 7;
const int DEADBAND_DEDO4 = 8;

// ---------- MUESTRAS PARA SUAVIZADO ----------
const int MUESTRAS = 30;

int ultimoAngulo1 = 0, ultimoAngulo2 = 0, ultimoAngulo3 = 0, ultimoAngulo4 = 0;

void setup()
{
  pinMode(PIN_FLEX1, INPUT);
  pinMode(PIN_FLEX2, INPUT);
  pinMode(PIN_FLEX3, INPUT);
  pinMode(PIN_FLEX4, INPUT);
  pinMode(PIN_POT_MUNECA, INPUT);
  pinMode(PIN_POT_CODO, INPUT);

  servoDedo1.attach(8, 500, 2500);
  servoDedo2.attach(9, 500, 2500);
  servoDedo3.attach(10, 500, 2500);
  servoDedo4.attach(11, 500, 2500);
  servoMuneca.attach(12, 500, 2500);
  servoCodo.attach(13, 500, 2500);

  Serial.begin(9600);
}

int leerSuavizado(int pin)
{
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(pin);
    delayMicroseconds(200);
  }
  return suma / MUESTRAS;
}

int calcularAngulo(int pin, int valCerrado, int valAbierto)
{
  int valor = leerSuavizado(pin);
  valor = constrain(valor, valCerrado, valAbierto);
  int angulo = map(valor, valCerrado, valAbierto, 0, 180);
  return angulo;
}

void moverSiCambia(Servo &servo, int nuevoAngulo, int &ultimoAngulo, int deadband)
{
  if (abs(nuevoAngulo - ultimoAngulo) >= deadband) {
    servo.write(nuevoAngulo);
    ultimoAngulo = nuevoAngulo;
  }
}

void loop()
{
  // ----- DEDOS -----
  int angDedo1 = calcularAngulo(PIN_FLEX1, DEDO1_CERRADO, DEDO1_ABIERTO);
  int angDedo2 = calcularAngulo(PIN_FLEX2, DEDO2_CERRADO, DEDO2_ABIERTO);
  int angDedo3 = calcularAngulo(PIN_FLEX3, DEDO3_CERRADO, DEDO3_ABIERTO);
  int angDedo4 = calcularAngulo(PIN_FLEX4, DEDO4_CERRADO, DEDO4_ABIERTO);

  moverSiCambia(servoDedo1, angDedo1, ultimoAngulo1, DEADBAND_DEDO1);
  moverSiCambia(servoDedo2, angDedo2, ultimoAngulo2, DEADBAND_DEDO2);
  moverSiCambia(servoDedo3, angDedo3, ultimoAngulo3, DEADBAND_DEDO3);
  moverSiCambia(servoDedo4, angDedo4, ultimoAngulo4, DEADBAND_DEDO4);

  // ----- MUÑECA Y CODO (sin cambios) -----
  int valPotMuneca = analogRead(PIN_POT_MUNECA);
  int valPotCodo   = analogRead(PIN_POT_CODO);

  int angMuneca = map(valPotMuneca, 0, 1023, 0, 180);
  int angCodo   = map(valPotCodo, 0, 1023, 0, 180);

  servoMuneca.write(angMuneca);
  servoCodo.write(angCodo);

  // ----- DEBUG -----
  Serial.print("D1:"); Serial.print(angDedo1);
  Serial.print(" D2:"); Serial.print(angDedo2);
  Serial.print(" D3:"); Serial.print(angDedo3);
  Serial.print(" D4:"); Serial.print(angDedo4);
  Serial.print(" | Muneca:"); Serial.print(angMuneca);
  Serial.print(" Codo:"); Serial.println(angCodo);

  delay(15);
}