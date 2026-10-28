# Firmware: estado actual

`firmware/platformio.ini` selecciona DOIT ESP32 DevKit V1 (`esp32doit-devkit-v1`) con Arduino y monitor a 115200 baudios.

`src/main.cpp` conserva la base existente: `setup()` inicia Serial y `loop()` imprime el tiempo de actividad aproximadamente cada segundo usando `millis()`. Incluye una pausa de 10 ms heredada; el diseño funcional deberá sustituirla por actualizaciones sin esperas. No mide distancia ni controla LEDs ni conecta a WiFi/MQTT.

`include/secrets.example.h` contiene valores ficticios. El futuro `secrets.h` está excluido de Git; actualmente ningún archivo lo carga.

La salida serial es únicamente diagnóstico local, no un canal para la app ni para MCP. El [plan general](plans/01-general-plan.md) separa hardware y conectividad; las clases, pines e interfaces se concretarán en el plan detallado antes de implementar.

Las instrucciones de compilación y carga están en el [README](../README.md). Compilar no verifica el montaje ni los objetivos de precisión, frecuencia o latencia.
