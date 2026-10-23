// robot_controller.h
// Puente XiaoZhi (ESP32-C3) -> Arduino Mega de ELVER, el humanoide.
//
// 1. Registra herramientas MCP en el firmware xiaozhi-esp32. Cuando hablas con
//    Elver ("saluda", "juguemos a piedra, papel o tijera"...), el LLM llama a
//    estas herramientas y aquí se envía UN carácter por UART al Mega, que lo
//    ejecuta con procesarComando() (ver humanoide-automatico.ino).
// 2. Avisa al Mega del estado de la conversación (escuchando / hablando /
//    reposo) para que Elver preste atención o acompañe lo que dice. Lo repite
//    cada 20 s: si el ESP32 se desconecta, el Mega vuelve solo a su vida normal.
// 3. Convierte la emoción de cada respuesta del LLM en un ánimo de Elver.
//
// Todos los límites de los servos están en el Mega: desde aquí solo se eligen
// poses y movimientos que ya existen en su programa.
//
// Solo se transmite (ESP32 -> Mega): en la placa Toy-AI-Core-C3 casi no quedan
// GPIO libres, así que se usa IO2 del conector "RGB" (3V3 / GND / IO2) como TX.
// Cableado: IO2 -> pin 19 (RX1) del Mega, y GND con GND.

#pragma once

#include <atomic>
#include <cstring>
#include <string>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "mcp_server.h"
#include "device_state.h"

class RobotController {
public:
    explicit RobotController(gpio_num_t tx_pin) {
        uart_config_t cfg = {};
        cfg.baud_rate = 115200;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        ESP_ERROR_CHECK(uart_driver_install(kUart, 256, 0, 0, nullptr, 0));
        ESP_ERROR_CHECK(uart_param_config(kUart, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(kUart, tx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

        esp_timer_create_args_t timer_args = {};
        timer_args.callback = [](void* arg) { static_cast<RobotController*>(arg)->Recordatorio(); };
        timer_args.arg = this;
        timer_args.name = "elver_conv";
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &recordatorio_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(recordatorio_timer_, kRecordatorioUs));

        RegisterTools();
    }

    // Lo llama el "LED" de la placa en cada cambio de estado del asistente
    void OnDeviceState(DeviceState state) {
        char c = 0;
        switch (state) {
            case kDeviceStateListening: c = 'E'; break;
            case kDeviceStateSpeaking:  c = 'H'; break;
            case kDeviceStateIdle:      c = 'Z'; break;
            default: return;               // conectando, arrancando...: sin cambios
        }
        if (c != estado_conv_.exchange(c)) {
            Send(c);
        }
    }

    // Lo llama la pantalla con la emoción de cada respuesta del LLM
    void OnEmotion(const char* emotion) {
        char c = AnimoDeEmocion(emotion);
        if (c != 0 && c != ultimo_animo_.exchange(c)) {
            Send(c);
        }
        if (strcmp(emotion, "winking") == 0) {
            Send('j');
        }
    }

private:
    static constexpr uart_port_t kUart = UART_NUM_1;
    static constexpr const char* TAG = "Robot";
    static constexpr uint64_t kRecordatorioUs = 20 * 1000 * 1000;

    esp_timer_handle_t recordatorio_timer_ = nullptr;
    std::atomic<char> estado_conv_{0};
    std::atomic<char> ultimo_animo_{0};

    void Send(char c) {
        uart_write_bytes(kUart, &c, 1);
        ESP_LOGI(TAG, "Comando enviado al Mega: %c", c);
    }

    // Repite el estado de la conversación (el Mega lo ignora si no cambia)
    void Recordatorio() {
        char c = estado_conv_.load();
        if (c != 0) {
            uart_write_bytes(kUart, &c, 1);
        }
    }

    // Emociones de xiaozhi -> ánimos de Elver: 0 Tranquilo, 1 Curioso,
    // 2 Somnoliento, 3 Alerta, 4 Tímido. "neutral" y las demás no cambian nada.
    static char AnimoDeEmocion(const char* e) {
        static const struct { const char* emocion; char animo; } kTabla[] = {
            {"happy", '0'}, {"laughing", '0'}, {"funny", '0'}, {"relaxed", '0'},
            {"cool", '0'}, {"confident", '0'}, {"loving", '0'}, {"delicious", '0'},
            {"thinking", '1'}, {"confused", '1'}, {"silly", '1'},
            {"sleepy", '2'},
            {"surprised", '3'}, {"shocked", '3'}, {"angry", '3'},
            {"embarrassed", '4'}, {"kissy", '4'}, {"sad", '4'}, {"crying", '4'},
        };
        for (const auto& fila : kTabla) {
            if (strcmp(e, fila.emocion) == 0) {
                return fila.animo;
            }
        }
        return 0;
    }

    static char Buscar(const std::string& valor, const char* const* nombres, const char* letras) {
        for (int i = 0; nombres[i] != nullptr; i++) {
            if (valor == nombres[i]) {
                return letras[i];
            }
        }
        return 0;
    }

    // Pocas herramientas y descripciones cortas: la lista completa viaja en un
    // solo mensaje y el ESP32-C3 tiene muy poca RAM libre (con descripciones
    // largas se quedaba sin memoria al enviarla). Las instrucciones de cómo
    // usarlas van en el prompt del agente (xiaozhi.me), no aquí.
    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.robot.hand",
            "Elver's hand. action: wave, come_here, silence, so_so, drum, rock, paper, scissors, "
            "thumbs_up, point, ok, relaxed, wink (eye).",
            PropertyList({ Property("action", kPropertyTypeString) }),
            [this](const PropertyList& props) -> ReturnValue {
                static const char* const kNombres[] = {"wave", "come_here", "silence", "so_so", "drum",
                    "rock", "paper", "scissors", "thumbs_up", "point", "ok", "relaxed", "wink", nullptr};
                char c = Buscar(props["action"].value<std::string>(), kNombres, "hvsmtnbxuworj");
                if (!c) return std::string("unknown action");
                Send(c);
                return true;
            });

        mcp.AddTool("self.robot.head",
            "Elver's head. direction: center, tilt_left, tilt_right, turn_left, turn_right, shake_no.",
            PropertyList({ Property("direction", kPropertyTypeString) }),
            [this](const PropertyList& props) -> ReturnValue {
                static const char* const kNombres[] = {"center", "tilt_left", "tilt_right", "turn_left", "turn_right", "shake_no", nullptr};
                char c = Buscar(props["direction"].value<std::string>(), kNombres, "CIDLRN");
                if (!c) return std::string("unknown direction");
                Send(c);
                return true;
            });

        mcp.AddTool("self.robot.state",
            "Elver's state. state: calm, curious, sleepy, alert, shy, still, move, sleep, wake.",
            PropertyList({ Property("state", kPropertyTypeString) }),
            [this](const PropertyList& props) -> ReturnValue {
                static const char* const kNombres[] = {"calm", "curious", "sleepy", "alert", "shy",
                    "still", "move", "sleep", "wake", nullptr};
                char c = Buscar(props["state"].value<std::string>(), kNombres, "01234qQda");
                if (!c) return std::string("unknown state");
                if (c >= '0' && c <= '4') {
                    ultimo_animo_ = c;
                }
                Send(c);
                return true;
            });
    }
};
