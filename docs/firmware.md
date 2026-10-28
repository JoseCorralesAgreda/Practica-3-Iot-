# Firmware

La Parte 1 implementa HC-SR04 y LEDs; la Parte 2 conecta sus adaptadores con MQTT. PlatformIO usa `espressif32@7.1.1`, placa `esp32doit-devkit-v1`, Arduino y monitor a 115200 baudios. El [plan](plans/02-firmware-plan.md) se conserva como documento histórico; esta página refleja el código actual.

El usuario aprobó sustituir AsyncMqttClient/AsyncTCP por **ESP-MQTT**, incluido en Arduino-ESP32 2.0.17, al encontrar que el transporte previsto no cancelaba un DNS pendiente. La única dependencia externa es `ArduinoJson@6.21.5`. Se conservan interfaces, tópicos, JSON y valores declarados.

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

`MqttClient.h/.cpp` recibe dos punteros a funciones: consumir una distancia y aplicar un estado de LED. No incluye clases de hardware ni conoce pines. Ambos adaptadores se ejecutan exclusivamente desde `update()`, en el bucle Arduino.

`main.cpp` crea los tres objetos y los adaptadores. `setup()` inicia Serial, LEDs, sensor y MQTT, en ese orden. `loop()` solo llama a `distanceSensor.update()` y `mqttClient.update()`, sin esperas de red.

## Conectividad y concurrencia

ESP-MQTT dispone de una tarea interna, pero sus métodos públicos de publicación y suscripción pueden esperar por red o por un mutex, según la [documentación de Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/protocols/mqtt.html). Por eso `MqttClient` añade una única tarea con pila estática para WiFi, supervisión de conexión y publicación. Esta adaptación al cambio de biblioteca mantiene disponible el bucle del sensor. La tarea duerme 20 ms entre revisiones; esa espera no ocurre en `loop()`.

La tarea configura WiFi en estación, sin persistencia de credenciales ni reconexión automática del framework. Solicita conexiones como máximo cada 5000 ms. MQTT usa `broker.hivemq.com:1883`, TCP sin TLS ni autenticación, sesión limpia y keepalive de 10 s. El identificador es `sis234/grupo2/esp32/` más los 12 dígitos hexadecimales de la MAC de fábrica.

Los estados son desconectado, conectando, suscribiendo y listo. Al conectar, el callback solicita `sis234/grupo2/leds/cmd` con QoS 1. Solo pasa a listo tras recibir el SUBACK del mismo paquete con QoS concedido 1. Un rechazo, error, pérdida de WiFi o plazo de conexión/suscripción de 5000 ms inicia la parada del cliente. La tarea espera que termine el trabajo interno antes de iniciar otro intento; no se superponen operaciones DNS. Los reintentos MQTT tienen al menos 5000 ms entre inicios y requieren una nueva suscripción.

El timeout de red configurado es 5000 ms; la finalización de DNS y del SDK puede prolongar una parada. Estas esperas quedan fuera del bucle de hardware. La reconexión en ≤30 s sigue siendo un objetivo que debe medirse con fallos reales, incluidos DNS sin respuesta y tráfico descartado.

Las secciones críticas solo intercambian estado, generación de sesión y contadores. No contienen operaciones de red, análisis JSON, acceso a hardware ni impresión. Los LEDs mantienen su estado cuando se pierde la conexión.

## Lecturas y comandos

`update()` consume la lectura del proveedor incluso desconectado. Solo acepta valores finitos de 5–200 cm y, estando listo, los entrega a un buzón estático de **una lectura**. La tarea de red descarta muestras de otra sesión o con antigüedad ≥1000 ms; una nueva lectura reemplaza la pendiente. Publica `{"cm":42.7}` con una cifra decimal, QoS 0 y sin retención ni reintentos de la muestra. No utiliza la cola de retransmisión de ESP-MQTT. La frecuencia depende del proveedor de la Parte 1, una lectura válida cada 1000 ms, sin segundo temporizador de publicación.

El callback de datos acepta únicamente el tópico de comandos y rechaza mensajes retenidos. Reconstruye hasta 128 bytes usando longitud, desplazamiento y total; rechaza fragmentos desordenados, totales cambiantes, sesiones anteriores y ensamblajes de más de 1000 ms. Los fragmentos posteriores pueden omitir el tópico, como permite ESP-MQTT. Un mensaje completo entra en una cola estática de ocho elementos con espera cero; si está llena, se descarta el nuevo.

Cada `update()` extrae como máximo ocho comandos. Rechaza los vencidos o de otra sesión. El analizador exige un único objeto completo con exactamente `led` y `state`, ambas cadenas; acepta solo `red/yellow/green` y `on/off`. Rechaza claves duplicadas, bytes posteriores al objeto y NUL escapado, que podría truncar una clave durante la deserialización. Respeta comillas y escapes al comprobar el cierre del objeto; permite espacio en blanco exterior.

Cada comando válido llama una vez al adaptador del LED. Un comando cambia únicamente su color; repetir un estado es idempotente y tolera duplicados de QoS 1. Encender rojo y apagar los demás requiere tres comandos. Los descartes se informan con un contador como máximo una vez por segundo, sin imprimir payloads ni credenciales. La aceptación de una publicación QoS 0 no confirma su recepción física.

## Prueba independiente

El entorno `hardware-check` compila exclusivamente `checks/hardware.cpp`, `DistanceSensor.cpp` y `LedController.cpp`. No necesita `secrets.h` ni librerías externas. Sus instrucciones de carga están en el [README](../README.md).

`setup()` inicia Serial y ambos componentes; comprueba además que el controlador rechaza `blue` y un puntero nulo. `loop()` actualiza el sensor, imprime una lectura si está disponible y procesa como máximo un carácter serial por vuelta. `r/R`, `y/Y`, `g/G` encienden/apagan los LEDs rojo, amarillo y verde. Se ignoran espacios y saltos de línea; las demás teclas se rechazan. Un mensaje de actividad cada segundo permite observar que el bucle sigue ejecutándose cuando no hay eco válido.

El serial controla LEDs únicamente en este ejecutable de prueba local; no es un canal de la app o MCP.

`mqtt-check` compila solo `checks/mqtt.cpp` y `MqttClient.cpp`, sin fuentes de sensor ni LEDs. Al arrancar ejecuta 45 comprobaciones de validación, fragmentación, límite de tamaño, retención, cola llena, caducidad, generaciones y SUBACK. Si alguna falla, no inicia la red. Si pasan, publica una distancia ficticia de 42.7 cm cada segundo y registra órdenes como `LED command: red on`. Requiere las credenciales locales de WiFi. No valida la medición ni acciona GPIO.

El entorno predeterminado `esp32doit-devkit-v1` compila e integra ambas partes. Los tres filtros excluyen el resto de fuentes para evitar varios pares `setup()/loop()`.

## Verificación

Se verificó la compilación de los tres entornos con credenciales ficticias y la independencia de `hardware-check` sin `secrets.h`. Las 45 comprobaciones de `mqtt-check` también se ejecutaron en PC con ArduinoJson real y dobles temporales de Arduino, FreeRTOS y red: 45 aprobadas, ninguna fallida. Con reloj y SDK simulados se comprobaron además los plazos de conexión/SUBACK, separación de reintentos y parada antes de reconectar. Esto verifica la lógica; no ejecuta el SDK ni demuestra concurrencia real o conectividad.

En la placa quedan pendientes:

- Comprobar arranque con LEDs apagados, las seis acciones y repetición del mismo estado.
- Comparar diez lecturas en 5, 20, 50, 100 y 200 cm con cinta métrica, registrando también descartes; error objetivo ≤3 cm.
- Retirar el objeto y verificar que el mensaje de actividad y los comandos seriales siguen respondiendo.
- Mantener medición y control ≥10 min sin reinicios ni bloqueos.

- Suscribir otro cliente al tópico de distancia y comprobar aproximadamente 60 publicaciones válidas durante 60 s.
- Enviar las seis órdenes desde la app y medir diez acciones con latencia ≤1 s.
- Cortar y restaurar WiFi y MQTT por separado, incluido tráfico descartado y DNS fallido, y medir recuperación ≤30 s sin detener el sensor.
- Ejecutar las comprobaciones de `mqtt-check` en FreeRTOS real y observar memoria libre durante ≥10 min.

La configuración real `include/secrets.h` está excluida de Git. Solo `MqttClient.cpp` la incluye; `hardware-check` no la necesita. La contraseña se completa localmente y no se imprime.
