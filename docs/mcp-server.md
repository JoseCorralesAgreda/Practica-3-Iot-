# Servidor MCP: estado actual

`mcp-server/server.py` usa `FastMCP` del SDK oficial y transporte stdio. La única herramienta implementada es `get_status()`, sin parámetros: devuelve `status: "ok"`, `service: "mqtt-mcp-server"`, `mqtt_connected: false` y `hardware_connected: false`. No consulta el dispositivo.

`mqtt_client.py` es un archivo reservado para la implementación futura. La dependencia actual se declara únicamente en `requirements.txt`; MQTT todavía no requiere ni carga configuración. Se retiraron el paquete anidado y la plantilla `.env` porque no aportaban comportamiento a esta base.

El proceso escribe únicamente mensajes del protocolo en stdout. Los futuros logs irán a stderr, conforme al [SDK oficial](https://py.sdk.modelcontextprotocol.io/handlers/logging/).

`tests/smoke_test.py` inicia `server.py` por ruta absoluta, abre una sesión MCP real, verifica el descubrimiento y resultado de `get_status`, y cierra la sesión. Tiene un límite total de 30 s y no accede al broker. Sus impresiones pertenecen al proceso de prueba, no al servidor.

El [README](../README.md) contiene instalación y ejecución. La [plantilla para Claude Desktop](../config/claude_desktop_config.example.json) usa rutas genéricas al intérprete del entorno virtual y al script.

El [plan general](plans/01-general-plan.md) propone guardar la última lectura con su hora de recepción para responder sin esperar indefinidamente. La suscripción MQTT, validación, caducidad y publicación de comandos se implementarán después de aprobar el plan detallado.
