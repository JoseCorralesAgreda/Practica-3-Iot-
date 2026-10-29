# Servidor MCP

`mcp-server/server.py` expone `get_distance` y `set_led` mediante `FastMCP` por stdio. `mqtt_client.py` contiene el único componente de red. Python requiere 3.11 o posterior; `requirements.txt` fija `mcp==1.30.0` y `paho-mqtt==2.1.0`. El [plan implementado](plans/03-mcp-server-plan.md) queda archivado sin modificaciones.

## Herramientas

`get_distance()` no recibe argumentos de la IA. Devuelve el último valor `cm`, el tiempo desde su recepción y el estado. Las muestras de más de 3 s se marcan `stale`, conservando su valor y edad sin presentarlas como actuales. Si aún no llegó una muestra, devuelve `no_data`; si MQTT no terminó su suscripción, devuelve `disconnected`. La herramienta toma una instantánea de memoria y no espera al broker.

`set_led(led, state)` acepta `red`, `yellow` o `green` y `on` u `off`. Publica `{"led":"red","state":"on"}` en `sis234/grupo2/leds/cmd`, QoS 1, sin retención. Devuelve `published` solo después del PUBACK del broker y siempre indica `device_confirmed: false`: el sistema no tiene un tópico de confirmación física. También informa `disconnected`, `busy`, `not_sent` o `unknown`. No publica parámetros inválidos ni reintenta automáticamente una entrega incierta.

La instrucción «Enciende el LED rojo y apaga los demás» se resuelve con llamadas secuenciales: rojo `on`, amarillo `off` y verde `off`. No es una acción atómica; Claude debe detenerse ante una entrega `unknown` e informar qué resultados confirmó el broker.

## Flujo MQTT

Al iniciar el ciclo de vida MCP, FastMCP crea `MqttClient` y arranca un único hilo daemon. La conexión no retrasa `initialize` ni `tools/list`. El hilo ejecuta el bucle Paho y reconecta con una instancia nueva cada 5 s. Usa MQTT 3.1.1, sesión limpia, `CallbackAPIVersion.VERSION2`, keepalive de 10 s e identificador `sis234/grupo2/mcp/<uuid4>`. Se conecta a `broker.hivemq.com:1883` por TCP, sin TLS ni autenticación.

Al recibir CONNACK, se suscribe a `sis234/grupo2/distance` con QoS 0 y solo habilita las herramientas cuando llega el SUBACK correspondiente. Cada callback verifica la instancia activa, para que una conexión retirada no pueda modificar la sesión nueva. Los datos retenidos o dirigidos a otro tópico no se aceptan.

Una muestra MQTT se limita a 128 bytes y debe ser UTF-8 con un objeto JSON que tenga exactamente una clave `cm`: número finito entre 5 y 200. Se descartan claves duplicadas, texto numérico, booleanos, `null`, NaN, infinitos y estructuras fuera del contrato. Los datos inválidos no rejuvenecen la última muestra. Una desconexión sí la borra; después de reconectar se espera una lectura nueva del firmware.

Un lock breve protege la caché, la sesión y una única orden pendiente. Las herramientas concurrentes pueden leer la distancia mientras hay una orden; si llega otro `set_led`, devuelve `busy`. El hilo de red comprueba el plazo y la sesión, publica QoS 1 y atiende el PUBACK en el mismo bucle Paho. La herramienta espera en un hilo auxiliar durante como máximo 3 s; así no detiene el event loop MCP.

El resultado es `published` cuando el PUBACK llega antes del plazo. Si vence antes de intentar publicar, `not_sent`; si ya se intentó enviar y se pierde conexión, se cancela la llamada o vence el plazo, `unknown`. Este resultado no permite saber si el actuador recibió el comando. Se invalida la sesión y se desecha su instancia Paho: la sesión MQTT limpia no vacía la cola local de salida de Paho, así que el próximo intento crea una instancia nueva. Antes de desconectar, el trabajador cierra el socket público para evitar que Paho vacíe una orden vencida. Un paquete ya entregado al broker no se puede retirar.

Al cerrar el servidor, el ciclo de vida resuelve la orden pendiente e invoca `stop()` en un hilo auxiliar. La espera se limita a 2 s; una resolución DNS atascada no debe retener el proceso porque el trabajador es daemon. Un resultado terminal no cambia por un PUBACK tardío.

## MCP por stdio y registro

El servidor escribe mensajes del protocolo en stdout y dirige los logs a stderr. La instancia MQTT se crea dentro del `lifespan`, no al importar `server.py`, lo que permite importar las herramientas en pruebas sin conectarse al broker. La configuración de Claude Desktop está en [la plantilla](../config/claude_desktop_config.example.json); usa rutas genéricas que deben ajustarse en la máquina donde se ejecute.

## Verificación

`python -m unittest discover -s mcp-server/tests -p "test_*.py"` ejecuta 23 pruebas locales con Paho real para la configuración y dobles de transporte para fallos y plazos. Se verifican validación de distancias, antigüedad, SUBACK, seis combinaciones de LEDs, rechazo de parámetros, caducidad, cancelación, resultados inciertos, reinicio de sesión y cierre limitado. `tests/smoke_test.py` abre un proceso MCP real por stdio, comprueba exactamente los dos esquemas, consulta la herramienta de distancia y verifica parámetros LED inválidos. La sesión de prueba observó `disconnected` cuando el entorno no permitió al trabajador conectarse.

Se verificó Paho contra HiveMQ en tópicos temporales aleatorios `sis234/grupo2/check/<uuid>/`: un cliente testigo confirmó la suscripción QoS 1, recibió la muestra JSON y recibió la orden LED después del PUBACK. Esto valida el transporte y el manejo de acknowledgements usando un segundo cliente de prueba; no acciona ni verifica el ESP32. La integración con sus tópicos de producción y la prueba en Claude Desktop —incluidas las tres llamadas de la orden compuesta— siguen pendientes.
