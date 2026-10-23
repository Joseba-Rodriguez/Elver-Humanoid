# 🧠 ESP32-C3 · El cerebro conversacional de Elver

La placa **Spotpear Toy-AI-Core-C3 1.54"** con el firmware [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (versión de Spotpear 2.0.3) es la que escucha, habla con la IA y le dice al Mega qué gesto hacer.

La carpeta `main/` copia la estructura del firmware: solo contiene los archivos que se han añadido o cambiado.

| Archivo | Qué es |
|---|---|
| [`main/boards/toy-ai-core-c3-1.54/robot_controller.h`](main/boards/toy-ai-core-c3-1.54/robot_controller.h) | Registra 3 herramientas MCP (`self.robot.hand`, `self.robot.head`, `self.robot.state`), envía cada orden al Mega como **un carácter** por UART y traduce las emociones de la IA a ánimos de Elver |
| [`main/boards/toy-ai-core-c3-1.54/toy_ai_core_c3_1.54.cc`](main/boards/toy-ai-core-c3-1.54/toy_ai_core_c3_1.54.cc) | La placa: `ElverLed` avisa al Mega de si Elver escucha o habla, y `ElverDisplay` le pasa cada emoción |
| [`main/boards/toy-ai-core-c3-1.54/config.h`](main/boards/toy-ai-core-c3-1.54/config.h) | `ROBOT_UART_TX_PIN = GPIO_NUM_2`. El LED RGB queda desactivado para usar su pin |
| [`main/audio/voz_robot.h`](main/audio/voz_robot.h) | Efecto de voz robótica en tiempo real (filtro de peine + modulación en anillo), solo con enteros |
| [`prompt-elver.txt`](prompt-elver.txt) | Personalidad de Elver y cuándo usar cada herramienta. Se pega en la consola de [xiaozhi.me](https://xiaozhi.me) |
| [`muestras-voz/`](muestras-voz) | Antes y después del efecto, y el script que genera las muestras |

## 🔌 Cableado

```
ESP32-C3 (conector "RGB")        Arduino Mega
   IO2  ───────────────────────►  pin 19 (RX1)     115200 baudios
   GND  ────────────────────────  GND
```

Solo va en un sentido (ESP32 → Mega): en esta placa casi no quedan GPIO libres, así que se usa el pin del LED RGB como TX.

## 🛠️ Montar el firmware

1. Descarga el firmware de Spotpear (xiaozhi-esp32 2.0.3, placa `toy-ai-core-c3-1.54`).
2. Copia la carpeta `main/` de aquí encima de la suya.
3. En `main/audio/audio_service.cc`, incluye `voz_robot.h` y, en la tarea de decodificación, justo **después** del remuestreo de la salida, añade:
   ```cpp
   // Elver: voz robótica antes de ir al altavoz
   VozRobot::GetInstance().Procesar(task->pcm, codec_->output_sample_rate());
   ```
4. Compila y flashea con ESP-IDF 5.4.
5. En [xiaozhi.me](https://xiaozhi.me), pega [`prompt-elver.txt`](prompt-elver.txt) como prompt del agente.

> 💡 **Lección aprendida:** con 7 herramientas MCP de descripciones largas, el ESP32-C3 se quedaba sin RAM al enviar `tools/list` y entraba en un bucle de reinicios. Por eso hay solo 3 herramientas con descripciones cortas, y todas las instrucciones van en el prompt.

## 🔊 Ajuste de la voz

Ajuste elegido de oído ("fuerte A"): retardo de 8 ms, realimentación de 0,55, portadora de 70 Hz, profundidad de 0,80 y ganancia ×1,6.
Para probar otros: edita `AJUSTES` en [`muestras-voz/generar_muestras.py`](muestras-voz/generar_muestras.py) (usa una voz de Windows, como *Microsoft Pablo*) y copia los valores elegidos en `voz_robot.h`.
