// arduino_sim.cpp: implementación del Arduino simulado y bucle principal.
// (las cabeceras estándar van antes: Arduino.h define min/max como macros)
#include <chrono>
#include <thread>
#include <iostream>

#include "Arduino.h"
#include "Servo.h"

#ifdef _WIN32
// (no se incluye windows.h: choca con INPUT, boolean... de Arduino)
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int uPeriod);
#endif

std::deque<char> simRx1;
std::mutex simRxMutex;
static std::mutex outMutex;

HardwareSerial Serial("USB", false);
HardwareSerial Serial1("TX1", true);

static const auto t0 = std::chrono::steady_clock::now();

void simEmit(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMutex);
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

unsigned long millis() {
    auto dt = std::chrono::steady_clock::now() - t0;
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
}

void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void pinMode(uint8_t, uint8_t) {}
int analogRead(uint8_t) { return (int)(millis() * 7919u % 1024u); }

// Mismo comportamiento que random() de Arduino
long random(long howbig) {
    if (howbig == 0) return 0;
    return std::rand() % howbig;
}
long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    return random(howbig - howsmall) + howsmall;
}
void randomSeed(unsigned long seed) {
    if (seed != 0) std::srand((unsigned)seed);
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setup();
void loop();

int main() {
#ifdef _WIN32
    // Sin esto Windows redondea cada sleep a ~15 ms y el loop() iría mucho más
    // lento que en el Mega (el cuello hace un paso cada 40 ms).
    timeBeginPeriod(1);
#endif
    // Hilo lector de stdin: cada carácter entra por Serial1 (RX1 del Mega)
    std::thread([] {
        int c;
        while ((c = std::getchar()) != EOF) {
            if (c == '\r' || c == '\n') continue;
            std::lock_guard<std::mutex> lock(simRxMutex);
            simRx1.push_back((char)c);
        }
    }).detach();

    setup();
    for (;;) {
        loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
