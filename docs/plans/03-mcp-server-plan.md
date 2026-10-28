# Plan del servidor MCP — Grupo 2

Fecha: 27 de octubre de 2026. Estado: pendiente de aprobación; documento de diseño, sin implementación funcional. Una vez ejecutado, se archiva sin modificaciones.

Se desarrolla el [plan general aprobado](01-general-plan.md) conforme a [AGENTS.md](../../AGENTS.md) y la [especificación](../../Práctica_MQTT.md). El montaje del [plan del firmware](02-firmware-plan.md) utiliza una resistencia de **220 Ω por LED**, según la corrección del usuario; ese valor no requiere cambios de software en MCP.

## 1. Alcance y arquitectura

Claude Desktop inicia el servidor Python como proceso local y utiliza MCP por stdio. El servidor se conecta exclusivamente a `broker.hivemq.com:1883` mediante MQTT TCP, sin TLS ni autenticación. No abre HTTP, WebSocket, serial ni conexiones directas al ESP32.

El servidor expone dos herramientas: `get_distance` y `set_led`. La herramienta provisional `get_status` se elimina al implementar este plan, porque no forma parte del contrato funcional aprobado. No se añade base de datos, historial, configuración por variables de entorno ni recursos/prompts MCP.

```mermaid
flowchart LR
    C[Claude Desktop] <-->|MCP stdio| S[server.py: herramientas]
    S <-->|llamadas locales| M[mqtt_client.py: cliente y última lectura]
    M <-->|MQTT| B[(HiveMQ público)]
    B <-->|MQTT| E[ESP32]
```

## 2. Archivos y responsabilidad

| Archivo | Contenido final |
|---|---|
| `mcp-server/server.py` | Instancia FastMCP, ciclo de vida, dos herramientas y entrada `main()` |
| `mcp-server/mqtt_client.py` | Clase `MqttClient`, validación de muestras, caché y ciclo MQTT |
| `mcp-server/requirements.txt` | Único archivo de dependencias |
| `mcp-server/tests/smoke_test.py` | Prueba del proceso real mediante protocolo MCP |
| `mcp-server/tests/test_mqtt_client.py` | Casos deterministas de validación, antigüedad y comandos pendientes |
| `config/claude_desktop_config.example.json` | Rutas genéricas del intérprete y servidor |
| `docs/mcp-server.md`, `README.md` | Documentación viva, actualizada después de implementar |

Responsable: integrante 3. No modifica archivos del firmware ni desarrolla código de la app móvil. El archivo de pruebas adicional verifica errores de red y datos, no constituye una capa de producción.

Python ≥3.11. Dependencias previstas: `mcp==1.30.0` y `paho-mqtt==2.1.0`. La versión MCP 1.30.0 está instalada en el entorno local y su API FastMCP se inspeccionó al elaborar este plan. Mantener la línea 1.x evita migrar la base existente; no instalar una versión mayor de manera implícita. Usar `unittest`, `json`, `threading`, `queue`, `time`, `uuid`, `logging` y `asyncio` de la biblioteca estándar; sin pytest ni frameworks adicionales.

No se crean `.env`, secretos del broker ni archivos de configuración propios. Host, tópicos y límites son constantes en `mqtt_client.py`.

## 3. Contrato MQTT conservado

| Uso | Tópico | Formato | QoS | Retain |
|---|---|---|---|---|
| Suscribir distancia | `sis234/grupo2/distance` | `{"cm":42.7}` | 0 | Rechazar muestras recibidas como retenidas |
| Publicar comando | `sis234/grupo2/leds/cmd` | `{"led":"red","state":"on"}` | 1 | false |

Colores exactos: `red`, `yellow`, `green`. Estados exactos: `on`, `off`. Cada comando modifica un LED; no añade campos ni apaga automáticamente los demás. La orden «Enciende el LED rojo y apaga los demás» requiere tres llamadas independientes.

MQTT 3.1.1, `clean_session=True`, `CallbackAPIVersion.VERSION2`. Identificador `sis234/grupo2/mcp/<uuid4>` por proceso; solo un cliente activo por proceso. Tras cada reconexión, suscribir nuevamente distancia y esperar SUBACK antes de declarar listo el cliente.

## 4. Herramientas y respuestas

Las firmas públicas usarán type hints y docstrings en inglés. Los resultados son diccionarios JSON con claves y mensajes en inglés; Claude los explica al usuario en español. `ctx` es contexto interno del SDK, no un parámetro que deba enviar la IA.

### 4.1 `get_distance`

Firma prevista: `def get_distance(ctx: Context) -> dict[str, str | float | bool | None]`.

Descripción: `Return the latest distance in centimeters and its age. Report missing, stale, or disconnected data without waiting for a new measurement.`

Sin argumentos visibles. La consulta lee una instantánea local y responde inmediatamente; nunca solicita una lectura al ESP32 ni espera el siguiente mensaje.

| `status` | Condición | `cm` / `age_seconds` | `mqtt_connected` |
|---|---|---|---|
| `ok` | Última muestra válida con edad ≤3 s | Número / edad no negativa | true |
| `stale` | Hay muestra válida pero edad >3 s | Último valor / edad actual | true |
| `no_data` | Conexión lista, todavía sin muestra válida | null / null | true |
| `disconnected` | Conexión o suscripción no disponible | null / null | false |

Cada respuesta incluye además `message`. Ejemplo de muestra reciente:

```json
{"status":"ok","cm":42.7,"age_seconds":0.4,"mqtt_connected":true,"message":"Recent distance measurement."}
```

Para `stale`, el mensaje es `The last measurement is stale; it does not describe the current distance.` No ocultar el valor antiguo ni presentarlo como actual. Para `no_data`: `No valid measurement has arrived yet.` Para `disconnected`: `MQTT is not ready; current distance is unavailable.`

Antigüedad calculada con `time.monotonic()`, comparando el valor sin redondear; redondear a tres decimales solo al construir la respuesta. La marca de recepción pertenece al servidor, no al sensor: no demuestra el instante físico de captura. El contrato MQTT aprobado no incluye reloj ni secuencia del ESP32.

### 4.2 `set_led`

Firma prevista: `async def set_led(led: Literal["red", "yellow", "green"], state: Literal["on", "off"], ctx: Context) -> dict[str, str | bool]`.

Descripción: `Publish an explicit on/off command for one LED through MQTT. Broker acknowledgement does not confirm the physical LED state. Do not automatically retry an uncertain result.`

El esquema MCP declara ambos parámetros obligatorios y sus valores permitidos. La clase MQTT repite la validación antes de crear cualquier mensaje; los parámetros inválidos producen un error de herramienta, sin publicar. No normalizar mayúsculas ni traducir colores en el servidor: Claude debe proporcionar los valores del esquema.

Respuestas de operación:

| `status` | Significado | `broker_acknowledged` |
|---|---|---|
| `published` | Se recibió el PUBACK del broker para ese mensaje | true |
| `disconnected` | Cliente no listo; no se aceptó la orden | false |
| `busy` | Ya existe una orden pendiente; no se aceptó otra | false |
| `not_sent` | La orden expiró o falló antes de publicarse | false |
| `unknown` | Se intentó publicar, pero no llegó PUBACK antes del timeout o se perdió la conexión | false |

Todas contienen `led`, `state`, `broker_acknowledged`, `device_confirmed:false` y `message`. Ejemplo:

```json
{"status":"published","led":"red","state":"on","broker_acknowledged":true,"device_confirmed":false,"message":"Broker acknowledged the command; physical LED state is unconfirmed."}
```

Ante `unknown`: `Delivery is uncertain. The command may have reached the device; do not automatically retry.` El timeout nunca significa que el LED seguro no cambió. Un mensaje ya entregado al broker no puede retirarse. Sin tópico de confirmación física no se afirma que el ESP32 esté conectado ni que haya aplicado el comando.

La llamada completa espera como máximo 3 s desde su aceptación local. No bloquea el event loop MCP: el resultado se espera mediante `asyncio.to_thread` sobre un `threading.Event` con timeout acotado. `get_distance` puede responder mientras hay un comando pendiente.

Las condiciones de red se devuelven como estados del resultado; una validación rechazada o fallo interno inesperado produce error MCP. Ningún fallo se disfraza de `published`.

## 5. Cliente MQTT y estado compartido

Clase única de componente: `MqttClient`. API pública:

| Método | Responsabilidad |
|---|---|
| `start() -> None` | Iniciar el trabajador MQTT sin esperar conexión |
| `stop() -> None` | Solicitar cierre, resolver pendientes y esperar como máximo 2 s al trabajador |
| `get_distance() -> dict[str, str | float | bool | None]` | Devolver instantánea de muestra y conexión |
| `async set_led(led: str, state: str) -> dict[str, str | bool]` | Validar, admitir una orden y esperar su resultado acotado |

Métodos privados: `_run`, `_create_client`, `_on_connect`, `_on_subscribe`, `_on_disconnect`, `_on_message`, `_on_publish`, `_process_command`, `_invalidate_connection` y `_parse_distance`.

El cliente mantiene la última distancia, instante de recepción, estado de conexión/suscripción, identificador de sesión y como máximo una orden pendiente. La orden se representa con una pequeña `dataclass` privada `_PendingCommand` dentro del mismo módulo: color, estado, plazo monotónico, generación, indicador de envío, resultado y evento de finalización. Representa datos de una operación, no otra capa de servicio.

Un `threading.Lock` protege las instantáneas, admisión de órdenes y transición terminal de resultados. Nunca mantenerlo durante conexión, espera de evento o cierre. Los callbacks y operaciones Paho se ejecutan en un único trabajador para evitar carreras entre `publish()` y el registro del identificador antes de procesar PUBACK. No depender del GIL para proteger el estado.

### 5.1 Recepción y validación

Aceptar solo el tópico exacto, payload UTF-8 de hasta 128 bytes, no retenido y perteneciente a la sesión vigente. Usar `json.loads` y exigir objeto con una única clave `cm`. Rechazar claves adicionales o duplicadas, arrays, valores de texto, booleanos, null, NaN, Infinity y valores fuera de 5–200 cm. La comprobación numérica excluye explícitamente `bool`, aunque sea subclase de `int` en Python.

Para duplicados, usar `object_pairs_hook` con una función privada breve que rechace claves repetidas. Después del parseo, comprobar finitud y rango. No convertir `"42.7"` a número ni redondear antes de validar.

Una muestra válida reemplaza valor y marca monotónica en una única sección protegida. Un mensaje inválido se descarta sin rejuvenecer ni borrar la última muestra válida: esta envejece naturalmente. Una desconexión sí borra la caché; al reconectar se espera una muestra nueva. No aceptar mensajes de una instancia de cliente ya retirada.

### 5.2 Límites y constantes

| Constante | Valor |
|---|---|
| `BROKER_HOST`, `BROKER_PORT` | `broker.hivemq.com`, 1883 |
| `DISTANCE_TOPIC`, `COMMAND_TOPIC` | Los dos tópicos de §3 |
| `MAX_READING_AGE_SECONDS` | 3.0 |
| `MIN_DISTANCE_CM`, `MAX_DISTANCE_CM` | 5.0, 200.0 |
| `MAX_PAYLOAD_BYTES` | 128 |
| `RECONNECT_INTERVAL_SECONDS` | 5.0 |
| `CONNECT_TIMEOUT_SECONDS` | 5.0 |
| `HANDSHAKE_TIMEOUT_SECONDS` | 5.0 para CONNACK y 5.0 para SUBACK |
| `KEEPALIVE_SECONDS` | 10 |
| `NETWORK_LOOP_TIMEOUT_SECONDS` | 0.1 |
| `COMMAND_TIMEOUT_SECONDS` | 3.0 |
| `SHUTDOWN_TIMEOUT_SECONDS` | 2.0 |

Usar `max_inflight_messages_set(1)` y `max_queued_messages_set(1)`; no existe cola de órdenes offline. Un nuevo comando concurrente recibe `busy` en lugar de esperar detrás de otro.

## 6. Ciclo de red, reintentos y comandos sin confirmar

Se usa un solo hilo trabajador daemon para las operaciones MQTT. El hilo ejecuta una conexión Paho y llama repetidamente a `loop(timeout=0.1)`, sin combinarlo con `loop_start` ni `loop_forever`. Esta elección permite retirar la instancia que conserva un comando pendiente y controlar las reconexiones en un único lugar. No se añade otra conexión MQTT por herramienta.

Flujo de `_run`:

1. Crear Paho con reconexión automática desactivada y timeout de conexión de 5 s. Intentar `connect` en el trabajador, nunca en el hilo del servidor MCP. Los intentos se espacian al menos 5 s usando `stop_event.wait`, que permite despertar al cerrar.
2. Atender la red y los plazos de CONNACK/SUBACK. Al recibir conexión aceptada, suscribir distancia con QoS 0; al recibir SUBACK correspondiente, habilitar herramientas. Un rechazo o plazo vencido retira esa instancia.
3. Procesar la orden pendiente únicamente si sigue vigente, la sesión coincide y el cliente está listo. Generar JSON con `json.dumps`, publicar QoS 1 y `retain=False`; registrar el identificador antes de volver a procesar la red. No llamar `wait_for_publish()` dentro del trabajador, porque este mismo hilo debe atender PUBACK.
4. `_on_publish` completa exclusivamente la orden correspondiente a ese cliente, generación e identificador. El resultado terminal se establece una sola vez; un PUBACK tardío no modifica una respuesta ya emitida.
5. Si hay desconexión, error de red o timeout de una publicación enviada, invalidar la sesión, borrar caché y resolver pendiente como `unknown` si se intentó enviar, o `not_sent` si no se envió. Retirar y desechar la instancia Paho; crear una nueva en el próximo intento. No acceder a colecciones privadas de Paho para borrar mensajes.
6. Si el plazo vence mientras la orden aún no fue enviada, cancelarla antes de publicar. Una cancelación de la llamada MCP aplica la misma regla; si ya fue enviada, retirar la sesión e informar por log que la entrega fue incierta.

La sesión limpia por sí sola no elimina la cola local de salida de Paho. La [documentación del cliente](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html) indica que reconectar puede reenviar mensajes propios; sus [limitaciones conocidas](https://eclipse.dev/paho/files/paho.mqtt.python/html/index.html#known-limitations) también describen esa retransmisión con QoS >0. Por ello se descarta la instancia tras una interrupción, en lugar de reutilizarla con comandos pendientes. Aun así, un mensaje que alcanzó el broker puede haberse ejecutado y no es cancelable retroactivamente.

La resolución DNS depende del sistema operativo y puede exceder el timeout de socket. Se mantiene fuera de MCP; el inicio y las herramientas siguen respondiendo. No lanzar hilos nuevos mientras uno sigue resolviendo ni prometer recuperación dentro de un plazo no verificado. La prueba de fallos debe distinguir DNS, TCP y ausencia de respuesta MQTT.

El trabajador comprueba cierre entre operaciones. En la salida normal desconecta, invalida caché y resuelve pendientes. `stop()` espera hasta 2 s; si el sistema operativo sigue bloqueando DNS, registra el límite y deja que el proceso finalice con el hilo daemon. No mantener vivo Claude Desktop por una espera de red indefinida.

## 7. Ciclo de vida MCP y logs

Crear FastMCP con un `lifespan` mediante `asynccontextmanager`. Al entrar, crear `MqttClient`, llamar `start()` y entregar un contexto con esa instancia; no esperar conexión para responder a `initialize` o `tools/list`. Las herramientas obtienen la instancia desde `ctx.request_context.lifespan_context`.

En `finally`, llamar a `stop()` fuera del event loop mediante `asyncio.to_thread`. `main()` configura logging y ejecuta `server.run(transport="stdio")`. No iniciar conexiones al importar el módulo; las pruebas podrán importarlo sin conectarse al broker.

Toda salida de diagnóstico va a stderr mediante `logging.StreamHandler(sys.stderr)`. Stdout queda reservado a mensajes MCP del SDK. Registrar transiciones y errores con mensajes breves en inglés; limitar avisos por payload inválido a uno por segundo con contador. No imprimir payloads completos ni emitir logs por cada muestra correcta.

Los nombres y parámetros de las herramientas forman parte del contrato de la IA. Sus descripciones deben advertir sobre datos antiguos y diferencia entre PUBACK y estado físico, sin exigir que el usuario entienda MQTT.

## 8. Configuración de Claude Desktop

Conservar `config/claude_desktop_config.example.json` con la entrada `sis234-grupo2`, `command` apuntando al Python del entorno virtual y `args` al archivo `mcp-server/server.py`. Ambas rutas serán absolutas y genéricas en el repositorio. No añadir `env`, contraseña WiFi ni credenciales de broker.

En la máquina del usuario, fusionar esa entrada con `mcpServers` existente, respetando los otros servidores. Adaptar rutas locales y reiniciar Claude Desktop. Usar `python.exe`, no un comando que imprima instalación o mensajes de shell en stdout antes del servidor.

Instrucciones que se incorporarán al README después de implementar:

```powershell
python -m venv mcp-server/.venv
./mcp-server/.venv/Scripts/python.exe -m pip install -r mcp-server/requirements.txt
./mcp-server/.venv/Scripts/python.exe -m unittest discover -s mcp-server/tests -p "test_*.py"
./mcp-server/.venv/Scripts/python.exe mcp-server/tests/smoke_test.py
```

La configuración actual ya tiene esas rutas genéricas; solo se cambia si la implementación modifica el punto de entrada, lo cual este plan no prevé.

## 9. Pruebas y aceptación

`test_mqtt_client.py` utiliza `unittest` y dobles locales únicamente para comprobar reglas internas, sin publicar en el broker. Controla el reloj monotónico para probar los límites sin dormir tres segundos en cada caso. No confundir estos resultados con una integración MQTT real.

| Prueba | Resultado esperado |
|---|---|
| Inicio sin broker | MCP inicializa, lista las dos herramientas y responde `disconnected` |
| Datos válidos en 5, 42.7 y 200 cm | `ok`, valor correcto y edad calculada |
| Sin muestras / edad exactamente 3 s / mayor de 3 s | `no_data` / `ok` / `stale` |
| Retained, UTF-8 inválido, >128 bytes, booleano, null, NaN, claves extra o duplicadas | Descartar sin actualizar la marca temporal |
| Desconexión y reconexión | Caché invalidada y nueva suscripción; esperar una muestra nueva |
| Parámetros inválidos de LEDs | Error MCP y ninguna publicación |
| Seis combinaciones válidas | Tópico y JSON exactos, QoS 1, retain false |
| PUBACK correcto | `published`, `broker_acknowledged:true`, `device_confirmed:false` |
| Sin PUBACK / caída tras enviar | `unknown` antes de superar 3 s de espera; no reutilizar instancia con salida pendiente |
| Orden expirada antes del envío | `not_sent`, ninguna publicación posterior |
| Dos órdenes concurrentes | Una admitida y otra `busy`; lectura de distancia sigue respondiendo |
| Reconexión después de publicación incierta | Cliente nuevo, sin reenvío local automático de la orden anterior |
| Cierre sin red | Salida acotada y sin hilos no-daemon reteniendo el proceso |

Actualizar `smoke_test.py` para comprobar inicialización, descubrimiento de exactamente `get_distance` y `set_led`, esquema de enums, lectura con estructura válida y rechazo de argumentos inválidos. Mantener timeout global de 30 s y lanzamiento de `server.py` por ruta absoluta. La prueba básica no envía comandos válidos a un dispositivo real.

Integración real, separada de los tests locales: con firmware conectado, confirmar recepción de muestras, apagar el sensor para observar caducidad, cortar acceso MQTT y restaurarlo. Ejecutar en Claude Desktop las dos frases del plan general. Verificar visualmente rojo encendido y los otros apagados; el resultado MCP por sí solo no demuestra el estado de los LEDs. Registrar los tres resultados si la orden compuesta falla parcialmente.

La integración utilizará el broker aprobado y configuraciones reales; no usar publicaciones de muestra en el tópico compartido mientras otro integrante mide precisión. Las pruebas que cambien LEDs se ejecutarán deliberadamente durante la verificación del sistema, no al importar módulos ni iniciar el servidor.

## 10. Orden de implementación y cierre

1. Aprobar este plan. No cambiar código funcional antes de ello.
2. Fijar las dos dependencias y comprobar instalación/importación en el entorno virtual.
3. Implementar validación y caché; probar rango, tipos y antigüedad.
4. Implementar trabajador MQTT, sesión, suscripción y cierre; comprobar errores sin bloquear MCP.
5. Implementar publicación QoS 1 con plazo, resultado incierto y descarte de instancia; comprobar que no haya reenvíos tardíos locales.
6. Integrar lifecycle y herramientas en `server.py`; sustituir `get_status` y actualizar la prueba stdio.
7. Configurar Claude Desktop y ejecutar la integración física mediante el broker.
8. Actualizar README y `docs/mcp-server.md` con comportamiento y resultados realmente verificados; conservar este plan archivado.

No crear commits ni hacer push sin petición explícita. No modificar autorías. Si una dependencia impide respetar timeouts, limpieza de pendientes o protocolo stdio, documentar el impedimento y consultar antes de rediseñar.

Referencias: [SDK oficial MCP, línea 1.x](https://github.com/modelcontextprotocol/python-sdk/tree/v1.x), [API de Paho](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html) y [limitaciones conocidas de Paho](https://eclipse.dev/paho/files/paho.mqtt.python/html/index.html#known-limitations). La inspección local confirmó el soporte de FastMCP para `lifespan`; los casos anteriores deberán verificar el comportamiento de la implementación conjunta.
