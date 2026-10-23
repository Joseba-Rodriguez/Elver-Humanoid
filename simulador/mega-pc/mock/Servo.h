// Servo.h (simulado): en vez de generar PWM, informa de cada cambio de ángulo.
#pragma once

#include "Arduino.h"

class Servo {
public:
    uint8_t attach(int pin) {
        pin_ = pin;
        return 1;
    }

    void write(int angle) {
        angle = constrain(angle, 0, 180);     // igual que la librería Servo real
        if (angle == angle_) return;
        angle_ = angle;
        simEmit("@S " + std::to_string(pin_) + " " + std::to_string(angle_));
    }

    int read() { return angle_; }

private:
    int pin_ = -1;
    int angle_ = -1;
};
