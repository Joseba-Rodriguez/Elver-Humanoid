// voz_robot.h - Voz robótica de Elver.
//
// Se aplica al audio ya decodificado justo antes de ir al altavoz:
//   1. Filtro de peine con realimentación: timbre metálico de "lata".
//   2. Modulación en anillo: el clásico efecto de voz de robot.
// Todo en enteros (el ESP32-C3 no tiene unidad de coma flotante).
// Es el mismo algoritmo que esp32-xiaozhi/muestras-voz/generar_muestras.py,
// con el que se generaron las muestras para elegir la intensidad.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

class VozRobot {
public:
    // Intensidad elegida por oído: "fuerte A" (ver AJUSTES en generar_muestras.py)
    static constexpr int   kRetardoMs      = 8;
    static constexpr float kRealimentacion = 0.55f;
    static constexpr int   kFrecuenciaHz   = 70;
    static constexpr float kProfundidad    = 0.80f;
    static constexpr int32_t kGananciaX256 = 410;   // x1,6: recupera el volumen que quita el efecto

    static VozRobot& GetInstance() {
        static VozRobot instancia;
        return instancia;
    }

    void Procesar(std::vector<int16_t>& pcm, int frecuencia_muestreo) {
        if (frecuencia_muestreo != fs_) {
            Configurar(frecuencia_muestreo);
        }
        for (auto& x : pcm) {
            int32_t y = (x * g_entrada_ + buf_[idx_] * g_realim_) >> 15;
            y = Limitar(y);
            buf_[idx_] = (int16_t)y;
            if (++idx_ >= buf_.size()) {
                idx_ = 0;
            }
            int32_t s = seno_[(fase_ >> 24) & 0xFF];
            fase_ += inc_;
            int32_t mod = (32768 - m_) + ((m_ * s) >> 15);
            int32_t z = Limitar((y * mod) >> 15);
            x = (int16_t)Limitar((z * kGananciaX256) >> 8);
        }
    }

private:
    int fs_ = 0;
    std::vector<int16_t> buf_;
    size_t idx_ = 0;
    int32_t seno_[256];
    int32_t g_entrada_ = 0, g_realim_ = 0, m_ = 0;
    uint32_t fase_ = 0, inc_ = 0;

    VozRobot() {
        for (int i = 0; i < 256; i++) {
            seno_[i] = (int32_t)lroundf(32767.0f * sinf(2.0f * (float)M_PI * i / 256.0f));
        }
    }

    void Configurar(int fs) {
        fs_ = fs;
        buf_.assign(fs * kRetardoMs / 1000, 0);
        idx_ = 0;
        g_realim_ = (int32_t)(kRealimentacion * 32768);
        g_entrada_ = (int32_t)(32768 / (1 + kRealimentacion));   // evita saturar
        m_ = (int32_t)(kProfundidad * 32768);
        inc_ = (uint32_t)((double)kFrecuenciaHz * 4294967296.0 / fs);
        fase_ = 0;
    }

    static int32_t Limitar(int32_t v) {
        return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    }
};
