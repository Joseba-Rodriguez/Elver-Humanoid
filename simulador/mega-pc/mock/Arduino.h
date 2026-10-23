// Arduino.h (simulado)
// Imita la parte de la API de Arduino que usa humanoide-automatico.ino para
// poder compilar el MISMO sketch en el PC.
//
// Protocolo con el exterior (lo usa puente.py):
//   stdin  -> cada carácter recibido entra por Serial1 (como si llegara del ESP32)
//   stdout <- "@S <pin> <ángulo>"   cada vez que un servo cambia de ángulo
//             "[USB] <texto>"       lo que el sketch imprime por Serial
//             "[TX1] <texto>"       lo que el sketch imprime por Serial1

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <deque>
#include <mutex>

typedef uint8_t byte;
typedef bool boolean;

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 54

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

unsigned long millis();
void delay(unsigned long ms);
void pinMode(uint8_t, uint8_t);
int analogRead(uint8_t);
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

// Imprime una línea completa por stdout de forma segura entre hilos
void simEmit(const std::string& line);
// Cola de caracteres que llegan por stdin (Serial1)
extern std::deque<char> simRx1;
extern std::mutex simRxMutex;

class HardwareSerial {
public:
    HardwareSerial(const char* tag, bool fromStdin) : tag_(tag), fromStdin_(fromStdin) {}

    void begin(unsigned long) {}

    int available() {
        if (!fromStdin_) return 0;
        std::lock_guard<std::mutex> lock(simRxMutex);
        return (int)simRx1.size();
    }

    int read() {
        if (!fromStdin_) return -1;
        std::lock_guard<std::mutex> lock(simRxMutex);
        if (simRx1.empty()) return -1;
        char c = simRx1.front();
        simRx1.pop_front();
        return (unsigned char)c;
    }

    void print(const char* s) { line_ += s; }
    void print(const __FlashStringHelper* s) { line_ += reinterpret_cast<const char*>(s); }
    void print(char c) { line_ += c; }
    void print(int v) { line_ += std::to_string(v); }
    void print(unsigned int v) { line_ += std::to_string(v); }
    void print(long v) { line_ += std::to_string(v); }
    void print(unsigned long v) { line_ += std::to_string(v); }
    void print(double v) { char b[32]; snprintf(b, sizeof(b), "%.2f", v); line_ += b; }

    template <typename T> void println(T v) { print(v); println(); }
    void println() {
        simEmit(std::string("[") + tag_ + "] " + line_);
        line_.clear();
    }

private:
    const char* tag_;
    bool fromStdin_;
    std::string line_;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
