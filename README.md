# Práctica MQTT — Grupo 2

SIS-234: ESP32 DOIT DevKit V1, HC-SR04 y tres LEDs, integrados con IoT MQTT Panel y Claude Desktop mediante MQTT y MCP. [Especificación](Práctica_MQTT.md).

**Estado:** Ambas partes del firmware implementadas e integradas (HC-SR04, LEDs, WiFi y MQTT), con compilación de los tres entornos y 45 comprobaciones lógicas aprobadas en PC. Pendiente de verificación física y de red en ESP32. Las herramientas funcionales MCP todavía no están implementadas. Integrantes: pendiente registrar los tres nombres.

## Organización

- `firmware/`: proyecto PlatformIO (Arduino).
- `mcp-server/`: servidor Python y prueba de integración MCP.
- `config/`: ejemplo de configuración de Claude Desktop; aquí se incorporará la exportación real del panel.
- [Plan general](docs/plans/01-general-plan.md): aprobado por el usuario; se conserva como archivo histórico.
- [Plan del firmware](docs/plans/02-firmware-plan.md): ambas partes implementadas, con resistencias de 220 Ω y cambio aprobado a ESP-MQTT. El plan se conserva como archivo histórico; las adaptaciones están en la documentación del firmware.
- [Plan del servidor MCP](docs/plans/03-mcp-server-plan.md): herramientas, datos recientes, conexión y pruebas; pendiente de aprobación.
- [Firmware](docs/firmware.md) y [servidor MCP](docs/mcp-server.md): funcionamiento actual.

## Contrato aprobado

Broker: `broker.hivemq.com:1883`, sin TLS ni autenticación. Todo acceso al objeto pasa por el broker.

| Tópico | Mensaje | Publica | Recibe |
|---|---|---|---|
| `sis234/grupo2/distance` | `{"cm":42.7}` | ESP32 | App y MCP |
| `sis234/grupo2/leds/cmd` | `{"led":"red","state":"on"}` | App y MCP | ESP32 |

Colores: `red`, `yellow`, `green`; estados: `on`, `off`. Distancia con QoS 0 y comandos con QoS 1, ambos sin retención; identificadores únicos por cliente. El firmware implementa este contrato; cada comando cambia solo el LED indicado.

Herramienta actual: `get_status()`, informa disponibilidad del servidor y conexiones aún no implementadas. Herramientas previstas: `get_distance()` (última lectura y antigüedad) y `set_led(led, state)` (publicar un comando validado).

## Valores declarados para la demo

| Atributo | Objetivo pendiente de verificación física |
|---|---|
| Rango | 5–200 cm, objeto plano perpendicular |
| Exactitud | Error máximo ±3 cm |
| Publicación | 1 lectura/s, cada 1000 ms, con medición válida |
| Respuesta app → LED | ≤1 s |
| Estabilidad | ≥10 min sin reinicios ni bloqueos |
| Reconexión | WiFi y MQTT en ≤30 s tras volver el servicio; reintentos cada 5 s |

Se adopta la decisión explícita de 1000 ms de AGENTS.md §3.2 frente a los 200 ms de §6.3; ≥2 lecturas/s es una referencia de la especificación.

## Ejecutar el firmware

Copiar `firmware/include/secrets.example.h` a `firmware/include/secrets.h` y completar el nombre y la contraseña WiFi **solo localmente**; el archivo real está excluido de Git. El broker no necesita credenciales. Desde la raíz, con PlatformIO instalado:

```powershell
pio run --project-dir firmware -e esp32doit-devkit-v1
pio run --project-dir firmware -e esp32doit-devkit-v1 --target upload --upload-port COM3
pio device monitor --port COM3 --baud 115200
```

El firmware inicia con los LEDs apagados y publica las lecturas válidas cada 1000 ms. `MQTT ready` indica que la suscripción de comandos fue confirmada. El `client_id` deriva de la MAC de la placa. Ver [funcionamiento y reconexión](docs/firmware.md).

Para compilar y cargar la prueba de la Parte 1, desde la raíz con PlatformIO instalado:

```powershell
pio run --project-dir firmware -e hardware-check
pio run --project-dir firmware -e hardware-check --target upload --upload-port COM3
pio device monitor --port COM3 --baud 115200
```

Cambiar `COM3` por el puerto real. La carga reemplaza el firmware de la placa. Conectar TRIG a GPIO18, ECHO a GPIO19 y los LEDs rojo/amarillo/verde a GPIO25/26/27, cada uno con una resistencia en serie de **220 Ω**. HC-SR04 a 5 V y GND común; ver [montaje y funcionamiento](docs/firmware.md).

El monitor muestra distancias válidas y un mensaje de actividad por segundo. Enviar `r/R`, `y/Y` o `g/G` para encender/apagar el LED correspondiente (minúscula enciende, mayúscula apaga). No necesita `secrets.h`, WiFi ni MQTT. Este control serial es solo una prueba local del hardware.

Para probar únicamente MQTT, usar `-e mqtt-check` en los comandos de compilación y carga. Ejecuta las comprobaciones internas antes de conectar; después publica 42.7 cm cada segundo y muestra los comandos recibidos por Serial sin tocar GPIO. El entorno predeterminado `esp32doit-devkit-v1` ejecuta el sistema integrado.

## Ejecutar la base MCP

Con Python ≥3.11:

```powershell
python -m venv mcp-server/.venv
./mcp-server/.venv/Scripts/python.exe -m pip install -r mcp-server/requirements.txt
./mcp-server/.venv/Scripts/python.exe mcp-server/tests/smoke_test.py
./mcp-server/.venv/Scripts/python.exe mcp-server/server.py
```

El servidor espera un cliente MCP por stdio. Para Claude Desktop, adaptar las rutas absolutas de [la plantilla](config/claude_desktop_config.example.json) a la instalación local y añadir su entrada a la configuración existente. Reiniciar Claude Desktop; actualmente solo aparecerá `get_status`.

Configuración prevista de IoT MQTT Panel: conexión al broker, identificador único, gauge con tópico de distancia y JSONPath `$.cm`, y seis botones (encender/apagar cada color) que publiquen los JSON propuestos. Exportar el panel real a `config/iot-mqtt-panel.json` si la versión instalada lo permite. El [fabricante](https://snrlab.in/iot/) documenta JSONPath y su [ficha oficial](https://play.google.com/store/apps/details?id=snr.lab.iotmqttpanel.prod) anuncia publicación JSON y exportación; falta verificarlo en el teléfono.
