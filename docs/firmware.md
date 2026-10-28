# Firmware — Parte 1

La Parte 1 implementa HC-SR04 y LEDs, sin dependencias de WiFi, MQTT, JSON ni credenciales. PlatformIO usa `espressif32@7.1.1`, placa `esp32doit-devkit-v1`, Arduino y monitor a 115200 baudios. El [plan](plans/02-firmware-plan.md) se conserva como documento histórico; esta página refleja el código actual.

## Montaje

| Componente | Conexión |
|---|---|
| HC-SR04 VCC / GND | 5 V / GND común |
| HC-SR04 TRIG / ECHO | GPIO18 / GPIO19 |
| LED rojo | GPIO25 → 220 Ω → ánodo; cátodo a GND |
| LED amarillo | GPIO26 → 220 Ω → ánodo; cátodo a GND |
| LED verde | GPIO27 → 220 Ω → ánodo; cátodo a GND |

Cada LED tiene su propia resistencia de **220 Ω** y se enciende con nivel alto. Los números son GPIO, no posiciones físicas del conector.

## Responsabilidades e interfaz

`DistanceSensor.h/.cpp` configura el sensor con `begin()`, avanza la medición con `update()` y entrega una lectura nueva mediante `takeReading(float&)`. Esta última devuelve `false` sin cambiar el argumento si no hay un resultado válido; una lectura consumida no se vuelve a entregar.

El primer `update()` dispara una medición; después se inicia un intento cada 1000 ms. TRIG permanece alto 10 µs, única espera intencional de la medición. Los flancos de ECHO se capturan mediante interrupción, evitando esperar el eco dentro del `loop()`. El plazo es 15000 µs desde el disparo: tanto la ISR como `update()` descartan una captura vencida, incluso si el bucle se ejecuta tarde.

La ISR solo registra tiempos y banderas. Una sección crítica breve protege esos datos frente al bucle; no se bloquean interrupciones durante todo el eco. Fuera de la ISR se calcula `duración × 0.0343 / 2`, aceptando únicamente 5–200 cm. No se reemplazan errores por cero ni se reutiliza la lectura anterior. No hay filtros ni compensación térmica; la exactitud debe comprobarse físicamente. Las diferencias temporales sin signo toleran el desbordamiento de los contadores.

`LedController.h/.cpp` apaga las tres salidas al iniciar y ofrece `setLed(color, enabled)`. Solo acepta `red`, `yellow` y `green`; un color desconocido o nulo devuelve `false` sin tocar pines. Establecer un estado es idempotente y afecta únicamente al LED elegido. Los pines están confinados a las implementaciones de hardware.

La futura Parte 2 recibirá adaptadores de estas funciones, sin conocer los pines. Todavía no hay procesamiento de comandos MQTT ni reconexión implementados.

## Prueba independiente

El entorno `hardware-check` compila exclusivamente `checks/hardware.cpp`, `DistanceSensor.cpp` y `LedController.cpp`. No necesita `secrets.h` ni librerías externas. Sus instrucciones de carga están en el [README](../README.md).

`setup()` inicia Serial y ambos componentes; comprueba además que el controlador rechaza `blue` y un puntero nulo. `loop()` actualiza el sensor, imprime una lectura si está disponible y procesa como máximo un carácter serial por vuelta. `r/R`, `y/Y`, `g/G` encienden/apagan los LEDs rojo, amarillo y verde. Se ignoran espacios y saltos de línea; las demás teclas se rechazan. Un mensaje de actividad cada segundo permite observar que el bucle sigue ejecutándose cuando no hay eco válido.

El serial controla LEDs únicamente en este ejecutable de prueba local; no es un canal de la app o MCP. `src/main.cpp` conserva el diagnóstico inicial hasta la integración de la Parte 2. El entorno predeterminado lo compila por separado; no carga esta prueba automáticamente ni conecta los componentes.

## Verificación

Compilar con `pio run --project-dir firmware -e hardware-check` verifica el enlace con Arduino-ESP32, pero no ejecuta el circuito. En la placa quedan pendientes:

- Comprobar arranque con LEDs apagados, las seis acciones y repetición del mismo estado.
- Comparar diez lecturas en 5, 20, 50, 100 y 200 cm con cinta métrica, registrando también descartes; error objetivo ≤3 cm.
- Retirar el objeto y verificar que el mensaje de actividad y los comandos seriales siguen respondiendo.
- Mantener medición y control ≥10 min sin reinicios ni bloqueos.

Frecuencia de publicación, latencia desde la app y reconexión se verificarán al implementar MQTT. La plantilla WiFi sigue sin ser utilizada por esta parte.
