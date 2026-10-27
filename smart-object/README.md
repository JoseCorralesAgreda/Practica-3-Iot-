# Smart object

Minimal PlatformIO firmware for ESP32 Dev Module (`esp32dev`) using Arduino.

## Behavior

Prints `Smart object started` at 115200 baud, then `Smart object is running | uptime_ms=...` approximately once per second. No sensors, actuators, network, or credentials are required.

## Build and run

Install PlatformIO Core or PlatformIO IDE. From this folder:

```powershell
pio run
pio device list
pio run --target upload --upload-port COM3
pio device monitor --port COM3 --baud 115200
```

Replace `COM3` with the actual port. Uploading replaces the board's existing firmware. Confirm the board matches `esp32dev`. Press Ctrl+C to close the monitor.

A successful build verifies compilation. Physical execution requires observing repeated heartbeat messages on the connected ESP32.

## Layout

- `src/main.cpp`: startup and heartbeat loop.
- `include/config.example.h`: future configuration template.
- `lib/`: future sensor, actuator, and connectivity classes.
- `test/`: future tests.
- `config/`: future mobile panel export.

For future connectivity, copy the template to `include/config.h`, which is ignored by Git.
