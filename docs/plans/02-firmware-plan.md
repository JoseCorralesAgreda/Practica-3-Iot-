# Plan del firmware — Grupo 2

Fecha: 27 de octubre de 2026. Estado: pendiente de aprobación del plan y confirmación de pines. No se ha implementado este diseño.

El usuario aprobó íntegramente el plan general en esta sesión. Este documento desarrolla sus decisiones sin modificar el [plan general archivado](01-general-plan.md). La [especificación](../../Práctica_MQTT.md) y [AGENTS.md](../../AGENTS.md) rigen el alcance. Una vez ejecutado, este plan queda archivado sin actualizaciones.

## 1. Resultado esperado

El ESP32 mide distancias entre 5 y 200 cm y publica una medición válida cada 1000 ms. Recibe comandos JSON por MQTT para encender o apagar individualmente los tres LEDs. Mantiene el control local disponible durante fallos del sensor, WiFi o broker y restablece la conectividad automáticamente.

Objetivos de aceptación: error máximo ±3 cm con objeto plano perpendicular, respuesta app → LED ≤1 s, operación continua ≥10 min y reconexión WiFi/MQTT ≤30 s desde que vuelve el servicio. Son objetivos que deben medirse, no garantías deducidas de la compilación. Se mantiene el intervalo aprobado de 1000 ms frente a la mención contradictoria de 200 ms en AGENTS.md §6.3.

No se añaden automatismos de LEDs por distancia, tópicos de estado, servomotor, servicios HTTP ni comunicación directa de la app/MCP con el dispositivo.

## 2. Archivos, responsables y dependencias

| Responsable | Archivos | Responsabilidad |
|---|---|---|
| Integrante 1, Parte 1 | `include/DistanceSensor.h`, `src/DistanceSensor.cpp` | Medición y validación de distancia |
| Integrante 1, Parte 1 | `include/LedController.h`, `src/LedController.cpp` | Pines y estado de los LEDs |
| Integrante 1, Parte 1 | `checks/hardware.cpp` | Prueba local por serial, sin red |
| Integrante 2, Parte 2 | `include/MqttClient.h`, `src/MqttClient.cpp` | WiFi, MQTT, JSON y reconexión |
| Integrante 2, Parte 2 | `checks/mqtt.cpp` | Prueba con distancia ficticia y comandos registrados por serial |
| Integrante 2, integración | `src/main.cpp`, `platformio.ini`, `include/secrets.example.h` | Ensamblaje, entornos de compilación y plantilla |

Todas las rutas de la tabla son relativas a `firmware/`. Las clases no requieren archivos adicionales de interfaces, configuración o utilidades. Los dos archivos de `checks/` se justifican por la exigencia de compilar y probar cada parte por separado; no forman parte del firmware final cargado en la demo.

Base de compilación: `platform = espressif32@7.1.1`, `board = esp32doit-devkit-v1`, `framework = arduino`, `monitor_speed = 115200`. Esa plataforma ya compiló la base del repositorio con Arduino-ESP32 2.0.17. Dependencias previstas para los entornos que usan MQTT:

- `marvinroger/AsyncMqttClient@0.9.0`: conexión y recepción asíncronas, QoS y sesiones limpias.
- `me-no-dev/AsyncTCP@1.1.1`: transporte TCP del cliente anterior para ESP32.
- `bblanchon/ArduinoJson@6.21.5`: análisis de JSON en documento de tamaño fijo.

WiFi y FreeRTOS proceden del framework; el sensor y los LEDs no necesitan librerías adicionales. Las versiones se fijan para evitar que una actualización cambie las APIs durante la práctica. La primera verificación de implementación será resolver y compilar esta combinación; si falla su compatibilidad, se consultará antes de cambiar la selección.

La elección asíncrona responde a un requisito actual: la conexión TCP y la espera de respuestas MQTT no deben detener la lectura ni la ejecución de comandos en `loop()`. No se crean tareas propias ni jerarquías de clientes. La biblioteca sí utiliza memoria dinámica internamente; el código del grupo utilizará buffers fijos y no hará `new`, `malloc` ni concatenaciones de `String` en su camino frecuente.

## 3. Interfaz mínima acordada entre partes

La Parte 2 no incluirá `DistanceSensor.h` ni `LedController.h`; recibirá dos punteros a funciones ordinarias. La Parte 1 no incluirá WiFi, MQTT ni JSON. No hay clases abstractas ni herencia.

| Clase | Firma pública prevista | Semántica |
|---|---|---|
| `DistanceSensor` | `DistanceSensor()` | Usa los pines constantes de su implementación |
| | `void begin()` | Configura TRIG/ECHO, estado inicial e interrupción |
| | `void update()` | Programa disparos, recoge resultados y atiende timeout; no espera el eco |
| | `bool takeReading(float& distanceCm)` | Entrega una lectura válida nueva una sola vez; devuelve `false` si no hay ninguna |
| `LedController` | `LedController()` | Usa los tres pines constantes de su implementación |
| | `void begin()` | Configura salidas y apaga los tres LEDs |
| | `bool setLed(const char* color, bool enabled)` | Aplica estado al color exacto; rechaza color desconocido sin cambiar salidas |
| `MqttClient` | `MqttClient(bool (*readDistance)(float&), bool (*applyLed)(const char*, bool))` | Guarda ambos adaptadores; no realiza conexiones en el constructor |
| | `void begin()` | Configura WiFi, broker y callbacks una vez |
| | `void update()` | Atiende conexión, cola de comandos y nuevas mediciones |

`takeReading` no devuelve una caché reutilizable: consume la disponibilidad del resultado. Las dos funciones entregadas a MQTT se ejecutan exclusivamente en `loop()`, nunca desde callbacks de red. Los adaptadores de `main.cpp` solo delegan llamadas al objeto correspondiente, sin decisiones de negocio.

## 4. Parte 1 — Sensor y actuadores

### 4.1 Montaje propuesto, pendiente de confirmación

| Elemento | Conexión ESP32 | Constante |
|---|---|---|
| HC-SR04 TRIG | GPIO18 | `kTriggerPin` |
| HC-SR04 ECHO | GPIO19 | `kEchoPin` |
| LED rojo | GPIO25 → resistencia 220 Ω → ánodo | `kRedLedPin` |
| LED amarillo | GPIO26 → resistencia 220 Ω → ánodo | `kYellowLedPin` |
| LED verde | GPIO27 → resistencia 220 Ω → ánodo | `kGreenLedPin` |
| HC-SR04 VCC | 5 V | — |
| HC-SR04 GND y cátodos de LEDs | GND común | — |

Se emplea numeración GPIO, no posición física del conector. Los LEDs son activos en alto. Usar una resistencia por LED y cableado ordenado. La selección evita GPIO de arranque, flash y UART; las funciones digitales elegidas no requieren ADC. La tabla de restricciones se verificó en la [documentación de Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-reference/peripherals/gpio.html).

Los números de pin pertenecen únicamente a los `.cpp` de hardware; ni MQTT ni `main.cpp` los conocen. No habrá parámetros de pines para cambios futuros hipotéticos.

### 4.2 Medición sin espera de eco

Constantes de `DistanceSensor.cpp`:

| Constante | Valor | Propósito |
|---|---|---|
| `kMeasurementIntervalMs` | 1000 | Cadencia de nuevas lecturas y, por tanto, de publicaciones |
| `kTriggerPulseUs` | 10 | Duración del pulso TRIG |
| `kEchoTimeoutUs` | 15000 | Límite desde el disparo hasta completar el eco |
| `kMinDistanceCm` / `kMaxDistanceCm` | 5.0 / 200.0 | Rango aceptado |
| `kSoundSpeedCmPerUs` | 0.0343 | Conversión nominal de duración a distancia |

Flujo previsto:

1. `begin()` deja TRIG bajo, registra ECHO con `attachInterruptArg(..., CHANGE)` y marca la primera medición como disponible para iniciar en el primer `update()`.
2. Cuando vence el intervalo, `update()` invalida cualquier resultado no consumido, prepara una captura nueva y emite TRIG alto durante 10 µs. Se permite únicamente esta espera microscópica y acotada mediante `delayMicroseconds`; no se usa `delay()` ni `pulseIn()` para esperar el eco.
3. Una ISR estática `onEchoChange(void* context)` delega en `handleEchoChange()`: registra con `micros()` el primer flanco ascendente y el descendente posterior. Solo guarda enteros y banderas; no imprime, calcula flotantes, llama a MQTT ni controla LEDs.
4. `update()` recoge una captura completa, calcula `durationUs * kSoundSpeedCmPerUs / 2.0f` y acepta solo un resultado finito dentro del rango. Una caída sin subida, duración cero o captura fuera del plazo es inválida.
5. Al alcanzar 15000 µs sin captura válida, termina el intento e invalida su resultado. ECHO permanentemente alto o bajo nunca detiene el programa. Los flancos fuera de una captura activa se ignoran.
6. `takeReading()` entrega el resultado válido una vez. El siguiente disparo ocurre a 1000 ms del anterior, independientemente de si hubo eco; no se envían ráfagas para recuperar intervalos perdidos.

El pulso a 200 cm dura aproximadamente 11662 µs con la conversión propuesta; 15000 µs deja margen para completar la captura. El intervalo se mide con resta de enteros sin signo para tolerar el desbordamiento de `millis()` y `micros()`. El plazo se comprueba también contra los tiempos de flanco, para no aceptar un eco tardío solo porque `update()` corrió después.

Los datos compartidos ISR/loop se protegen con el mecanismo de sección crítica de ESP32 (`portMUX_TYPE` y variantes ISR). La sección solo copia o cambia los pocos campos de captura; `volatile` por sí solo no reemplaza esa protección. No se mantienen interrupciones deshabilitadas durante todo el eco. Los miembros de estado serán `captureActive_`, `riseSeen_`, `captureComplete_`, `triggerTimeUs_`, `echoRiseUs_`, `echoDurationUs_`, `lastMeasurementMs_`, `readingAvailable_` y `distanceCm_`.

No se añade promedio móvil, mediana ni compensación térmica. Si la prueba física no cumple ±3 cm, se registrará el resultado y se consultará antes de modificar el diseño.

### 4.3 LEDs

`LedController::begin()` deja las tres salidas en bajo. `setLed()` compara `color` con `red`, `yellow` y `green` mediante comparaciones explícitas; aplica `HIGH` o `LOW` solo al pin seleccionado y devuelve éxito. Un puntero nulo o color desconocido devuelve `false`.

No hay temporizadores ni `update()` de LEDs: son salidas de estado persistente. Repetir `on` o `off` conserva el mismo resultado, lo que tolera duplicados de QoS 1. Una pérdida de red no llama a `begin()` ni modifica LEDs; un reinicio los apaga.

### 4.4 Prueba independiente de hardware

`checks/hardware.cpp` crea únicamente sensor y controlador. Imprime cada medición válida y permite seis teclas seriales: `r/R`, `y/Y`, `g/G` para encender/apagar rojo, amarillo y verde respectivamente. Usa `Serial.available()` y lee como máximo un carácter por vuelta; no usa lecturas con espera. Los mensajes y comentarios del código estarán en inglés.

Esta prueba funciona sin `secrets.h`, sin WiFi y sin dependencias MQTT. Verifica todos los estados, rechazo de color inválido, precisión, pérdida de eco y continuidad del control. El control serial pertenece exclusivamente al ejecutable de prueba local y no existe en el firmware de la demo.

## 5. Parte 2 — WiFi y cliente MQTT

### 5.1 Configuración fija

| Constante/ajuste | Valor |
|---|---|
| `kBrokerHost` / `kBrokerPort` | `broker.hivemq.com` / 1883 |
| `kDistanceTopic` | `sis234/grupo2/distance` |
| `kCommandTopic` | `sis234/grupo2/leds/cmd` |
| `kReconnectIntervalMs` | 5000 |
| `kMqttAttemptTimeoutMs` | 5000 para conexión; otros 5000 para SUBACK |
| `kKeepAliveSeconds` | 10 |
| `kMaxCommandBytes` | 128 |
| `kCommandQueueLength` | 8 |
| `kCommandMaxAgeMs` | 1000 |
| `kDistancePayloadSize` | 32 bytes |
| `kClientIdSize` | 48 bytes |

Credenciales WiFi: `kWifiSsid` y `kWifiPassword` en `include/secrets.h`, creado localmente a partir de la plantilla ignorada por Git. Solo `MqttClient.cpp` incluye el secreto. No registrar contraseña ni copiarla en ejemplos. Broker y tópicos permanecen constantes en `MqttClient.cpp`, sin `.env`, menús ni configuración remota.

Identificador: `sis234/grupo2/esp32/` seguido de los 12 dígitos hexadecimales de la MAC de fábrica, formateado en un buffer persistente. Se configura `setCleanSession(true)`, `setKeepAlive(10)` y el broker sin credenciales, TLS ni LWT. Los callbacks y buffers se crean una sola vez en `begin()`.

### 5.2 Flujo de conexión y reconexión

`update()` es el único lugar que decide iniciar o repetir intentos. WiFi se configura en modo estación, sin guardar credenciales por cambios de configuración repetidos y con reconexión automática de Arduino desactivada para mantener una sola política de reintentos. Se inicia la conexión y se consulta `WiFi.status()` sin un bucle de espera. Si no hay IP, se solicita reconexión como máximo una vez cada 5 s.

Para MQTT se distinguen `Disconnected`, `Connecting`, `Subscribing` y `Ready`:

1. Con WiFi disponible y sin intento activo, comenzar una conexión asíncrona cuando corresponda el reintento. No usar resolución DNS síncrona en `loop()`; el hostname se entrega al transporte asíncrono.
2. `onConnect` registra el evento; `update()` solicita la suscripción QoS 1 al tópico de comandos y guarda el identificador de paquete. No se considera listo el sistema hasta recibir el SUBACK correspondiente con QoS concedido 1.
3. Si CONNACK o SUBACK no llega en su plazo, la suscripción falla o se pierde WiFi, solicitar `disconnect(true)`, invalidar estado y esperar el siguiente intento. No iniciar conexiones superpuestas ni recrear el cliente continuamente.
4. `onDisconnect` invalida la sesión y su generación. Los comandos recibidos en la sesión anterior se descartan. Antes de reconectar se limpia la cola de salida únicamente cuando el cliente ya está desconectado y ha finalizado su limpieza; no se conservan muestras para retransmitirlas tras recuperar la red. Una conexión que falle antes de abrir TCP no puede depender de recibir `onDisconnect`: el vencimiento del intento también debe devolver el controlador a estado desconectado. Verificar específicamente DNS fallido y servidor que no responde.
5. Cada reconexión requiere una nueva suscripción. No se acumulan órdenes ni mediciones durante la desconexión. LEDs y sensor continúan funcionando localmente.

El intervalo de 5 s es el mínimo entre inicios de intentos, no una promesa de que una red fallida responda exactamente en ese plazo. El timeout de cada intento y el keepalive se validarán con pérdida de tráfico, además de un cierre limpio del broker. El criterio medido sigue siendo ≤30 s desde la recuperación del servicio.

Los callbacks de AsyncTCP pueden correr en otro contexto: las banderas de conexión, generación de sesión y confirmaciones se intercambian con `loop()` bajo una sección crítica breve. No llamar APIs de red, Serial, JSON o hardware dentro de esa sección. Las operaciones de red se ejecutan fuera del bloqueo y los eventos se aplican solo a la sesión vigente. No se reinicia el ESP32 como estrategia de reconexión.

### 5.3 Publicación de distancia

En cada `update()`, llamar al adaptador `readDistance` una vez, aun sin conexión. Así se consume y descarta cualquier resultado obtenido mientras la red no estaba lista. Si devuelve una lectura válida y la sesión está `Ready`, serializar `{"cm":42.7}` con una cifra decimal en un buffer de 32 bytes y publicarla con QoS 0 y `retain=false`.

La Parte 2 verifica también que el valor sea finito y esté entre 5 y 200 antes de formatear. No conoce el mecanismo físico de medición. No usa un segundo temporizador de publicación que pueda perder muestras por desfase: la cadencia la entrega el proveedor, una lectura nueva cada 1000 ms. En la prueba MQTT, el proveedor ficticio cumple exactamente esa misma regla.

Comprobar longitud de `snprintf` antes de publicar. No reintentar una muestra fallida ni guardar un historial. La aceptación por la librería no equivale a recepción confirmada del broker, pues QoS 0 no tiene PUBACK. No interpretar el identificador de paquete de QoS 0 como un acuse de entrega.

### 5.4 Recepción, fragmentación y validación

`onMessage` recibe longitudes explícitas: no supone que payload esté terminado en NUL ni que un JSON llegue en un único fragmento. Un buffer fijo de 129 bytes reconstruye un mensaje de hasta 128 bytes usando `index`, `len` y `total`. Se acepta únicamente el tópico exacto y `retain=false`.

Al primer fragmento se reinicia el ensamblaje. Cada fragmento debe comenzar en el desplazamiento esperado y conservar el total anunciado. Si excede el límite, se desordena, cambia de sesión o no completa en 1000 ms, se descarta el mensaje completo. Nunca interpretar un fragmento como comando ni truncar una orden para ejecutarla.

Cuando el mensaje está completo, copiarlo a una cola FreeRTOS estática de ocho elementos con espera cero. Cada elemento contiene longitud, payload, instante de recepción y generación de sesión. El callback no analiza JSON ni toca hardware. Si la cola está llena, se descarta la nueva orden y se registra un contador para diagnóstico; no se sobrescribe una orden anterior ni se bloquea el callback.

`processPendingCommands()` consume como máximo ocho elementos por `update()`, sin esperar. Descarta órdenes con más de 1000 ms de antigüedad o de una sesión anterior. Para cada mensaje vigente:

1. Analizar con `StaticJsonDocument<256>` y límite de anidamiento 1. Rechazar errores, datos vacíos y contenido distinto de un único objeto JSON completo; el espacio en blanco exterior es válido. No confiar en que el parser rechace datos posteriores al primer objeto: `parseCommand` debe comprobar el cierre exterior y que después solo haya espacio en blanco, respetando comillas y escapes en un recorrido acotado del buffer.
2. Exigir exactamente las dos claves `led` y `state`, ambas cadenas de texto; no convertir números, booleanos ni valores nulos a texto.
3. Aceptar únicamente `red`, `yellow`, `green` y `on`, `off`, con coincidencia exacta y sensible a mayúsculas.
4. Invocar `applyLed(color, enabled)` una sola vez. Si devuelve `false`, informar error sin intentar otro color.

Los duplicados MQTT se pueden ejecutar de nuevo porque son idempotentes. `{"led":"red","state":"on"}` no apaga los demás: la instrucción compuesta de IA sigue correspondiendo a tres comandos separados del contrato aprobado.

Diagnósticos privados: `handleConnect`, `handleDisconnect`, `handleSubscribe`, `handleMessage`, `updateConnections`, `processPendingCommands`, `publishReading` y `parseCommand`. Ninguno expone funciones de pines. Limitar los avisos de mensajes descartados a uno por segundo con contador, para que un flujo inválido no bloquee el programa por Serial. No imprimir el payload completo.

### 5.5 Prueba independiente de MQTT

`checks/mqtt.cpp` solo incluye `MqttClient.h`. Su proveedor entrega 42.7 cm una vez por segundo; su actuador imprime `LED command: red on` y los otros valores, sin GPIO. Usa credenciales locales reales para el WiFi y el mismo broker/contrato aprobado.

Comprobar que otro cliente recibe `{"cm":42.7}`, que los seis comandos válidos llegan al adaptador y que los inválidos no lo invocan. No ejecutar simultáneamente la prueba y el firmware integrado con el mismo identificador en dos instancias; cada ESP32 físico tiene su ID propio. Esta prueba valida MQTT, no los LEDs ni la precisión física.

## 6. Integración y entornos PlatformIO

`main.cpp` tendrá solo tres objetos persistentes, dos adaptadores de delegación y las funciones Arduino:

- `setup()`: iniciar Serial, `ledController.begin()`, `distanceSensor.begin()` y `mqttClient.begin()` en ese orden.
- `loop()`: `distanceSensor.update()` y `mqttClient.update()`. Sin `delay()`, interpretación JSON, cálculos de distancia ni lógica de reconexión.

Los objetos globales en este archivo están justificados por el ciclo de vida Arduino. Los constructores no configuran hardware antes de `setup()`.

En `platformio.ini`, usar `src_dir = .` y filtros explícitos que solo compilen estos archivos:

| Entorno | Fuentes incluidas mediante `build_src_filter` | Dependencias externas |
|---|---|---|
| `esp32doit-devkit-v1` (predeterminado) | `src/main.cpp`, `src/DistanceSensor.cpp`, `src/LedController.cpp`, `src/MqttClient.cpp` | Las tres de §2 |
| `hardware-check` | `checks/hardware.cpp`, `src/DistanceSensor.cpp`, `src/LedController.cpp` | Ninguna |
| `mqtt-check` | `checks/mqtt.cpp`, `src/MqttClient.cpp` | Las tres de §2 |

Cada filtro comienza por excluir todo (`-<*>`) e incluye únicamente las rutas indicadas. No se compilan `.pio/`, ejemplos, otros ejecutables ni dos pares de `setup/loop`. Mantener `include_dir = include`. El integrante 2 prepara los tres entornos antes de repartir la implementación, de modo que la Parte 1 no necesite editar la configuración compartida.

Comandos previstos desde la raíz:

```powershell
pio run --project-dir firmware -e hardware-check
pio run --project-dir firmware -e mqtt-check
pio run --project-dir firmware -e esp32doit-devkit-v1
```

La carga se hace seleccionando el entorno y puerto reales con `--target upload --upload-port COM3`; el monitor a 115200 baudios. La ejecución por serial de las pruebas no añade un canal directo al sistema final.

## 7. Orden de implementación

1. Confirmar pines y aprobar este plan; no escribir clases funcionales antes de ello.
2. Integrante 2 prepara entornos y verifica disponibilidad de las versiones fijadas. Los contratos públicos de §3 quedan congelados para ambas partes.
3. Integrante 1 implementa LEDs, sensor y prueba de hardware. Verifica compilación sin secretos ni librerías de red y luego el montaje físico.
4. Integrante 2 implementa conexiones, recepción acotada, validación y publicación, usando los adaptadores de prueba. Verifica reconexión sin necesitar archivos de la Parte 1.
5. Ensamblar `main.cpp` y compilar el entorno integrado. No trasladar lógica de una parte a la otra para resolver la integración.
6. Ejecutar los casos de §8, configurar la app según el contrato y verificar los objetivos físicos.
7. Actualizar `docs/firmware.md` y README con funcionamiento real, pines, comandos de uso, decisiones y límites comprobados. No describir como realizadas las pruebas pendientes.

Cada integrante conserva sus archivos y realiza sus propios commits cuando lo solicite. No se modifican el servidor MCP ni su plan como parte de esta implementación.

## 8. Verificación y criterios de cierre

| Caso | Procedimiento | Resultado requerido |
|---|---|---|
| Independencia | Compilar `hardware-check` sin `secrets.h` y `mqtt-check` sin fuentes de sensor/LEDs | Ambas partes compilables por separado |
| Arranque | Reiniciar la placa | Tres LEDs apagados; conexión iniciada sin detener el sensor |
| Precisión | Diez lecturas en 5, 20, 50, 100 y 200 cm, objeto plano perpendicular y cinta métrica | Error absoluto ≤3 cm; registrar también descartes, sin ocultarlos |
| Sin eco | Retirar objeto y probar ECHO sin pulso | Timeout ≤15000 µs de captura; ningún dato inventado ni bloqueo |
| Frecuencia | Objeto válido, suscriptor con marcas de recepción durante 60 s | Cadencia local de 1000 ms; aproximadamente 60 muestras según bordes de la ventana; distinguir pérdidas MQTT de la programación local |
| LEDs | Seis acciones desde app, repetir cada una | Solo cambia el color indicado; repeticiones no invierten estado |
| Validación | JSON roto, vacío, array, clave extra/ausente, `blue`, `ON`, `state:true`, `state:1`, `null`, contenido después del objeto | No se aplica ningún comando |
| Recepción | Mensajes divididos en fragmentos y mensajes >128 bytes | Mensaje válido reconstruido una vez; exceso o secuencia inválida descartado |
| Retención | Publicar una orden retenida antes de suscribir | No se ejecuta al conectar; borrar luego el mensaje retenido de prueba del broker |
| Latencia | Diez acciones de la app, medir con vídeo desde pulsación hasta LED | Todas ≤1 s bajo las condiciones de demo |
| WiFi | Apagar y restaurar hotspot tres veces | LEDs conservados; nuevas lecturas/comandos disponibles ≤30 s después de restaurar |
| Broker | Interrumpir solo acceso MQTT, incluido tráfico descartado sin cierre limpio, y restaurar | Sin reinicio; timeout, reconexión y nueva suscripción ≤30 s tras restaurar |
| Ausencia prolongada | Mantener red caída ≥1 min | Sensor sigue activo, sin espera indefinida ni cola creciente de lecturas |
| Estabilidad | Medir y accionar LEDs durante ≥10 min | Sin reinicios, watchdog ni bloqueos; sin descenso sostenido de memoria libre |
| Responsividad local | Instrumentación temporal de duración de `update()` y período entre vueltas durante errores de red | Ninguna espera de red en `loop()`; objetivo de diagnóstico <20 ms por vuelta, excluyendo pausa de depuración |

Las pruebas de fragmentación y temporización se apoyarán en los ejecutables de comprobación; no añadir un sistema de pruebas genérico. Las comprobaciones de app/MCP usan exclusivamente el broker. La comprobación de RF6 completa corresponde a la integración posterior con el servidor MCP.

Antes de cerrar: revisar ausencia de secretos, comentarios y logs en inglés, interfaces separadas, documentación actualizada y errores tratados sin reinicios. Si una dependencia introduce una espera que invalida los tiempos o el montaje no cumple el error declarado, detener la implementación afectada y consultar; no bajar silenciosamente los objetivos.

## 9. Referencias técnicas consultadas

- [GPIO ESP32, restricciones por pin](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-reference/peripherals/gpio.html).
- [Arduino-ESP32: interrupciones GPIO y `attachInterruptArg`](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/gpio.html).
- [AsyncMqttClient 0.9.0: API](https://github.com/marvinroger/async-mqtt-client/blob/v0.9.0/src/AsyncMqttClient.hpp) y [dependencia de AsyncTCP](https://github.com/marvinroger/async-mqtt-client/blob/v0.9.0/library.json).
- [Implementación de AsyncMqttClient 0.9.0](https://github.com/marvinroger/async-mqtt-client/blob/v0.9.0/src/AsyncMqttClient.cpp): fragmentos de entrada, limpieza de sesión y publicación asíncrona.
- [ArduinoJson 6: documento estático](https://arduinojson.org/v6/api/staticjsondocument/) y [deserialización con longitud y límite de anidamiento](https://arduinojson.org/v6/api/json/deserializejson/).

Estas referencias sustentan la selección técnica. No sustituyen la compilación conjunta de dependencias ni las mediciones físicas.
