# MQTT practice: ESP32 and MCP server

Minimal projects based on [the assignment](Práctica_MQTT.md).

- `smart-object/`: ESP32 Arduino firmware managed by PlatformIO. Prints a startup message and a heartbeat every second.
- `mcp-server/`: Python MCP server using stdio. Exposes `get_status` and includes an integration smoke test.

See each project's README for setup and execution commands. Code and project folder names are in English.

## Current scope

Both components run independently. WiFi, MQTT, sensors, actuators, and the mobile panel are not implemented yet. MCP status explicitly reports MQTT and hardware as disconnected.

## Assignment checklist

- Record group members and select at least three LEDs or one servo.
- Define pins, a unique MQTT topic prefix, and message formats.
- Implement MQTT and sensor/actuator tools.
- Configure the AI client and mobile panel.
- Declare and physically verify stability, accuracy, latency, publication frequency, and reconnection requirements.
- Keep credentials in ignored local configuration files.
